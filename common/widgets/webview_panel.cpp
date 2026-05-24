/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <build_version.h>

#include <tool/tool_manager.h>
#include <tool/tool_base.h>

#include <widgets/webview_panel.h>
#include <wx/evtloop.h>
#include <wx/sizer.h>
#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#include <wx/utils.h>
#include <wx/log.h>

#if wxUSE_WEBVIEW_EDGE
    // note webview2 is available on Windows, but not on MINGW (only webview available)
    #include <webview2EnvironmentOptions.h>
#endif


WEBVIEW_PANEL::WEBVIEW_PANEL( wxWindow* aParent, wxWindowID aId, const wxPoint& aPos, const wxSize& aSize,
                              const int aStyle, TOOL_MANAGER* aToolManager, TOOL_BASE* aTool ) :
        wxPanel( aParent, aId, aPos, aSize, aStyle ),
        m_initialized( false ),
        m_handleExternalLinks( false ),
        m_loadError( false ),
        m_loadedEventBound( false ),
        m_browser( nullptr ),
        m_toolManager( aToolManager ),
        m_tool( aTool )
{
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );

    if( !wxGetEnv( wxT( "WEBKIT_DISABLE_COMPOSITING_MODE" ), nullptr ) )
    {
        wxSetEnv( wxT( "WEBKIT_DISABLE_COMPOSITING_MODE" ), wxT( "1" ) );
    }

#if wxCHECK_VERSION( 3, 3, 0 )
    wxWebViewConfiguration config = wxWebView::NewConfiguration();
    m_backend = config.GetBackend();

#if wxUSE_WEBVIEW_EDGE
    if( m_backend == wxWebViewBackendEdge )
    {
        ICoreWebView2EnvironmentOptions* webViewOptions =
                (ICoreWebView2EnvironmentOptions*) config.GetNativeConfiguration();

        // Disable gesture navigation features.
        // Allow file:// pages to connect to localhost WebSockets (needed for AI chat panel).
        webViewOptions->put_AdditionalBrowserArguments( L"--disable-features=msEdgeMouseGestureSupported,"
                                                        L"msEdgeMouseGestureDefaultEnabled,OverscrollHistoryNavigation "
                                                        L"--enable-features=kEdgeMouseGestureDisabledInCN "
                                                        L"--allow-file-access-from-files "
                                                        L"--disable-web-security" );
    }
#endif

    m_browser = wxWebView::New( config );
#else
    m_browser = wxWebView::New();
    m_backend = wxWebViewBackendDefault;
#endif

    wxWebView* browser = GetWebView();

    if( !browser )
        return;

#ifdef __WXMAC__
    browser->RegisterHandler( wxSharedPtr<wxWebViewHandler>( new wxWebViewArchiveHandler( "wxfs" ) ) );
    browser->RegisterHandler( wxSharedPtr<wxWebViewHandler>( new wxWebViewFSHandler( "memory" ) ) );
#endif
    browser->SetUserAgent( wxString::Format( "KiCad/%s WebView/%s", GetMajorMinorPatchVersion(), wxGetOsDescription() ) );
    browser->Create( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize );
    sizer->Add( browser, 1, wxEXPAND );
    SetSizer( sizer );

#ifndef __WXMAC__
    browser->RegisterHandler( wxSharedPtr<wxWebViewHandler>( new wxWebViewArchiveHandler( "wxfs" ) ) );
    browser->RegisterHandler( wxSharedPtr<wxWebViewHandler>( new wxWebViewFSHandler( "memory" ) ) );
#endif

    Bind( wxEVT_WEBVIEW_NAVIGATING, &WEBVIEW_PANEL::OnNavigationRequest, this, browser->GetId() );
    Bind( wxEVT_WEBVIEW_NEWWINDOW, &WEBVIEW_PANEL::OnNewWindow, this, browser->GetId() );
    Bind( wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WEBVIEW_PANEL::OnScriptMessage, this, browser->GetId() );
    Bind( wxEVT_WEBVIEW_SCRIPT_RESULT, &WEBVIEW_PANEL::OnScriptResult, this, browser->GetId() );
    Bind( wxEVT_WEBVIEW_ERROR, &WEBVIEW_PANEL::OnError, this, browser->GetId() );

    m_initRetryTimer.Bind( wxEVT_TIMER, [this]( wxTimerEvent& ) { DoInitHandlers(); } );
}

WEBVIEW_PANEL::~WEBVIEW_PANEL()
{
    m_initRetryTimer.Stop();
}

