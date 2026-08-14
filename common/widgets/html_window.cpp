/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2021 Mikołaj Wielgus <wielgusmikolaj@gmail.com>
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <wx/menu.h>
#include <wx/clipbrd.h>
#include <wx/log.h>
#include <wx/settings.h>
#include <widgets/html_window.h>
#include <advanced_config.h>


HTML_WINDOW::HTML_WINDOW( wxWindow* aParent, wxWindowID aId, const wxPoint& aPos,
                          const wxSize& aSize, long aStyle, const wxString& aName ) :
        wxHtmlWindow( aParent, aId, aPos, aSize, aStyle, aName )
{
    // NEMI brand: render HTML (reports, help, message boxes) in the app-wide UI font instead
    // of wxHtmlWindow's OS-default font table.  Normal text = the UI face (Space Grotesk),
    // <tt>/<code> = the mono face (IBM Plex Mono), sized to AnvilUiFontPt.  A plain SetFont()
    // does NOT rebuild the HTML parser's font table, so SetStandardFonts() is required.
    {
        const ADVANCED_CFG& acfg = ADVANCED_CFG::GetCfg();
        int      htmlPt   = ( acfg.m_AnvilUiFontPt > 0.0 )
                                    ? static_cast<int>( acfg.m_AnvilUiFontPt + 0.5 )
                                    : GetFont().GetPointSize();
        wxString normFace = acfg.m_AnvilUiFontFace;    // empty -> wx keeps its default face
        wxString fixedFace = acfg.m_AnvilMonoFontFace;

        SetStandardFonts( htmlPt, normFace, fixedFace );
    }

    Bind( wxEVT_SYS_COLOUR_CHANGED,
          wxSysColourChangedEventHandler( HTML_WINDOW::onThemeChanged ), this );

    Connect( wxEVT_RIGHT_UP, wxMouseEventHandler( HTML_WINDOW::onRightClick ), nullptr, this );
    Connect( wxEVT_COMMAND_MENU_SELECTED, wxMenuEventHandler( HTML_WINDOW::onMenuEvent ), nullptr, this );
}


HTML_WINDOW::~HTML_WINDOW()
{
	// Disconnect Events
	Disconnect( wxEVT_RIGHT_UP, wxMouseEventHandler( HTML_WINDOW::onRightClick ), nullptr, this );
    Disconnect( wxEVT_COMMAND_MENU_SELECTED, wxMenuEventHandler( HTML_WINDOW::onMenuEvent ), nullptr, this );
}


bool HTML_WINDOW::SetPage( const wxString& aSource )
{
    m_pageSource = aSource;

    wxColour fgColor   = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
    wxColour bgColor   = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );
    wxColour linkColor = wxSystemSettings::GetColour( wxSYS_COLOUR_HOTLIGHT );

    wxString html = wxString::Format( wxT( "<html>\n<body text='%s' bgcolor='%s' link='%s'>\n" ),
                                      fgColor.GetAsString( wxC2S_HTML_SYNTAX ),
                                      bgColor.GetAsString( wxC2S_HTML_SYNTAX ),
                                      linkColor.GetAsString( wxC2S_HTML_SYNTAX ) );
    html.Append( aSource );
    html.Append( wxT( "\n</body>\n</html>" ) );

    return wxHtmlWindow::SetPage( html );
}


bool HTML_WINDOW::AppendToPage( const wxString& aSource )
{
    return SetPage( m_pageSource + aSource );
}


void HTML_WINDOW::ThemeChanged()
{
    SetPage( m_pageSource );
}


bool HTML_WINDOW::ScrollToAnchor( const wxString& aAnchor )
{
    // Check if we have content loaded
    if( !GetInternalRepresentation() )
        return false;

    // Try to scroll to the anchor
    bool result = wxHtmlWindow::ScrollToAnchor( aAnchor );
    return result;
}


void HTML_WINDOW::onThemeChanged( wxSysColourChangedEvent &aEvent )
{
    ThemeChanged();
}


void HTML_WINDOW::onRightClick( wxMouseEvent& event )
{
    wxMenu popup;
    popup.Append( wxID_COPY, _( "Copy" ) );
    popup.Append( wxID_SELECTALL, _( "Select All" ) );
    PopupMenu( &popup );
}


void HTML_WINDOW::onMenuEvent( wxMenuEvent& event )
{
    if( event.GetId() == wxID_COPY )
    {
        wxLogNull doNotLog; // disable logging of failed clipboard actions

        if( wxTheClipboard->Open() )
        {
            bool primarySelection = wxTheClipboard->IsUsingPrimarySelection();
            wxTheClipboard->UsePrimarySelection( false );   // required to use the main clipboard
            wxTheClipboard->SetData( new wxTextDataObject( SelectionToText() ) );
            wxTheClipboard->Flush(); // Allow data to be available after closing KiCad
            wxTheClipboard->Close();
            wxTheClipboard->UsePrimarySelection( primarySelection );
        }
    }
    else if( event.GetId() == wxID_SELECTALL )
    {
        SelectAll();
    }
}


