/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2020 Ian McInerney <Ian.S.McInerney at ieee.org>
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

#include <windows.h>
#include <commctrl.h>   // SetWindowSubclass / DefSubclassProc (FlattenNativeBorder)

#include <algorithm>

#include <kiplatform/ui.h>

#if defined( _MSC_VER )
#pragma comment( lib, "comctl32.lib" )
#endif

#include <wx/cursor.h>
#include <wx/dialog.h>
#include <wx/nonownedwnd.h>
#include <wx/window.h>
#include <wx/msw/registry.h>


bool KIPLATFORM::UI::IsDarkTheme()
{
#if wxCHECK_VERSION( 3, 3, 0 )
    wxSystemAppearance appearance = wxSystemSettings::GetAppearance();
    return appearance.IsDark();
#else
    wxColour bg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOW );

    // Weighted W3C formula
    double brightness = ( bg.Red() / 255.0 ) * 0.299 +
        ( bg.Green() / 255.0 ) * 0.587 +
        ( bg.Blue() / 255.0 ) * 0.117;

    return brightness < 0.5;
#endif
}


wxColour KIPLATFORM::UI::GetDialogBGColour()
{
    return wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );
}


void KIPLATFORM::UI::GetInfoBarColours( wxColour& aFGColour, wxColour& aBGColour )
{
    aBGColour = wxSystemSettings::GetColour( wxSYS_COLOUR_INFOBK );
    aFGColour = wxSystemSettings::GetColour( wxSYS_COLOUR_INFOTEXT );
}


void KIPLATFORM::UI::ForceFocus( wxWindow* aWindow )
{
    aWindow->SetFocus();
}


bool KIPLATFORM::UI::IsWindowActive( wxWindow* aWindow )
{
    if(! aWindow )
    {
	    return false;
    }

    return ( aWindow->GetHWND() == GetForegroundWindow() );
}


void KIPLATFORM::UI::EnsureVisible( wxWindow* aWindow )
{
    // Not needed on this platform
}


void KIPLATFORM::UI::ReparentModal( wxNonOwnedWindow* aWindow )
{
    // Not needed on this platform
}


void KIPLATFORM::UI::ReparentWindow( wxNonOwnedWindow* aWindow, wxTopLevelWindow* aParent )
{
    // Not needed on this platform (used only on macOS for child window ordering)
}


void KIPLATFORM::UI::FixupCancelButtonCmdKeyCollision( wxWindow *aWindow )
{
    // Not needed on this platform
}


bool KIPLATFORM::UI::IsStockCursorOk( wxStockCursor aCursor )
{
    switch( aCursor )
    {
    case wxCURSOR_BULLSEYE:
    case wxCURSOR_HAND:
    case wxCURSOR_ARROW:
        return true;
    default:
        return false;
    }
}


void KIPLATFORM::UI::LargeChoiceBoxHack( wxChoice* aChoice )
{
    // Not implemented
}


void KIPLATFORM::UI::EllipsizeChoiceBox( wxChoice* aChoice )
{
    // Not implemented
}


double KIPLATFORM::UI::GetPixelScaleFactor( const wxWindow* aWindow )
{
    return aWindow->GetContentScaleFactor();
}


double KIPLATFORM::UI::GetContentScaleFactor( const wxWindow* aWindow )
{
    return aWindow->GetDPIScaleFactor();
}


wxSize KIPLATFORM::UI::GetUnobscuredSize( const wxWindow* aWindow )
{
    return aWindow->GetClientSize();
}


void KIPLATFORM::UI::SetOverlayScrolling( const wxWindow* aWindow, bool overlay )
{
    // Not implemented
}


bool KIPLATFORM::UI::AllowIconsInMenus()
{
    return true;
}


wxPoint KIPLATFORM::UI::GetMousePosition()
{
    return wxGetMousePosition();
}


bool KIPLATFORM::UI::WarpPointer( wxWindow* aWindow, int aX, int aY )
{
    aWindow->WarpPointer( aX, aY );
    return true;
}


void KIPLATFORM::UI::ImmControl( wxWindow* aWindow, bool aEnable )
{
    if ( !aEnable )
    {
        ImmAssociateContext( aWindow->GetHWND(), NULL );
    }
    else
    {
        ImmAssociateContextEx( aWindow->GetHWND(), 0, IACE_DEFAULT );
    }
}


void KIPLATFORM::UI::ImeNotifyCancelComposition( wxWindow* aWindow )
{
    const HIMC himc = ImmGetContext( aWindow->GetHWND() );
    ImmNotifyIME( himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0 );
    ImmReleaseContext( aWindow->GetHWND(), himc );
}


bool KIPLATFORM::UI::InfiniteDragPrepareWindow( wxWindow* aWindow )
{
    return true;
}


void KIPLATFORM::UI::InfiniteDragReleaseWindow()
{
    // Not needed on this platform
}