void WEBVIEW_PANEL::BindLoadedEvent()
{
    if( m_loadedEventBound )
        return;

    wxWebView* browser = GetWebView();

    if( !browser )
        return;

    Bind( wxEVT_WEBVIEW_LOADED, &WEBVIEW_PANEL::OnWebViewLoaded, this, browser->GetId() );
    m_loadedEventBound = true;
}

void WEBVIEW_PANEL::LoadURL( const wxString& aURL )
{
    wxLogTrace( "webview", "Loading URL: %s", aURL );

    if( aURL.starts_with( "file:/" ) && !aURL.starts_with( "file:///" ) )
    {
        wxString new_url = wxString( "file:///" ) + aURL.AfterFirst( '/' );

        if( wxWebView* browser = GetWebView() )
            browser->LoadURL( new_url );

        return;
    }

    if( !aURL.StartsWith( "http://" ) && !aURL.StartsWith( "https://" ) && !aURL.StartsWith( "file://" ) )
    {
        wxLogError( "Invalid URL: %s", aURL );
        return;
    }

    if( wxWebView* browser = GetWebView() )
        browser->LoadURL( aURL );
}

void WEBVIEW_PANEL::SetPage( const wxString& aHtmlContent )
{
    wxLogTrace( "webview", "Setting page content" );

    if( wxWebView* browser = GetWebView() )
        browser->SetPage( aHtmlContent, "file://" );
}

bool WEBVIEW_PANEL::AddMessageHandler( const wxString& aName, MESSAGE_HANDLER aHandler )
{
    wxLogTrace( "webview", "Adding message handler for: %s", aName );
    auto it = m_msgHandlers.find( aName );

    if( it != m_msgHandlers.end() )
    {
        it->second = std::move( aHandler );
        return true;
    }

    m_msgHandlers.emplace( aName, std::move( aHandler ) );

    if( m_initialized )
    {
        wxWebView* browser = GetWebView();

        if( !browser )
            return true;

        wxEventLoopBase* activeLoop = wxEventLoopBase::GetActive();

        if( activeLoop && ( !activeLoop->IsMain() || activeLoop->IsYielding() ) )
        {
            m_initRetryTimer.StartOnce( 200 );
        }
        else
        {
            if( !browser->AddScriptMessageHandler( aName ) )
                wxLogTrace( "webview", "Could not add script message handler %s", aName );
        }
    }

    return true;
}

void WEBVIEW_PANEL::ClearMessageHandlers()
{
    wxLogTrace( "webview", "Clearing all message handlers" );

    if( wxWebView* browser = GetWebView() )
    {
        for( const auto& handler : m_msgHandlers )
            browser->RemoveScriptMessageHandler( handler.first );
    }

    m_msgHandlers.clear();
}

void WEBVIEW_PANEL::OnNavigationRequest( wxWebViewEvent& aEvt )
{
    m_loadError = false;
    wxLogTrace( "webview", "Navigation request to URL: %s", aEvt.GetURL() );
    // Default behavior: open external links in the system browser.
    // localhost URLs are always kept inside the WebView (used for local backends like AI chat).
    bool isLocalhost = aEvt.GetURL().StartsWith( "http://localhost" )
                       || aEvt.GetURL().StartsWith( "http://127.0.0.1" );
    bool isExternal = !isLocalhost
                      && ( aEvt.GetURL().StartsWith( "http://" )
                           || aEvt.GetURL().StartsWith( "https://" ) );

    if( isExternal && !m_handleExternalLinks )
    {
        wxLogTrace( "webview", "Opening external URL in system browser: %s", aEvt.GetURL() );
        wxLaunchDefaultBrowser( aEvt.GetURL() );
        aEvt.Veto();
    }
}

void WEBVIEW_PANEL::DoInitHandlers()
{
    wxWebView* browser = GetWebView();

    if( !browser )
        return;

    // WebKit's AddScriptMessageHandler internally calls RunScript which yields the
    // event loop via wxGUIEventLoop::DoYieldFor. If we're inside a nested event loop
    // or the main loop is already mid-yield (e.g., wxProgressDialog pumping events),
    // this causes WebKit's JSC to crash in sanitizeStackForVM when the reentrant
    // yield tries to create a JS context. Re-defer until the loop is idle.
    wxEventLoopBase* activeLoop = wxEventLoopBase::GetActive();

    if( activeLoop && ( !activeLoop->IsMain() || activeLoop->IsYielding() ) )
    {
        m_initRetryTimer.StartOnce( 200 );
        return;
    }

    for( const auto& handler : m_msgHandlers )
    {
        if( !browser->AddScriptMessageHandler( handler.first ) )
            wxLogTrace( "webview", "Could not add script message handler %s", handler.first );
    }

    browser->AddUserScript( R"(
        (function() {
            // Change window.open to navigate in the same window
            window.open = function(url) { if (url) window.location.href = url; return null; };
            window.showModalDialog = function() { return null; };

            if (window.external && window.external.invoke) {
                function notifyHost() {
                    window.external.invoke('navigation:' + window.location.href);
                }
                window.addEventListener('popstate', notifyHost);
                window.addEventListener('pushstate', notifyHost);
                window.addEventListener('replacestate', notifyHost);
                ['pushState', 'replaceState'].forEach(function(type) {
                    var orig = history[type];
                    history[type] = function() {
                        var rv = orig.apply(this, arguments);
                        window.dispatchEvent(new Event(type.toLowerCase()));
                        return rv;
                    };
                });
            }
        })();
    )" );
}