void KIPLATFORM::UI::SetFloatLevel( wxWindow* aWindow )
{
}

void KIPLATFORM::UI::ReleaseChildWindow( wxNonOwnedWindow* aWindow )
{
    // Not needed on this platform
}


void KIPLATFORM::UI::AllowNetworkFileSystems( wxDialog* aDialog )
{
    // Not needed on Windows - file dialogs show network filesystems by default
}


void KIPLATFORM::UI::AddDropShadow( wxWindow* aWindow )
{
    if( !aWindow )
        return;

    HWND hwnd = static_cast<HWND>( aWindow->GetHWND() );

    if( !hwnd )
        return;

    // Give the borderless NEMI popup the same soft drop shadow the OS gives native menus, so
    // it reads as floating above the window instead of painted flat onto it.  CS_DROPSHADOW is
    // a window-class style: setting it affects windows created from this popup's class, which
    // is exactly what we want (every NEMI popup of this class gets the shadow).
    ULONG_PTR style = ::GetClassLongPtr( hwnd, GCL_STYLE );

    if( !( style & CS_DROPSHADOW ) )
        ::SetClassLongPtr( hwnd, GCL_STYLE, style | CS_DROPSHADOW );
}


namespace
{
struct FLAT_BORDER_COLOURS
{
    COLORREF edge;
    COLORREF inner;
};

const UINT_PTR FLAT_BORDER_SUBCLASS_ID = 0xA271;    // arbitrary, unique within the app

LRESULT CALLBACK flatBorderSubclassProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
    LRESULT ret = ::DefSubclassProc( hWnd, uMsg, wParam, lParam );

    if( uMsg == WM_NCDESTROY )
    {
        ::RemoveWindowSubclass( hWnd, flatBorderSubclassProc, uIdSubclass );
        delete reinterpret_cast<FLAT_BORDER_COLOURS*>( dwRefData );
        return ret;
    }

    if( uMsg != WM_PAINT && uMsg != WM_NCPAINT )
        return ret;

    const FLAT_BORDER_COLOURS* colours = reinterpret_cast<FLAT_BORDER_COLOURS*>( dwRefData );

    RECT wr, cr;
    ::GetWindowRect( hWnd, &wr );
    ::GetClientRect( hWnd, &cr );

    // Native border thickness per side.  0 means the control paints its border inside the
    // client area (themed combo boxes) — still one ring to overdraw.
    int thickness = ( ( wr.right - wr.left ) - ( cr.right - cr.left ) ) / 2;
    thickness = std::clamp( thickness, 1, 3 );

    RECT rc = { 0, 0, wr.right - wr.left, wr.bottom - wr.top };

    if( HDC hdc = ::GetWindowDC( hWnd ) )
    {
        HBRUSH edgeBrush = ::CreateSolidBrush( colours->edge );
        ::FrameRect( hdc, &rc, edgeBrush );
        ::DeleteObject( edgeBrush );

        if( thickness > 1 )
        {
            HBRUSH innerBrush = ::CreateSolidBrush( colours->inner );

            for( int ring = 1; ring < thickness; ++ring )
            {
                ::InflateRect( &rc, -1, -1 );
                ::FrameRect( hdc, &rc, innerBrush );
            }

            ::DeleteObject( innerBrush );
        }

        ::ReleaseDC( hWnd, hdc );
    }

    return ret;
}
} // namespace


void KIPLATFORM::UI::FlattenNativeBorder( wxWindow* aWindow, const wxColour& aEdgeColour,
                                          const wxColour& aInnerColour )
{
    if( !aWindow )
        return;

    HWND hwnd = static_cast<HWND>( aWindow->GetHWND() );

    if( !hwnd )
        return;

    // Re-flattening the same control just updates the colours: free the previous colour block
    // first, since SetWindowSubclass overwrites the reference data without any callback.
    DWORD_PTR prevData = 0;

    if( ::GetWindowSubclass( hwnd, flatBorderSubclassProc, FLAT_BORDER_SUBCLASS_ID, &prevData )
        && prevData )
    {
        delete reinterpret_cast<FLAT_BORDER_COLOURS*>( prevData );
    }

    FLAT_BORDER_COLOURS* colours = new FLAT_BORDER_COLOURS{
        RGB( aEdgeColour.Red(), aEdgeColour.Green(), aEdgeColour.Blue() ),
        RGB( aInnerColour.Red(), aInnerColour.Green(), aInnerColour.Blue() ) };

    if( !::SetWindowSubclass( hwnd, flatBorderSubclassProc, FLAT_BORDER_SUBCLASS_ID,
                              reinterpret_cast<DWORD_PTR>( colours ) ) )
    {
        delete colours;
        return;
    }

    // Repaint frame + client now so the flat border shows without waiting for the next paint.
    ::RedrawWindow( hwnd, nullptr, nullptr,
                    RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN );
}