void WEBVIEW_PANEL::OnWebViewLoaded( wxWebViewEvent& aEvt )
{
    if( !m_initialized )
    {
        m_initialized = true;

        if( m_toolManager && m_tool )
            m_toolManager->RunMainStack( m_tool, [this]() { DoInitHandlers(); } );
        else
            m_initRetryTimer.StartOnce( 0 );
    }

    aEvt.Skip();
}

void WEBVIEW_PANEL::OnNewWindow( wxWebViewEvent& aEvt )
{
    if( wxWebView* browser = GetWebView() )
        browser->LoadURL( aEvt.GetURL() );

    aEvt.Veto(); // Prevent default behavior of opening a new window
    wxLogTrace( "webview", "New window requested for URL: %s", aEvt.GetURL() );
    wxLogTrace( "webview", "Target: %s", aEvt.GetTarget() );
    wxLogTrace( "webview", "Action flags: %d", static_cast<int>(aEvt.GetNavigationAction()) );
    wxLogTrace( "webview", "Message handler: %s", aEvt.GetMessageHandler() );
}

void WEBVIEW_PANEL::OnScriptMessage( wxWebViewEvent& aEvt )
{
    wxLogTrace( "webview", "Script message received: %s for handler %s", aEvt.GetString(), aEvt.GetMessageHandler() );
    wxString handler = aEvt.GetMessageHandler();
    handler.Trim(true).Trim(false);

    wxString message = aEvt.GetString();

    // If no handler specified (Edge WebView2 chrome.webview.postMessage),
    // try to parse JSON with {handler, message} fields
    if( handler.IsEmpty() && message.StartsWith( wxT( "{" ) ) )
    {
        // Simple JSON extraction for {"handler":"name","message":"data"}
        int hStart = message.Find( wxT( "\"handler\"" ) );
        int mStart = message.Find( wxT( "\"message\"" ) );

        if( hStart != wxNOT_FOUND )
        {
            wxString sub = message.Mid( hStart + 10 );
            int q1 = sub.Find( wxT( '"' ) );
            if( q1 != wxNOT_FOUND )
            {
                wxString rest = sub.Mid( q1 + 1 );
                int q2 = rest.Find( wxT( '"' ) );
                if( q2 != wxNOT_FOUND )
                    handler = rest.Left( q2 );
            }
        }

        if( mStart != wxNOT_FOUND )
        {
            wxString sub = message.Mid( mStart + 10 );
            int q1 = sub.Find( wxT( '"' ) );
            if( q1 != wxNOT_FOUND )
            {
                wxString rest = sub.Mid( q1 + 1 );
                int q2 = rest.Find( wxT( '"' ) );
                if( q2 != wxNOT_FOUND )
                    message = rest.Left( q2 );
            }
        }

        wxLogTrace( "webview", "Parsed JSON envelope: handler=%s message=%s", handler, message );
    }

    if( handler.IsEmpty() )
    {
        for( auto handlerPair : m_msgHandlers )
        {
            wxLogTrace( "webview", "No handler specified, trying: %s", handlerPair.first );
            handlerPair.second( message );
        }

        return;
    }

    auto it = m_msgHandlers.find( handler );

    if( it == m_msgHandlers.end() )
    {
        wxLogTrace( "webview", "No handler registered for message: %s", handler );
        return;
    }

    // Call the registered handler with the message
    wxLogTrace( "webview", "Calling handler for message: %s", handler );
    it->second( message );
}


void WEBVIEW_PANEL::OnScriptResult( wxWebViewEvent& aEvt )
{
    if( aEvt.IsError() )
        wxLogTrace( "webview", "Async script execution failed: %s", aEvt.GetString() );
}

void WEBVIEW_PANEL::OnError( wxWebViewEvent& aEvt )
{
    m_loadError = true;
    wxLogTrace( "webview", "WebView error: %s", aEvt.GetString() );
}
