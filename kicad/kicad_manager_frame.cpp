/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2017 Jean-Pierre Charras, jp.charras at wanadoo.fr
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include "kicad_id.h"
#include "pcm.h"
#include "pgm_kicad.h"
#include "project_tree_pane.h"
#include "local_history_pane.h"
#include "widgets/bitmap_button.h"

#include <advanced_config.h>
#include <background_jobs_monitor.h>
#include <bitmaps.h>
#include <build_version.h>
#include <confirm.h>
#include <dialogs/panel_kicad_launcher.h>
#include <dialogs/panel_jobset.h>
#include <dialogs/dialog_edit_cfg.h>
#include <local_history.h>
#include <wx/msgdlg.h>
#include <eda_base_frame.h>
#include <executable_names.h>
#include <file_history.h>
#include <local_history.h>
#include <policy_keys.h>
#include <gestfich.h>
#include <kiplatform/app.h>
#include <kidialog.h>
#include <json_common.h>
#include <kiplatform/environment.h>
#include <kiplatform/ui.h>
#include <kiplatform/policy.h>
#include <build_version.h>
#include <kiway.h>
#include <kiway_mail.h>
#include <ki_exception.h>           // IO_ERROR (editor pre-warm)
#include <frame_type.h>             // FRAME_T values warmed by the editor pre-warm
#include <wx/timer.h>
#include <wx/graphics.h>           // wxGraphicsContext for the title-bar AI logo mark
#include <wx/mstream.h>            // wxMemoryInputStream to decode the embedded AI logo PNG
#include "anvil_ai_logo_png.h"     // embedded Anvil "A" logo bytes for the title-bar AI icon
#include <launch_ext.h>
#include <lockfile.h>
#include <notifications_manager.h>
#include <reporter.h>
#include <project/project_local_settings.h>
#include <sch_file_versions.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <tool/action_manager.h>
#include <tool/action_toolbar.h>
#include <tool/common_control.h>
#include <tool/tool_dispatcher.h>
#include <tool/tool_manager.h>
#include <tools/kicad_manager_actions.h>
#include <tools/kicad_manager_control.h>
#include <toolbars_kicad_manager.h>
#include <wildcards_and_files_ext.h>
#include <widgets/app_progress_dialog.h>
#include <widgets/kistatusbar.h>
#include <wx/ffile.h>
#include <wx/filedlg.h>
#include <wx/dnd.h>
#include <wx/process.h>
#include <wx/snglinst.h>
#include <atomic>
#include <vector>
#include <functional>
#include <update_manager.h>
#include <jobs/jobset.h>
#include <widgets/wx_aui_art_providers.h>
#include <widgets/wx_aui_utils.h>          // SetAuiPaneSize (common AI panel width restore)
#include <widgets/webview_panel.h>         // shell-owned common AI chat panel
#include <widgets/ai_ipc_client.h>         // shell-side backend command channel (open_project)
#include <envil_ai/envil_ai_agent.h>       // native Claude agent driving the shell AI panel
#include <envil_ai/envil_ai_tool_server.h> // MCP tool socket (external Claude clients)
#include <nlohmann/json.hpp>               // parse the open_project IPC payload
#include <paths.h>                         // PATHS::GetStockDataPath (locate chat.html)
#include <wx/stdpaths.h>
#include <wx/file.h>                       // read ipc_port.txt for the AI IPC client
#include <wx/utils.h>                      // wxGetEnv / wxGetHomeDir (IPC port discovery)
#include <wx/filename.h>
#include <wx/webview.h>                    // wxWebView / wxEVT_WEBVIEW_LOADED
#include <wx/datetime.h>                   // wxDateTime::Now (chat.html cache-buster)
#include <wx/panel.h>
#include <wx/statbmp.h>
#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <windowsx.h>     // GET_X_LPARAM / GET_Y_LPARAM
// windows.h defines these as macros (e.g. IsMaximized -> IsZoomed) that clobber the
// wxTopLevelWindow methods of the same name. Undefine them so the wx calls compile.
#undef IsMaximized
#undef IsMinimized
#undef IsRestored
#endif

#include <../pcbnew/pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>   // for SEXPR_BOARD_FILE_VERSION def


#ifdef __WXMAC__
#include <MacTypes.h>
#include <ApplicationServices/ApplicationServices.h>
#endif

#include "kicad_manager_frame.h"
#include "settings/kicad_settings.h"

#include <project/project_file.h>


#define EDITORS_CAPTION _( "Editors" )
#define PROJECT_FILES_CAPTION _( "Project Files" )

#define ID_INIT_WATCHED_PATHS 52913

#define SEP()   wxFileName::GetPathSeparator()


// Flat, owner-painted title-bar menu button.  A native wxButton with a custom background goes
// owner-drawn on MSW and renders its label in the system button-text colour on hover — which is
// dark and invisible on the dark purple bar.  We paint it ourselves instead: parent-coloured
// background normally, accent purple on hover, and an always-white label in both states.
namespace
{
// Windows 11 renders native popup menus itself and ignores every colour lever (wxDarkModeSettings,
// wxSYS_COLOUR_MENU, SetMenuInfo, per-item owner-draw) — you can tint the body but never the icon
// gutter without losing the icons.  So we render the dropdown ourselves: a fully-purple popup with
// icons, accelerators, separators and submenus, dispatching the real command through
// wxMenu::SendEvent() so behaviour is unchanged.
class ANVIL_POPUP_MENU : public wxPopupTransientWindow
{
public:
    ANVIL_POPUP_MENU( wxWindow* aParent, wxMenu* aMenu, ANVIL_POPUP_MENU* aParentPopup = nullptr ) :
            wxPopupTransientWindow( aParent, wxBORDER_NONE ),
            m_menu( aMenu ),
            m_parentPopup( aParentPopup )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        // Dropdown rows follow the app-wide UI font size (AnvilUiFontPt) — the popups belong to
        // the rest of the UI, not the top menu BAR (which keeps its own larger size).  Set before
        // computeLayout() so row width/height are measured at the new size.
        const double uiPt = ADVANCED_CFG::GetCfg().m_AnvilUiFontPt;

        if( uiPt > 0.0 )
        {
            wxFont rowFont = GetFont();
            rowFont.SetFractionalPointSize( uiPt );
            SetFont( rowFont );
        }

        buildRows();
        computeLayout();

        Bind( wxEVT_PAINT, &ANVIL_POPUP_MENU::onPaint, this );
        Bind( wxEVT_MOTION, &ANVIL_POPUP_MENU::onMotion, this );
        Bind( wxEVT_LEFT_UP, &ANVIL_POPUP_MENU::onClick, this );
        Bind( wxEVT_LEAVE_WINDOW,
              [this]( wxMouseEvent& ) { if( !m_child ) { m_hover = -1; Refresh(); } } );
    }

    /// Pop the menu so its top-left sits at screen point @p aScreenPos.
    void PopupAt( const wxPoint& aScreenPos )
    {
        Move( aScreenPos );
        Popup();
    }

    void OnDismiss() override
    {
        // The framework calls this on the deepest (capturing) popup when the user clicks outside.
        // Hide + destroy the whole chain from the root down.
        ANVIL_POPUP_MENU* root = this;

        while( root->m_parentPopup )
            root = root->m_parentPopup;

        root->dismissChain();
    }

private:
    struct ROW
    {
        wxMenuItem* item = nullptr;
        bool        separator = false;
        bool        submenu = false;
        bool        enabled = true;
        bool        checked = false;
        wxString    label;
        wxString    accel;
        int         top = 0;
        int         height = 0;
    };

    void buildRows()
    {
        for( wxMenuItem* it : m_menu->GetMenuItems() )
        {
            ROW r;
            r.item = it;

            if( it->IsSeparator() )
            {
                r.separator = true;
            }
            else
            {
                wxString full = it->GetItemLabel();
                r.label = full.BeforeFirst( '\t' );
                wxString accel = full.AfterFirst( '\t' );
                r.accel   = ( accel == full ) ? wxString() : accel;
                r.label.Replace( wxS( "&" ), wxEmptyString );
                r.submenu = it->GetSubMenu() != nullptr;
                r.enabled = it->IsEnabled();
                r.checked = it->IsCheckable() && it->IsChecked();
            }

            m_rows.push_back( r );
        }
    }

    void computeLayout()
    {
        m_iconW  = FromDIP( 26 );
        m_padL   = FromDIP( 8 );
        m_padR   = FromDIP( 14 );
        m_gap    = FromDIP( 28 );
        m_arrowW = FromDIP( 14 );
        m_rowH   = GetTextExtent( wxS( "Ag" ) ).y + FromDIP( 8 );
        m_sepH   = FromDIP( 9 );

        int maxLabel = 0, maxAccel = 0;

        for( const ROW& r : m_rows )
        {
            if( r.separator )
                continue;

            maxLabel = wxMax( maxLabel, GetTextExtent( r.label ).x );
            maxAccel = wxMax( maxAccel, GetTextExtent( r.accel ).x );
        }

        int width = m_padL + m_iconW + maxLabel + m_gap + maxAccel + m_arrowW + m_padR;
        int y = FromDIP( 4 );

        for( ROW& r : m_rows )
        {
            r.top    = y;
            r.height = r.separator ? m_sepH : m_rowH;
            y += r.height;
        }

        SetSize( wxSize( width, y + FromDIP( 4 ) ) );
    }

    int rowAt( int aY ) const
    {
        for( size_t i = 0; i < m_rows.size(); ++i )
            if( aY >= m_rows[i].top && aY < m_rows[i].top + m_rows[i].height )
                return static_cast<int>( i );

        return -1;
    }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        const wxColour bg     ( 76, 74, 120 );    // #4C4A78
        const wxColour border ( 94, 78, 146 );
        const wxColour hover  ( 109, 99, 230 );
        const wxColour text   ( 248, 250, 252 );
        const wxColour dim    ( 168, 164, 200 );
        const wxColour accelC ( 196, 190, 221 );

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( bg ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        dc.SetPen( wxPen( border ) );
        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        for( size_t i = 0; i < m_rows.size(); ++i )
        {
            const ROW& r = m_rows[i];

            if( r.separator )
            {
                dc.SetPen( wxPen( border ) );
                int ly = r.top + r.height / 2;
                dc.DrawLine( m_padL + m_iconW, ly, sz.x - m_padR, ly );
                continue;
            }

            if( static_cast<int>( i ) == m_hover && r.enabled )
            {
                dc.SetPen( *wxTRANSPARENT_PEN );
                dc.SetBrush( wxBrush( hover ) );
                dc.DrawRectangle( FromDIP( 3 ), r.top, sz.x - FromDIP( 6 ), r.height );
            }

            // Icon (or checkmark for a checked checkable item).
            if( r.item->GetBitmapBundle().IsOk() )
            {
                wxBitmap bmp = r.item->GetBitmapBundle().GetBitmap(
                        wxSize( FromDIP( 16 ), FromDIP( 16 ) ) );

                if( bmp.IsOk() )
                    dc.DrawBitmap( bmp, m_padL + ( m_iconW - bmp.GetWidth() ) / 2 - FromDIP( 2 ),
                                   r.top + ( r.height - bmp.GetHeight() ) / 2, true );
            }
            else if( r.checked )
            {
                dc.SetPen( wxPen( text, FromDIP( 2 ) ) );
                int cx = m_padL + FromDIP( 4 ), cy = r.top + r.height / 2;
                dc.DrawLine( cx, cy, cx + FromDIP( 3 ), cy + FromDIP( 3 ) );
                dc.DrawLine( cx + FromDIP( 3 ), cy + FromDIP( 3 ), cx + FromDIP( 9 ),
                             cy - FromDIP( 4 ) );
            }

            dc.SetFont( GetFont() );
            dc.SetTextForeground( r.enabled ? text : dim );

            int ty = r.top + ( r.height - GetTextExtent( r.label ).y ) / 2;
            dc.DrawText( r.label, m_padL + m_iconW, ty );

            if( !r.accel.IsEmpty() )
            {
                dc.SetTextForeground( r.enabled ? accelC : dim );
                int aw = GetTextExtent( r.accel ).x;
                dc.DrawText( r.accel, sz.x - m_padR - m_arrowW - aw, ty );
            }

            if( r.submenu )
            {
                // Right-pointing chevron.
                dc.SetPen( wxPen( r.enabled ? text : dim, FromDIP( 1 ) ) );
                int ax = sz.x - m_padR - FromDIP( 8 ), ay = r.top + r.height / 2;
                dc.DrawLine( ax, ay - FromDIP( 4 ), ax + FromDIP( 4 ), ay );
                dc.DrawLine( ax + FromDIP( 4 ), ay, ax, ay + FromDIP( 4 ) );
            }
        }
    }

    void onMotion( wxMouseEvent& aEvent )
    {
        int row = rowAt( aEvent.GetY() );

        if( row != m_hover )
        {
            m_hover = row;
            Refresh();

            // Open / switch submenu on hover; close it when moving onto a normal row.
            if( row >= 0 && m_rows[row].submenu && m_rows[row].enabled )
                openSubmenu( row );
            else if( row >= 0 && !m_rows[row].separator )
                closeChild();
        }
    }

    void onClick( wxMouseEvent& aEvent )
    {
        int row = rowAt( aEvent.GetY() );

        if( row < 0 || m_rows[row].separator || !m_rows[row].enabled )
            return;

        if( m_rows[row].submenu )
        {
            openSubmenu( row );
            return;
        }

        int id = m_rows[row].item->GetId();
        wxMenu* menu = m_menu;

        DismissChain();
        menu->SendEvent( id );    // fire the real command exactly as a native click would
    }

    void openSubmenu( int aRow )
    {
        if( m_child && m_childRow == aRow )
            return;

        closeChild();
        m_childRow = aRow;

        m_child = new ANVIL_POPUP_MENU( GetParent(), m_rows[aRow].item->GetSubMenu(), this );
        wxPoint pt = ClientToScreen( wxPoint( GetClientSize().x - FromDIP( 2 ),
                                              m_rows[aRow].top ) );
        m_child->PopupAt( pt );
    }

    /// Programmatically close the open submenu (e.g. when hovering onto another row).
    void closeChild()
    {
        if( m_child )
        {
            m_child->dismissChain();   // hides + destroys it and any of its descendants
            m_child = nullptr;
            m_childRow = -1;
        }
    }

    /// Hide this popup and all of its descendants, destroying them after the event unwinds.
    void dismissChain()
    {
        if( m_dismissed )
            return;

        m_dismissed = true;

        if( m_child )
        {
            m_child->dismissChain();
            m_child = nullptr;
        }

        Dismiss();
        CallAfter( [this]() { Destroy(); } );
    }

    /// Hide this popup and every ancestor (used when a leaf item is chosen).
    void DismissChain()
    {
        ANVIL_POPUP_MENU* root = this;

        while( root->m_parentPopup )
            root = root->m_parentPopup;

        root->dismissChain();
    }

    wxMenu*           m_menu;
    ANVIL_POPUP_MENU* m_parentPopup;
    ANVIL_POPUP_MENU* m_child = nullptr;
    int               m_childRow = -1;
    bool              m_dismissed = false;
    std::vector<ROW>  m_rows;
    int               m_hover = -1;
    int               m_iconW = 0, m_padL = 0, m_padR = 0, m_gap = 0, m_arrowW = 0;
    int               m_rowH = 0, m_sepH = 0;
};


class TITLEBAR_MENU_BUTTON : public wxWindow
{
public:
    TITLEBAR_MENU_BUTTON( wxWindow* aParent, const wxString& aLabel, wxMenu* aMenu ) :
            wxWindow( aParent, wxID_ANY ),
            m_label( aLabel ),
            m_menu( aMenu )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        // Menu-bar labels at 11 pt (keep the system face, bump only the size).
        wxFont labelFont = GetFont();
        labelFont.SetPointSize( 11 );
        SetFont( labelFont );

        wxSize ext = GetTextExtent( m_label.IsEmpty() ? wxString( wxS( "M" ) ) : m_label );
        SetMinSize( wxSize( ext.x + FromDIP( 16 ), FromDIP( 24 ) ) );

        Bind( wxEVT_PAINT, &TITLEBAR_MENU_BUTTON::onPaint, this );
        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true;  Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& ) { m_hover = false; Refresh(); } );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& )
              {
                  if( m_menu )
                  {
                      // Custom fully-purple popup (native menus can't be themed on Win11).
                      ANVIL_POPUP_MENU* popup = new ANVIL_POPUP_MENU( this, m_menu );
                      popup->PopupAt( ClientToScreen( wxPoint( 0, GetSize().GetHeight() ) ) );
                  }
              } );
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );

        const wxColour normalBg = GetParent()->GetBackgroundColour();
        const wxColour hoverBg( 60, 52, 92 );   // subtle hover (refined, not a solid block)

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( m_hover ? hoverBg : normalBg ) );
        dc.DrawRectangle( GetClientRect() );

        dc.SetFont( GetFont() );
        dc.SetTextForeground( *wxWHITE );

        wxSize sz  = GetClientSize();
        wxSize ext = dc.GetTextExtent( m_label );
        dc.DrawText( m_label, ( sz.x - ext.x ) / 2, ( sz.y - ext.y ) / 2 );
    }

    wxString m_label;
    wxMenu*  m_menu;
    bool     m_hover = false;
};


// Flat, owner-painted title-bar glyph button (Segoe MDL2 Assets) for the layout toggles and the
// window controls.  Same reason as TITLEBAR_MENU_BUTTON: a native button darkens its glyph on
// hover.  Here the glyph is always drawn light (or dimmed when inactive, for the toggles) over a
// per-button hover colour (accent purple, or red for the close button), and a left click emits
// wxEVT_BUTTON so the existing handlers keep working.
class TITLEBAR_GLYPH_BUTTON : public wxWindow
{
public:
    TITLEBAR_GLYPH_BUTTON( wxWindow* aParent, const wxString& aGlyph, int aWidth,
                           const wxColour& aHoverBg, bool aIndicator = false ) :
            wxWindow( aParent, wxID_ANY, wxDefaultPosition, wxDefaultSize ),
            m_glyph( aGlyph ),
            m_hoverBg( aHoverBg ),
            m_indicator( aIndicator )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        wxFont glyphFont( wxFontInfo( 11 ).FaceName( wxT( "Segoe MDL2 Assets" ) ) );

        if( glyphFont.IsOk() )
            SetFont( glyphFont );

        SetMinSize( wxSize( aWidth, FromDIP( 24 ) ) );

        Bind( wxEVT_PAINT, &TITLEBAR_GLYPH_BUTTON::onPaint, this );
        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true;  Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& ) { m_hover = false; Refresh(); } );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& )
              {
                  wxCommandEvent evt( wxEVT_BUTTON, GetId() );
                  evt.SetEventObject( this );
                  ProcessWindowEvent( evt );
              } );
    }

    void SetGlyph( const wxString& aGlyph ) { m_glyph = aGlyph; Refresh(); }

    /// Toggles: bright glyph + accent indicator bar while the pane is shown, dimmed while hidden.
    void SetActiveGlyph( bool aActive ) { m_active = aActive; Refresh(); }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        // Full-height hover fill (window-control / VS Code style).
        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( m_hover ? m_hoverBg : GetParent()->GetBackgroundColour() ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        dc.SetFont( GetFont() );
        dc.SetTextForeground( m_active ? *wxWHITE : wxColour( 150, 145, 175 ) );

        wxSize ext = dc.GetTextExtent( m_glyph );
        dc.DrawText( m_glyph, ( sz.x - ext.x ) / 2, ( sz.y - ext.y ) / 2 );

        // VS Code-style active indicator: a short accent bar along the bottom edge while the
        // toggle's pane is shown — turns the flat glyph row into a stateful, polished control.
        if( m_indicator && m_active )
        {
            const int barW = sz.x / 2;
            const int barH = FromDIP( 2 );
            dc.SetBrush( wxBrush( wxColour( 139, 92, 246 ) ) );
            dc.DrawRectangle( ( sz.x - barW ) / 2, sz.y - barH, barW, barH );
        }
    }

    wxString m_glyph;
    wxColour m_hoverBg;
    bool     m_hover     = false;
    bool     m_active    = true;
    bool     m_indicator = false;
};


// VS Code / Cursor-style "panel toggle" icon, drawn as a little editor-window diagram with one
// docked region (left / bottom / right).  The region fills in (accent purple) when that pane is
// open and is empty when hidden — so the control shows its state the way a modern title bar does,
// instead of a plain glyph.  Crisp at any DPI because it's vector-drawn.
class TITLEBAR_PANEL_BUTTON : public wxWindow
{
public:
    enum SIDE { LEFT, BOTTOM, RIGHT };

    TITLEBAR_PANEL_BUTTON( wxWindow* aParent, SIDE aSide, int aWidth, const wxColour& aHoverBg ) :
            wxWindow( aParent, wxID_ANY ),
            m_side( aSide ),
            m_hoverBg( aHoverBg )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( aWidth, FromDIP( 24 ) ) );

        Bind( wxEVT_PAINT, &TITLEBAR_PANEL_BUTTON::onPaint, this );
        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true;  Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& ) { m_hover = false; Refresh(); } );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& )
              {
                  wxCommandEvent evt( wxEVT_BUTTON, GetId() );
                  evt.SetEventObject( this );
                  ProcessWindowEvent( evt );
              } );
    }

    /// Matches TITLEBAR_GLYPH_BUTTON so the same refresh closures drive either control.
    void SetActiveGlyph( bool aActive ) { m_active = aActive; Refresh(); }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( m_hover ? m_hoverBg : GetParent()->GetBackgroundColour() ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        const int  side = FromDIP( 15 );
        const int  ox   = ( sz.x - side ) / 2;
        const int  oy   = ( sz.y - side ) / 2;
        const int  t    = side / 3;                          // docked-region thickness
        const int  r    = FromDIP( 2 );
        const wxColour fg = m_active ? wxColour( 255, 255, 255 ) : wxColour( 150, 145, 180 );

        // Outer editor-window outline.
        dc.SetPen( wxPen( fg, FromDIP( 1 ) ) );
        dc.SetBrush( *wxTRANSPARENT_BRUSH );
        dc.DrawRoundedRectangle( ox, oy, side, side, r );

        // Divider between the editor and the docked region.
        wxRect region;

        switch( m_side )
        {
        case LEFT:   region = wxRect( ox, oy, t, side );
                     dc.DrawLine( ox + t, oy, ox + t, oy + side );        break;
        case RIGHT:  region = wxRect( ox + side - t, oy, t, side );
                     dc.DrawLine( ox + side - t, oy, ox + side - t, oy + side ); break;
        case BOTTOM: region = wxRect( ox, oy + side - t, side, t );
                     dc.DrawLine( ox, oy + side - t, ox + side, oy + side - t ); break;
        }

        // Fill the docked region (accent purple) when the pane is open.
        if( m_active )
        {
            dc.SetPen( *wxTRANSPARENT_PEN );
            dc.SetBrush( wxBrush( wxColour( 139, 92, 246 ) ) );
            dc.DrawRectangle( region.Deflate( FromDIP( 1 ) ) );
        }
    }

    SIDE     m_side;
    wxColour m_hoverBg;
    bool     m_hover  = false;
    bool     m_active = true;
};


// Anvil AI logo mark — the purple "A" product logo (embedded PNG, see anvil_ai_logo_png.h) drawn
// in the title bar as the AI-panel toggle, the way VS Code / Cursor put their product icon in the
// header.  Full strength while the AI panel is open, dimmed (semi-transparent) while it is closed.
// Decoded once and cached; scaled with high-quality interpolation so it stays crisp at any DPI.
class TITLEBAR_AI_BUTTON : public wxWindow
{
public:
    TITLEBAR_AI_BUTTON( wxWindow* aParent, int aWidth, const wxColour& aHoverBg ) :
            wxWindow( aParent, wxID_ANY ),
            m_hoverBg( aHoverBg )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( aWidth, FromDIP( 24 ) ) );

        Bind( wxEVT_PAINT, &TITLEBAR_AI_BUTTON::onPaint, this );
        Bind( wxEVT_ENTER_WINDOW, [this]( wxMouseEvent& ) { m_hover = true;  Refresh(); } );
        Bind( wxEVT_LEAVE_WINDOW, [this]( wxMouseEvent& ) { m_hover = false; Refresh(); } );
        Bind( wxEVT_LEFT_DOWN,
              [this]( wxMouseEvent& )
              {
                  wxCommandEvent evt( wxEVT_BUTTON, GetId() );
                  evt.SetEventObject( this );
                  ProcessWindowEvent( evt );
              } );
    }

    /// Matches the other title-bar toggles: full mark while the pane is shown, dimmed while hidden.
    void SetActiveGlyph( bool aActive ) { m_active = aActive; Refresh(); }

private:
    /// The embedded "A" logo, decoded once (PNG handler is registered at app start) and cached.
    static const wxImage& logoImage()
    {
        static wxImage s_img = []() -> wxImage
        {
            wxMemoryInputStream stream( anvil_ai_logo_png, anvil_ai_logo_png_len );
            wxImage             img( stream, wxBITMAP_TYPE_PNG );
            return img;
        }();

        return s_img;
    }

    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize          sz = GetClientSize();

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( m_hover ? m_hoverBg : GetParent()->GetBackgroundColour() ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        const wxImage& logo = logoImage();

        if( !logo.IsOk() )
            return;

        wxGraphicsContext* gc = wxGraphicsContext::Create( dc );

        if( !gc )
            return;

        gc->SetInterpolationQuality( wxINTERPOLATION_BEST );

        const int    target = FromDIP( 26 );             // logo box inside the button
        const double x      = ( sz.x - target ) / 2.0;
        const double y      = ( sz.y - target ) / 2.0;

        // Dim the logo itself slightly while the AI panel is closed; full strength while open —
        // the same active/inactive read as the other title-bar toggles, but kept legible.
        if( !m_active )
            gc->BeginLayer( 0.8 );

        gc->DrawBitmap( wxBitmap( logo ), x, y, target, target );

        if( !m_active )
            gc->EndLayer();

        delete gc;
    }

    wxColour m_hoverBg;
    bool     m_hover  = false;
    bool     m_active = true;
};
} // namespace


// ============================================================================
// KiCad Next: custom single-row title bar (logo + menu + window buttons).
// Hosts the menu as a row of popup buttons so the native OS caption + native
// menu row collapse into one bar. The caption itself is removed by the frame's
// WM_NCCALCSIZE handler (see MSWWindowProc); this panel is just the content.
// ============================================================================
class KICAD_MANAGER_FRAME::TITLEBAR_PANEL : public wxPanel
{
public:
    TITLEBAR_PANEL( KICAD_MANAGER_FRAME* aParent ) :
            wxPanel( aParent, wxID_ANY ),
            m_frame( aParent )
    {
        applyTheme();

        m_sizer = new wxBoxSizer( wxHORIZONTAL );

        // VS Code / Cursor-style title bar: a small 16px app glyph at the far left
        // with tight padding, then the menu.
        m_logo = new wxStaticBitmap( this, wxID_ANY, KiBitmapBundle( BITMAPS::icon_kicad, 16 ) );
        m_sizer->Add( m_logo, 0, wxALIGN_CENTRE_VERTICAL | wxLEFT | wxRIGHT, FromDIP( 6 ) );

        m_menuSizer = new wxBoxSizer( wxHORIZONTAL );
        m_sizer->Add( m_menuSizer, 0, wxEXPAND );   // full-height menu buttons (clean hover)

        m_sizer->AddStretchSpacer( 1 );

        // KiCad Next single-window shell: VS Code / Cursor-style title-bar layout toggles,
        // sitting just left of the window-control buttons.  Each is a vector panel-diagram icon
        // (left/bottom/right docked region) that flips a pane and fills in while it's shown.
        m_sizer->Add( makeLayoutButton( TITLEBAR_PANEL_BUTTON::LEFT,
                                        _( "Toggle Project Explorer" ),
                                        [this]() { m_frame->ToggleProjectExplorer(); },
                                        [this]() { return m_frame->ProjectExplorerShown(); } ),
                      0, wxEXPAND );

        m_sizer->Add( makeLayoutButton( TITLEBAR_PANEL_BUTTON::BOTTOM,
                                        _( "Split Editor" ),
                                        [this]() { m_frame->SplitActiveEditor(); },
                                        []() { return false; } ),
                      0, wxEXPAND );

        // AI Assistant: the Anvil "AI sparkle" logo mark (vector-drawn, see TITLEBAR_AI_BUTTON)
        // instead of the abstract panel-region diagram.  VS Code / Cursor keep their AI toggle in
        // the title bar so the panel is one click away after you close it; this is that icon.  It
        // lights up while the AI panel is open and dims when it is closed.
        m_sizer->Add( makeAiToggle( _( "Toggle AI Assistant" ),
                                    [this]() { m_frame->ToggleAiChat(); },
                                    [this]() { return m_frame->AiChatPanelShown(); } ),
                      0, wxEXPAND );

        m_sizer->AddSpacer( FromDIP( 6 ) );   // gap before the window-control buttons

        // Segoe MDL2 Assets caption glyphs (same as the native Windows / VS Code bar).
        // Subtle hover for minimize/maximize; the conventional red hover for close.
        const wxColour subtleHover( 60, 52, 92 );
        const wxColour closeHover( 232, 17, 35 );
        m_min   = makeWinButton( wxUniChar( 0xE921 ), subtleHover );  // ChromeMinimize
        m_max   = makeWinButton( wxUniChar( 0xE922 ), subtleHover );  // ChromeMaximize
        m_close = makeWinButton( wxUniChar( 0xE8BB ), closeHover );   // ChromeClose
        m_sizer->Add( m_min,   0, wxEXPAND );
        m_sizer->Add( m_max,   0, wxEXPAND );
        m_sizer->Add( m_close, 0, wxEXPAND );

        m_min  ->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->Iconize( true ); } );
        m_max  ->Bind( wxEVT_BUTTON,
                       [this]( wxCommandEvent& )
                       {
                           m_frame->Maximize( !m_frame->IsMaximized() );
                           UpdateMaximizeGlyph();
                       } );
        m_close->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->Close( false ); } );

        UpdateMaximizeGlyph();   // show restore vs maximize for the initial window state

        SetSizer( m_sizer );
        SetMinSize( wxSize( -1, FromDIP( 32 ) ) );   // 32px title bar (pane MinSize/BestSize is the real driver)

        Bind( wxEVT_SYS_COLOUR_CHANGED,
              [this]( wxSysColourChangedEvent& e ) { applyTheme(); Refresh(); e.Skip(); } );
    }

    ~TITLEBAR_PANEL() override
    {
        for( wxMenu* m : m_ownedMenus )
            delete m;
    }

    /// Rebuild the row of menu buttons by taking ownership of the live menubar's menus.
    void SetMenus( wxMenuBar* aBar )
    {
        m_menuSizer->Clear( true );   // destroys old buttons
        m_menuBtns.clear();

        for( wxMenu* m : m_ownedMenus )
            delete m;

        m_ownedMenus.clear();

        if( aBar )
        {
            while( aBar->GetMenuCount() > 0 )
            {
                // Use GetMenuLabel (the Append label, e.g. "&File"); WX_MENUBAR overrides
                // GetMenuLabelText to return the ACTION_MENU title, which is unset for the
                // top-level menus and would yield empty (0-width) buttons.
                wxString lbl = aBar->GetMenuLabel( 0 );
                lbl.Replace( wxS( "&" ), wxEmptyString );

                wxMenu*  menu = aBar->Remove( 0 );        // take ownership out of the bar
                m_ownedMenus.push_back( menu );

                // Owner-painted button so the label stays white on hover (a native wxButton
                // with a custom background darkens its text in the hover state — invisible here).
                TITLEBAR_MENU_BUTTON* b = new TITLEBAR_MENU_BUTTON( this, lbl, menu );

                // Full-height button (clean hover), ~8px between items (4px each side).
                m_menuSizer->Add( b, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP( 4 ) );
                m_menuBtns.push_back( b );
            }
        }

        Layout();
    }

    /// For WM_NCHITTEST: is this frame-client point over a clickable child (a button)?
    bool HitInteractive( const wxPoint& aClientPt )
    {
        wxPoint local = aClientPt - GetPosition();

        for( TITLEBAR_GLYPH_BUTTON* b : { m_min, m_max, m_close } )
            if( b && b->GetRect().Contains( local ) )
                return true;

        for( wxWindow* b : m_menuBtns )
            if( b && b->GetRect().Contains( local ) )
                return true;

        for( wxWindow* b : m_layoutBtns )
            if( b && b->GetRect().Contains( local ) )
                return true;

        return false;
    }

    /// Show the restore glyph when the window is maximized, the maximize glyph otherwise.
    void UpdateMaximizeGlyph()
    {
        if( m_max )
            m_max->SetGlyph( wxUniChar( m_frame->IsMaximized() ? 0xE923 : 0xE922 ) );
    }

    /// Re-sync every layout-toggle button's highlight to its pane's current visibility.
    /// Called after the AUI panes are built (they don't exist yet at construction time).
    void RefreshLayoutToggles()
    {
        for( const std::function<void()>& fn : m_layoutRefreshers )
            fn();
    }

private:
    /// One VS Code-style title-bar layout toggle: a glyph button that flips a pane's
    /// visibility (aToggle) and highlights itself while that pane is shown (aIsShown).
    TITLEBAR_PANEL_BUTTON* makeLayoutButton( TITLEBAR_PANEL_BUTTON::SIDE aSide,
                                             const wxString& aTooltip, std::function<void()> aToggle,
                                             std::function<bool()> aIsShown )
    {
        // Vector panel-diagram icon with a subtle hover (refined, not a solid block).
        TITLEBAR_PANEL_BUTTON* b =
                new TITLEBAR_PANEL_BUTTON( this, aSide, FromDIP( 40 ), wxColour( 60, 52, 92 ) );

        b->SetToolTip( aTooltip );

        // Brighten the glyph while its pane is visible, dim it while hidden (VS Code's
        // active/inactive toggle look).
        auto refresh = [b, aIsShown]() { b->SetActiveGlyph( aIsShown() ); };

        b->Bind( wxEVT_BUTTON,
                 [aToggle, refresh]( wxCommandEvent& ) { aToggle(); refresh(); } );

        refresh();
        m_layoutBtns.push_back( b );
        m_layoutRefreshers.push_back( refresh );
        return b;
    }

    /// The Anvil AI Assistant toggle: a vector "AI sparkle" logo mark (see TITLEBAR_AI_BUTTON) that
    /// flips the AI panel (aToggle) and lights up while it is shown (aIsShown).  This is the
    /// title-bar icon you click to reopen the AI chat after closing it — the way you reopen
    /// Copilot / Cursor chat.
    TITLEBAR_AI_BUTTON* makeAiToggle( const wxString& aTooltip, std::function<void()> aToggle,
                                      std::function<bool()> aIsShown )
    {
        TITLEBAR_AI_BUTTON* b = new TITLEBAR_AI_BUTTON( this, FromDIP( 48 ), wxColour( 60, 52, 92 ) );
        b->SetToolTip( aTooltip );

        auto refresh = [b, aIsShown]() { b->SetActiveGlyph( aIsShown() ); };

        b->Bind( wxEVT_BUTTON,
                 [aToggle, refresh]( wxCommandEvent& ) { aToggle(); refresh(); } );

        refresh();
        m_layoutBtns.push_back( b );
        m_layoutRefreshers.push_back( refresh );
        return b;
    }

    /// @param aHoverBg per-button hover colour (accent purple for min/max, red for close).
    TITLEBAR_GLYPH_BUTTON* makeWinButton( const wxString& aGlyph, const wxColour& aHoverBg )
    {
        return new TITLEBAR_GLYPH_BUTTON( this, aGlyph, FromDIP( 46 ), aHoverBg );
    }

    void applyTheme()
    {
        wxColour bg, fg;

        if( ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame )
        {
            // Match the rest of the Anvil "Vibrant Purple & Indigo" frame.  Use an explicit
            // colour (not wxSYS_COLOUR_MENUBAR, whose public query doesn't return the dark-mode
            // purple override) so the title-bar strip is the same indigo as the panels.
            bg = wxColour( 33, 27, 56 );
            fg = wxColour( 255, 255, 255 );
        }
        else
        {
            bg = wxSystemSettings::GetColour( wxSYS_COLOUR_MENUBAR );
            fg = wxSystemSettings::GetColour( wxSYS_COLOUR_WINDOWTEXT );
        }

        SetBackgroundColour( bg );
        SetForegroundColour( fg );

        // Re-tint existing children (logo, menu + layout + window buttons) — they cache the
        // colour at creation, so a later theme change needs them refreshed too.
        for( wxWindow* child : GetChildren() )
        {
            child->SetBackgroundColour( bg );
            child->SetForegroundColour( fg );
            child->Refresh();
        }
    }

    KICAD_MANAGER_FRAME*   m_frame;
    wxBoxSizer*            m_sizer = nullptr;
    wxBoxSizer*            m_menuSizer = nullptr;
    wxStaticBitmap*        m_logo = nullptr;
    TITLEBAR_GLYPH_BUTTON* m_min = nullptr;
    TITLEBAR_GLYPH_BUTTON* m_max = nullptr;
    TITLEBAR_GLYPH_BUTTON* m_close = nullptr;
    std::vector<wxWindow*> m_menuBtns;
    std::vector<wxMenu*>   m_ownedMenus;

    // VS Code-style title-bar layout toggles (Project Explorer / Local History) and the
    // closures that re-sync their highlight to pane visibility (see RefreshLayoutToggles()).
    std::vector<wxWindow*>             m_layoutBtns;
    std::vector<std::function<void()>> m_layoutRefreshers;
};


// Menubar and toolbar event table
BEGIN_EVENT_TABLE( KICAD_MANAGER_FRAME, EDA_BASE_FRAME )
    // Window events
    EVT_SIZE( KICAD_MANAGER_FRAME::OnSize )
    EVT_IDLE( KICAD_MANAGER_FRAME::OnIdle )

    // Menu events
    EVT_MENU( wxID_EXIT, KICAD_MANAGER_FRAME::OnExit )
    EVT_MENU( ID_EDIT_LOCAL_FILE_IN_TEXT_EDITOR, KICAD_MANAGER_FRAME::OnOpenFileInTextEditor )
    EVT_MENU( ID_EDIT_ADVANCED_CFG, KICAD_MANAGER_FRAME::OnEditAdvancedCfg )
    EVT_MENU( ID_IMPORT_CADSTAR_ARCHIVE_PROJECT, KICAD_MANAGER_FRAME::OnImportCadstarArchiveFiles )
    EVT_MENU( ID_IMPORT_EAGLE_PROJECT, KICAD_MANAGER_FRAME::OnImportEagleFiles )
    EVT_MENU( ID_IMPORT_EASYEDA_PROJECT, KICAD_MANAGER_FRAME::OnImportEasyEdaFiles )
    EVT_MENU( ID_IMPORT_EASYEDAPRO_PROJECT, KICAD_MANAGER_FRAME::OnImportEasyEdaProFiles )
    EVT_MENU( ID_IMPORT_KICAD_PROJECT, KICAD_MANAGER_FRAME::OnImportProject )
    EVT_MENU( ID_IMPORT_KICAD_PROJECT_FILES, KICAD_MANAGER_FRAME::OnImportKiCadProject )
    EVT_MENU( ID_IMPORT_ALTIUM_PROJECT, KICAD_MANAGER_FRAME::OnImportAltiumProjectFiles )
    EVT_MENU( ID_IMPORT_PADS_PROJECT, KICAD_MANAGER_FRAME::OnImportPadsProjectFiles )
    EVT_MENU( ID_IMPORT_GEDA_PROJECT, KICAD_MANAGER_FRAME::OnImportGedaFiles )

    // Range menu events
    EVT_MENU_RANGE( ID_FILE1, ID_FILEMAX, KICAD_MANAGER_FRAME::OnFileHistory )
    EVT_MENU( ID_FILE_LIST_CLEAR, KICAD_MANAGER_FRAME::OnClearFileHistory )

    // Special functions
    EVT_MENU( ID_INIT_WATCHED_PATHS, KICAD_MANAGER_FRAME::OnChangeWatchedPaths )

    // Drop files event
    EVT_DROP_FILES( KICAD_MANAGER_FRAME::OnDropFiles )

END_EVENT_TABLE()

// See below the purpose of this include
#include <wx/xml/xml.h>

KICAD_MANAGER_FRAME::KICAD_MANAGER_FRAME( wxWindow* parent, const wxString& title,
                                          const wxPoint& pos, const wxSize&   size ) :
        EDA_BASE_FRAME( parent, KICAD_MAIN_FRAME_T, title, pos, size, KICAD_DEFAULT_DRAWFRAME_STYLE,
                        KICAD_MANAGER_FRAME_NAME, &::Kiway, unityScale ),
        m_openSavedWindows( false ),
        m_restoredFromHistory( false ),
        m_active_project( false ),
        m_showHistoryPanel( false ),
        m_projectTreePane( nullptr ),
        m_historyPane( nullptr ),
        m_editorTabs( nullptr ),
        m_aiChatPanel( nullptr ),
        m_envilAgent( nullptr ),
        m_launcher( nullptr ),
        m_lastToolbarIconSize( 0 ),
        m_pcmButton( nullptr ),
        m_pcmUpdateCount( 0 )
{
    const int defaultLeftWinWidth = FromDIP( 250 );

    m_leftWinWidth = defaultLeftWinWidth; // Default value
    m_aboutTitle = "Anvil";

    // JPC: A very ugly hack to fix an issue on Linux: if the wxbase315u_xml_gcc_custom.so is
    // used **only** in PCM, it is not found in some cases at run time.
    // So just use it in the main module to avoid a not found issue
    // wxbase315u_xml_gcc_custom shared object when launching Kicad
    wxXmlDocument dummy;

    // Create the status line (bottom of the frame).  Left half is for project name; right half
    // is for Reporter (currently used by archiver/unarchiver and PCM).
    // Note: this is a KISTATUSBAR status bar. Therefore the specified number of fields
    // is the extra number of fields, not the full field count.
    // We need here 2 fields: the extra fiels to display the project name, and another field
    // to display a info (specific to Windows) using the FIELD_OFFSET_BGJOB_TEXT id offset (=1)
    // So the extra field count is 1
    CreateStatusBar( 2 );
    Pgm().GetBackgroundJobMonitor().RegisterStatusBar( (KISTATUSBAR*) GetStatusBar() );
    Pgm().GetNotificationsManager().RegisterStatusBar( (KISTATUSBAR*) GetStatusBar() );
    Pgm().RegisterLibraryLoadStatusBar( (KISTATUSBAR*) GetStatusBar() );
    GetStatusBar()->SetFont( KIUI::GetStatusFont( this ) );

    // Give an icon
    wxIcon icon;
    wxIconBundle icon_bundle;

    if( IsNightlyVersion())
    {
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_nightly, 48 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_nightly, 128 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_nightly, 256 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_nightly_32 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_nightly_16 ) );
        icon_bundle.AddIcon( icon );
    }
    else
    {
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad, 48 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad, 128 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad, 256 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_32 ) );
        icon_bundle.AddIcon( icon );
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad_16 ) );
        icon_bundle.AddIcon( icon );
    }

    SetIcons( icon_bundle );

    // Load the settings
    LoadSettings( config() );

    // Left window: is the box which display tree project
    m_projectTreePane = new PROJECT_TREE_PANE( this );

    setupTools();
    setupUIConditions();

    m_toolbarSettings = GetToolbarSettings<KICAD_MANAGER_TOOLBAR_SETTINGS>( "kicad-toolbars" );
    configureToolbars();
    RecreateToolbars();

#ifdef __WXMSW__
    // Create the custom title bar BEFORE the menu bar is (re)built: doReCreateMenuBar()
    // populates the title bar's menu buttons, and the menu rebuild is deferred via
    // CallAfter — which can fire before the rest of this constructor runs. Creating the
    // panel first guarantees it exists when the menu is populated.
    m_titleBar = new TITLEBAR_PANEL( this );
#endif

    ReCreateMenuBar();

    m_auimgr.SetManagedWindow( this );
    // wxAUI_MGR_DEFAULT keeps floating + drag-dock hints enabled so the common AI panel can be
    // dragged and re-docked anywhere (left/right/top/bottom) or floated free.  The other shell
    // panes (Project Explorer, editor tabs, title bar) are individually locked
    // Movable(false)/Floatable(false)/DockFixed, so only the AI panel actually moves.
    m_auimgr.SetFlags( wxAUI_MGR_DEFAULT | wxAUI_MGR_LIVE_RESIZE );

    // KiCad Next: the left vertical toolbar only repeats actions that already live in the
    // top menu bar (New/Open/Archive/Zoom/Project-dir), so it is hidden to avoid showing
    // the same commands twice. The 9 editor/tool launchers become the left icon rail below.
    m_auimgr.AddPane( m_tbLeft, EDA_PANE().VToolbar().Name( "TopMainToolbar" ).Left().Layer( 2 ).Hide() );

    // Project files tree placement.  In the single-window shell the tree becomes a
    // narrow left "Project Explorer" and the center is freed for the editor tabs
    // (Layer B); otherwise it fills the main area as before.
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell )
    {
        m_auimgr.AddPane( m_projectTreePane,
                          EDA_PANE().Name( "ProjectTree" ).Left().Layer( 0 )
                                    .CaptionVisible( true ).Caption( PROJECT_FILES_CAPTION )
                                    .PaneBorder( false ).Floatable( false ).Movable( false )
                                    .MinSize( defaultLeftWinWidth, FromDIP( 80 ) )
                                    .BestSize( defaultLeftWinWidth, -1 ) );
    }
    else
    {
        m_auimgr.AddPane( m_projectTreePane,
                          EDA_PANE().Name( "ProjectTree" ).Center().Layer( 0 )
                                    .CaptionVisible( true ).Caption( PROJECT_FILES_CAPTION )
                                    .PaneBorder( false ).Floatable( false ).Movable( false ) );
    }

    m_historyPane = new LOCAL_HISTORY_PANE( this );
    m_auimgr.AddPane( m_historyPane,
                      EDA_PANE().Palette().Name( "LocalHistory" ).Left().Layer( 1 ).Position( 1 )
                                .Caption( _( "Local History" ) ).PaneBorder( false )
                                .Floatable( false ).Movable( false ).CloseButton( true ).Hide() );

    if( m_showHistoryPanel )
        m_auimgr.GetPane( m_historyPane ).Show();

    wxSize client_size = GetClientSize();
    m_notebook = new wxAuiNotebook( this, wxID_ANY, wxPoint( client_size.x, client_size.y ),
                                    FromDIP( wxSize( 700, 590 ) ),
                                    wxAUI_NB_TOP | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_TAB_MOVE
                                            | wxAUI_NB_SCROLL_BUTTONS | wxNO_BORDER );

    m_notebook->SetArtProvider( new WX_AUI_TAB_ART() );

    m_notebook->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSE, &KICAD_MANAGER_FRAME::onNotebookPageCloseRequest, this );
    m_notebook->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSED, &KICAD_MANAGER_FRAME::onNotebookPageCountChanged, this );
    m_launcher = new PANEL_KICAD_LAUNCHER( m_notebook );

    m_notebook->Freeze();
    m_launcher->SetClosable( false );
    m_notebook->AddPage( m_launcher, EDITORS_CAPTION, false );
    m_notebook->SetTabCtrlHeight( 0 );
    m_notebook->Thaw();

    // Editor/tool launchers as a compact left icon rail (the "activity bar").
    m_auimgr.AddPane( m_notebook, EDA_PANE().Name( "Editors" ).Left().Layer( 3 ).Position( 0 )
                                            .CaptionVisible( false ).PaneBorder( false )
                                            .CloseButton( false ).Floatable( false ).Movable( false )
                                            .DockFixed( true )
                                            .MinSize( FromDIP( 40 ), -1 ).BestSize( FromDIP( 40 ), -1 ) );

    // KiCad Next single-window shell (Layer B): the center editor-tab area.  Each
    // editor (Schematic/PCB/Gerber/Calculator/…) is re-hosted here as a tab by
    // DockEditorAsTab() instead of floating as its own window.  Created only when
    // the flag is set, so the legacy layout is untouched when off.
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell )
    {
        // Step 2: an X on every tab (VS Code / Cursor style).  Clicking it does NOT
        // destroy the reparented editor frame (that would dangle KIWAY's player
        // pointer); onEditorTabCloseRequest() reparents the frame back to a hidden
        // top-level so it survives and can be re-docked later — see DetachDockedEditor().
        // wxAUI_NB_TAB_SPLIT: drag an editor tab to an edge to split the editor area into
        // side-by-side groups (VS Code style); the Split-Editor title-bar button does the
        // same on click.  Each page is a plain host panel, so splitting just re-homes that
        // panel between tab groups and never disturbs the reparented editor inside it.
        m_editorTabs = new wxAuiNotebook( this, wxID_ANY, wxDefaultPosition,
                                          FromDIP( wxSize( 700, 590 ) ),
                                          wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_SPLIT
                                                  | wxAUI_NB_CLOSE_ON_ALL_TABS
                                                  | wxAUI_NB_SCROLL_BUTTONS | wxNO_BORDER );
        m_editorTabs->SetArtProvider( new WX_AUI_TAB_ART() );

        m_editorTabs->Bind( wxEVT_AUINOTEBOOK_PAGE_CLOSE,
                            &KICAD_MANAGER_FRAME::onEditorTabCloseRequest, this );

        // Make the shell's top menu follow whichever editor tab is active (Track 1 menu +
        // Layer B tabs): switching to a Schematic/PCB tab swaps the title-bar menu to that
        // editor's File/Edit/View/Place/Route/Inspect/Tools set.
        m_editorTabs->Bind( wxEVT_AUINOTEBOOK_PAGE_CHANGED,
                            &KICAD_MANAGER_FRAME::onEditorTabChanged, this );

        // Make the top menu also follow which PANE has focus, not only tab switches.  The
        // Project Explorer tree and the editor-tab area are separate AUI panes, so focusing the
        // tree never fires PAGE_CHANGED; without this the Project Manager's own menu is
        // unreachable while any editor tab is open.  Clicking the tree restores the PM menu;
        // clicking back into the editor area restores the active editor's menu.
        m_projectTreePane->Bind( wxEVT_CHILD_FOCUS,
                                 &KICAD_MANAGER_FRAME::onShellPaneFocus, this );
        m_editorTabs->Bind( wxEVT_CHILD_FOCUS,
                            &KICAD_MANAGER_FRAME::onEditorAreaFocus, this );

        m_auimgr.AddPane( m_editorTabs, EDA_PANE().Name( "EditorTabs" ).Center().Layer( 0 )
                                                  .CaptionVisible( false ).PaneBorder( false )
                                                  .Floatable( false ).Movable( false ) );

        // KiCad Next single-window shell: register this shell as the KIWAY tab host so editor
        // KIFACEs (eeschema "Update PCB" / pcbnew "Update Schematic", etc.) dock the sibling
        // editor they open as a tab here instead of floating it as a separate window.  Cleared
        // in doCloseWindow().  Only registered when the shell's tab area actually exists.
        Kiway().SetTabHost( this );
    }

#ifdef __WXMSW__
    // Custom single-row title bar (logo + menu + window buttons), created earlier above.
    // The native caption is removed by the WM_NCCALCSIZE handler; this strip replaces it.
    m_auimgr.AddPane( m_titleBar, wxAuiPaneInfo().Name( "TitleBar" ).Top().Layer( 10 )
                                  .CaptionVisible( false ).PaneBorder( false ).Gripper( false )
                                  .DockFixed( true ).Floatable( false ).Movable( false )
                                  .Resizable( false )
                                  .MinSize( -1, FromDIP( 32 ) ).BestSize( -1, FromDIP( 32 ) ) );
#endif

    // KiCad Next: a single shell-owned "AI Assistant" panel (Cursor style).  Created only
    // when CommonAiPanel + SingleWindowShell are set; the per-editor panels are suppressed
    // (see SCH_EDIT_FRAME / PCB_EDIT_FRAME) so this is the ONLY AI panel in the window.  It
    // retargets to whichever editor tab is in front via syncAiPanelToActiveTab(); the editors
    // keep their own IPC reload channel, so the backend's revert/open_file still refreshes the
    // right document.  Wrapped in try/catch so a WebView failure never blocks the shell.
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell && ADVANCED_CFG::GetCfg().m_CommonAiPanel )
    {
        try
        {
            m_aiChatPanel = new WEBVIEW_PANEL( this );

            // Envil native AI agent: drives this panel from an in-process C++ Claude agent
            // (no Python backend).  Attached before the page loads so window.envilSend
            // exists when chat.html runs.  Tool calls reach the schematic editor over
            // MAIL_ENVIL_AI_TOOL, since kicad.exe cannot see SCH_EDIT_FRAME directly.
            m_envilAgent = new ENVIL_AI_AGENT( &Kiway(), this, m_aiChatPanel );
            m_envilAgent->Attach();

            // Locate chat.html the same way the editors do: stock data path, then the exe
            // directory (build output), then the wx resources dir.
            wxString      chatHtmlFound;
            wxArrayString searchPaths;
            searchPaths.Add( PATHS::GetStockDataPath( true ) );
            searchPaths.Add( wxFileName( wxStandardPaths::Get().GetExecutablePath() ).GetPath() );
            searchPaths.Add( wxStandardPaths::Get().GetResourcesDir() );

            for( const wxString& basePath : searchPaths )
            {
                wxString p = basePath + wxFileName::GetPathSeparator() + wxT( "ai_chat" )
                             + wxFileName::GetPathSeparator() + wxT( "chat.html" );

                if( wxFileExists( p ) )
                {
                    chatHtmlFound = p;
                    break;
                }
            }

            m_aiChatPanel->BindLoadedEvent();

            if( !chatHtmlFound.IsEmpty() )
            {
                wxString fileUrl = wxT( "file:///" ) + chatHtmlFound;
                fileUrl.Replace( wxT( "\\" ), wxT( "/" ) );

                wxString projPath = Prj().GetProjectPath();
                projPath.Replace( wxT( "\\" ), wxT( "/" ) );
                projPath.Replace( wxT( " " ), wxT( "%20" ) );

                // No app= scope here — the shell is not an editor.  syncAiPanelToActiveTab()
                // calls window.anvilSetSchematic / anvilSetPcb on tab switch, which is the
                // authoritative app-context signal chat.html honours.
                fileUrl += wxString::Format( wxT( "?t=%ld&backend=localhost:8765&project=%s" ),
                                             (long) wxDateTime::Now().GetTicks(), projPath );
                m_aiChatPanel->LoadURL( fileUrl );

                // A tab may already be open before the async WebView load finishes; push the
                // active document once the panel is ready.
                if( wxWebView* browser = m_aiChatPanel->GetWebView() )
                {
                    browser->Bind( wxEVT_WEBVIEW_LOADED,
                            [this]( wxWebViewEvent& aEvt )
                            {
                                syncAiPanelToActiveTab();
                                aEvt.Skip();
                            } );
                }
            }
            else
            {
                m_aiChatPanel->SetPage( wxT( "<!DOCTYPE html><html><body style='background:#1e1e2e;color:#e0e0e0;font-family:sans-serif;display:flex;align-items:center;justify-content:center;height:100vh'><p>AI Chat — chat.html not found</p></body></html>" ) );
            }

            KICAD_SETTINGS* aiCfg = kicadSettings();

            m_auimgr.AddPane( m_aiChatPanel, EDA_PANE().Name( AiChatPanelName() )
                              .Right().Layer( 6 )
                              .Caption( _( "AI Assistant" ) )
                              .CaptionVisible( true )
                              .PaneBorder( true )
                              .Floatable( true )     // can be torn off into its own floating window
                              .Movable( true )       // drag the "AI Assistant" caption to move it
                              .Dockable( true )      // dock on any side: left / right / top / bottom
                              .CloseButton( true )
                              .MinSize( FromDIP( wxSize( 320, 200 ) ) )
                              .BestSize( FromDIP( wxSize( 380, 600 ) ) )
                              .FloatingSize( FromDIP( wxSize( 500, 700 ) ) )
                              .Show( aiCfg ? aiCfg->m_ShowAiChat : true ) );
        }
        catch( ... )
        {
            wxLogWarning( wxT( "Shell AI Chat panel failed to initialize — continuing without it" ) );
            m_aiChatPanel = nullptr;
        }

        // KiCad Next (Cursor-style): give the SHELL its own backend command channel so a
        // just-built project can be loaded into the Project Files tree automatically. The
        // editors already listen for open_file/revert; the shell listens for "open_project"
        // and calls LoadProject() — which rebuilds the tree and resets the file watcher, so
        // the new files appear (and update live) like Cursor opening a folder. Additive: the
        // backend only emits open_project behind a config flag; eeschema/pcbnew ignore the
        // action. Port is resolved lazily in TryConnectAiIpc() (retry timer) so a backend
        // started after the shell still connects.
        if( m_aiChatPanel )
        {
            m_aiIpcClient = std::make_unique<AI_IPC_CLIENT>( "127.0.0.1", 5556 );

            m_aiIpcClient->SetCommandCallback(
                    [this]( const std::string& /* aAction */, const std::string& aData )
                    {
                        CallAfter( [this, aData]()
                        {
                            try
                            {
                                nlohmann::json payload = nlohmann::json::parse( aData );
                                wxString action = wxString::FromUTF8( payload.value( "action", "" ) );
                                nlohmann::json data = payload.value( "data", nlohmann::json::object() );

                                if( action != wxT( "open_project" ) )
                                    return;   // open_file/revert/refresh are for the editors

                                wxString proPath = wxString::FromUTF8( data.value( "path", "" ) );

                                if( proPath.IsEmpty() || !wxFileExists( proPath ) )
                                    return;

                                wxFileName proFn( proPath );

                                // Skip if it is already the active project so an in-place edit
                                // does not trigger a needless tree reload.
                                wxString current = Prj().GetProjectFullName();

                                if( current.IsEmpty() || !wxFileName( current ).SameAs( proFn ) )
                                    LoadProject( proFn );   // rebuilds the tree + resets the watcher
                            }
                            catch( ... )
                            {
                                wxLogDebug( wxT( "AI Chat (shell): bad IPC payload ignored" ) );
                            }
                        } );
                    } );

            if( !TryConnectAiIpc() )
            {
                m_aiIpcRetryAttempts = 0;
                m_aiIpcRetryTimer.SetOwner( this );
                Bind( wxEVT_TIMER, &KICAD_MANAGER_FRAME::OnAiIpcRetryTimer, this,
                      m_aiIpcRetryTimer.GetId() );
                m_aiIpcRetryTimer.Start( 2000, wxTIMER_CONTINUOUS );
                wxLogDebug( wxT( "AI Chat (shell): IPC connect failed on startup; retrying" ) );
            }
        }
    }

    m_auimgr.Update();

    // Envil AI MCP tool socket: lets an external Claude client (via the envil-mcp bridge)
    // place parts in the schematic on the user's own subscription — an alternative to the
    // in-app API-key agent. Loopback only, always on so MCP works regardless of CommonAiPanel.
    m_envilToolServer = std::make_unique<ENVIL_AI_TOOL_SERVER>( &Kiway(), this );

    if( !m_envilToolServer->Start() )
    {
        wxLogDebug( wxT( "Envil AI: MCP tool socket failed to start (port in use?)" ) );
        m_envilToolServer.reset();
    }

    // Restore the AI panel's saved width now that the pane exists (AddPane only set BestSize).
    if( m_aiChatPanel )
    {
        KICAD_SETTINGS* aiCfg = kicadSettings();

        if( aiCfg && aiCfg->m_AiChatPanelWidth > 0 )
        {
            wxAuiPaneInfo& aiPane = m_auimgr.GetPane( AiChatPanelName() );

            if( aiPane.IsOk() )
                SetAuiPaneSize( m_auimgr, aiPane, aiCfg->m_AiChatPanelWidth, -1 );
        }
    }

#ifdef __WXMSW__
    // Force a non-client recompute so the native title bar is dropped before first paint
    // (a style/frame change only takes visual effect after SWP_FRAMECHANGED).
    if( HWND hwnd = static_cast<HWND>( GetHandle() ) )
        ::SetWindowPos( hwnd, nullptr, 0, 0, 0, 0,
                        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );

    // The panes now exist, so the layout-toggle buttons can reflect real visibility
    // (Project Explorer shown, Local History hidden by default).
    if( m_titleBar )
        m_titleBar->RefreshLayoutToggles();
#endif

    // Now the actual m_projectTreePane size is set, give it a reasonable min width
    m_auimgr.GetPane( m_projectTreePane ).MinSize( defaultLeftWinWidth, FromDIP( 80 ) );


    wxSizer* mainSizer = GetSizer();

    // Only fit the initial window size the first time KiCad is run.
    if( mainSizer && config()->m_Window.state.size_x == 0 && config()->m_Window.state.size_y == 0 )
    {
        Layout();
        mainSizer->Fit( this );
        Center();
    }

    if( ADVANCED_CFG::GetCfg().m_HideVersionFromTitle )
        SetTitle( wxT( "Anvil" ) );
    else
        SetTitle( wxString( "Anvil " ) + GetMajorMinorVersion() );

    // Do not let the messages window have initial focus
    m_projectTreePane->SetFocus();

    // Init for dropping files
    m_acceptedExts.emplace( FILEEXT::AnvilProjectFileExtension, &KICAD_MANAGER_ACTIONS::loadProject );
    m_acceptedExts.emplace( FILEEXT::ProjectFileExtension, &KICAD_MANAGER_ACTIONS::loadProject );
    m_acceptedExts.emplace( FILEEXT::LegacyProjectFileExtension, &KICAD_MANAGER_ACTIONS::loadProject );

    // Gerber files
    // Note that all gerber files are aliased as GerberFileExtension
    m_acceptedExts.emplace( FILEEXT::GerberFileExtension, &KICAD_MANAGER_ACTIONS::viewDroppedGerbers );
    m_acceptedExts.emplace( FILEEXT::GerberJobFileExtension, &KICAD_MANAGER_ACTIONS::viewDroppedGerbers );
    m_acceptedExts.emplace( FILEEXT::DrillFileExtension, &KICAD_MANAGER_ACTIONS::viewDroppedGerbers );

    DragAcceptFiles( true );

    // Anvil "Vibrant Purple & Indigo" theme: repaint the shell's own backgrounds to match.
    if( ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame )
        applyAnvilShellTheme();

    // KiCad Next single-window shell: warm the heavy editor KIFACEs in the background so
    // the user's first click on Symbol/Footprint/Gerber/Drawing-Sheet is instant instead
    // of "loading the whole app".  No-op unless the shell + prewarm flags are set.  Use a
    // unique timer id and bind only to it, so this never intercepts the base frame's
    // auto-save timer (ID_AUTO_SAVE_TIMER), which also raises wxEVT_TIMER on this frame.
    m_prewarmTimer.SetOwner( this, wxWindow::NewControlId() );
    Bind( wxEVT_TIMER, &KICAD_MANAGER_FRAME::prewarmNextEditor, this, m_prewarmTimer.GetId() );
    schedulePrewarmEditors();

    // KiCad Next unified shell footer: with no editor tab docked yet, make sure the shell's own
    // status bar is the one showing (Project Manager state). No-op when the flag is off.
    syncShellStatusBarToActiveTab();
}


bool KICAD_MANAGER_FRAME::TryConnectAiIpc()
{
    if( !m_aiIpcClient )
        return false;

    if( m_aiIpcClient->IsConnected() )
        return true;

    // Re-read ipc_port.txt each attempt — same resolution order as the editors — so a backend
    // that came up after the shell (writing a fresh dynamic port) still connects.
    int           aiIpcPort = m_aiIpcClient->GetPort();
    wxFileName    exePath( wxStandardPaths::Get().GetExecutablePath() );
    wxArrayString portSearchPaths;

    // 1. Portable per-user state dir — matches the Python server's _user_state_dir().
    wxString orchestratorDir;
#ifdef __WXMSW__
    wxString localAppData;
    if( wxGetEnv( wxT( "LOCALAPPDATA" ), &localAppData ) && !localAppData.IsEmpty() )
        orchestratorDir = localAppData + wxFileName::GetPathSeparator() + wxT( "orchestrator" );
#elif defined( __WXMAC__ )
    orchestratorDir = wxGetHomeDir() + wxT( "/Library/Application Support/orchestrator" );
#else
    wxString xdgState;
    if( !wxGetEnv( wxT( "XDG_STATE_HOME" ), &xdgState ) || xdgState.IsEmpty() )
        xdgState = wxGetHomeDir() + wxT( "/.local/state" );
    orchestratorDir = xdgState + wxT( "/orchestrator" );
#endif

    if( !orchestratorDir.IsEmpty() )
        portSearchPaths.Add( orchestratorDir );

    // 2. Stock data dir + exe-relative — dev-tree fallbacks.
    portSearchPaths.Add( PATHS::GetStockDataPath( true ) + wxFileName::GetPathSeparator()
                        + wxT( "ai_backend" ) );
    portSearchPaths.Add( exePath.GetPath() + wxFileName::GetPathSeparator()
                        + wxT( "ai_backend" ) );

    for( const wxString& dir : portSearchPaths )
    {
        wxString portFile = dir + wxFileName::GetPathSeparator() + wxT( "ipc_port.txt" );

        if( !wxFileExists( portFile ) )
            continue;

        wxFile f( portFile );

        if( !f.IsOpened() )
            continue;

        wxString content;
        f.ReadAll( &content );
        long port;

        if( content.Trim().ToLong( &port ) && port > 0 && port < 65536 )
            aiIpcPort = (int) port;

        break;
    }

    m_aiIpcClient->SetPort( aiIpcPort );
    return m_aiIpcClient->Connect();
}


void KICAD_MANAGER_FRAME::OnAiIpcRetryTimer( wxTimerEvent& )
{
    // Never give up — back off the polling interval instead (the backend may start late).
    // Two-stage: 2 s for the first minute, then 10 s indefinitely. Mirrors the editors.
    constexpr int kFastAttempts  = 30;          // 30 × 2 s = 60 s
    constexpr int kSlowIntervalMs = 10000;      // 10 s after that

    if( !m_aiIpcClient || m_aiIpcClient->IsConnected() )
    {
        m_aiIpcRetryTimer.Stop();
        return;
    }

    m_aiIpcRetryAttempts++;

    if( TryConnectAiIpc() )
    {
        wxLogDebug( wxT( "AI Chat (shell): IPC connected on retry attempt %d (port %d)" ),
                    m_aiIpcRetryAttempts, m_aiIpcClient->GetPort() );
        m_aiIpcRetryTimer.Stop();
        return;
    }

    if( m_aiIpcRetryAttempts == kFastAttempts )
        m_aiIpcRetryTimer.Start( kSlowIntervalMs, wxTIMER_CONTINUOUS );
}


KICAD_MANAGER_FRAME::~KICAD_MANAGER_FRAME()
{
    // Stop the editor pre-warm before anything else so a queued tick cannot fire mid-teardown.
    m_prewarmTimer.Stop();

    // Tear down the shell's backend command channel + its retry timer.
    m_aiIpcRetryTimer.Stop();
    Unbind( wxEVT_TIMER, &KICAD_MANAGER_FRAME::OnAiIpcRetryTimer, this, m_aiIpcRetryTimer.GetId() );

    if( m_aiIpcClient )
    {
        m_aiIpcClient->Disconnect();
        m_aiIpcClient.reset();
    }
    Unbind( wxEVT_TIMER, &KICAD_MANAGER_FRAME::prewarmNextEditor, this, m_prewarmTimer.GetId() );

    Unbind( wxEVT_CHAR, &TOOL_DISPATCHER::DispatchWxEvent, m_toolDispatcher );
    Unbind( wxEVT_CHAR_HOOK, &TOOL_DISPATCHER::DispatchWxEvent, m_toolDispatcher );

    m_notebook->Unbind( wxEVT_AUINOTEBOOK_PAGE_CLOSE, &KICAD_MANAGER_FRAME::onNotebookPageCloseRequest, this );
    m_notebook->Unbind( wxEVT_AUINOTEBOOK_PAGE_CLOSED, &KICAD_MANAGER_FRAME::onNotebookPageCountChanged, this );

    if( m_editorTabs )
    {
        m_editorTabs->Unbind( wxEVT_AUINOTEBOOK_PAGE_CLOSE,
                              &KICAD_MANAGER_FRAME::onEditorTabCloseRequest, this );
        m_editorTabs->Unbind( wxEVT_AUINOTEBOOK_PAGE_CHANGED,
                              &KICAD_MANAGER_FRAME::onEditorTabChanged, this );
        m_editorTabs->Unbind( wxEVT_CHILD_FOCUS,
                              &KICAD_MANAGER_FRAME::onEditorAreaFocus, this );
    }

    if( m_projectTreePane )
        m_projectTreePane->Unbind( wxEVT_CHILD_FOCUS,
                                   &KICAD_MANAGER_FRAME::onShellPaneFocus, this );

    Pgm().GetBackgroundJobMonitor().UnregisterStatusBar( (KISTATUSBAR*) GetStatusBar() );
    Pgm().GetNotificationsManager().UnregisterStatusBar( (KISTATUSBAR*) GetStatusBar() );
    Pgm().UnregisterLibraryLoadStatusBar( (KISTATUSBAR*) GetStatusBar() );

    // Shutdown all running tools
    if( m_toolManager )
        m_toolManager->ShutdownAllTools();

    if( m_pcm )
        m_pcm->StopBackgroundUpdate();

    // Stop update manager before tearing down the AUI framework. The update
    // task runs on the thread pool and may call CallAfter on this frame, so it
    // must complete before we uninitialize AUI or destroy child windows.
    m_updateManager.reset();

    delete m_actions;
    delete m_toolManager;
    delete m_toolDispatcher;

    m_auimgr.UnInit();
}

void KICAD_MANAGER_FRAME::HideTabsIfNeeded()
{
    if( m_notebook->GetPageCount() == 1 )
        m_notebook->SetTabCtrlHeight( 0 );
    else
        m_notebook->SetTabCtrlHeight( -1 );
}


void KICAD_MANAGER_FRAME::PruneDeadEditorTabs()
{
#ifdef __WXMSW__
    if( !m_editorTabs )
        return;

    std::vector<std::pair<int, wxWindow*>> live;
    live.reserve( m_dockedEditors.size() );

    for( const std::pair<int, wxWindow*>& entry : m_dockedEditors )
    {
        // FindWindowById returns null once the player frame has been destroyed (the
        // same test KIWAY uses), so this never touches a freed frame.
        if( wxWindow::FindWindowById( entry.first ) )
        {
            live.push_back( entry );
        }
        else
        {
            int idx = m_editorTabs->GetPageIndex( entry.second );

            if( idx != wxNOT_FOUND )
                m_editorTabs->DeletePage( idx );   // destroys the now-orphaned host page
        }
    }

    m_dockedEditors = std::move( live );
#endif
}


void KICAD_MANAGER_FRAME::DetachDockedEditor( wxWindow* aPlayer )
{
#ifdef __WXMSW__
    if( !aPlayer )
        return;

    // Reverse the WS_CHILD surgery DockEditorAsTab() applied: detach the frame from its
    // host panel and restore its top-level decorations so it is a normal (hidden) window
    // again — ready to be re-docked.  We do NOT destroy it, so KIWAY's pointer stays valid.
    if( HWND child = static_cast<HWND>( aPlayer->GetHandle() ) )
    {
        ::SetParent( child, nullptr );

        LONG_PTR style = ::GetWindowLongPtr( child, GWL_STYLE );
        style &= ~( WS_CHILD | WS_POPUP );
        style |= WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
                 | WS_SYSMENU | WS_OVERLAPPED | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        ::SetWindowLongPtr( child, GWL_STYLE, style );

        ::SetWindowPos( child, nullptr, 0, 0, 0, 0,
                        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
    }

    // wx-side bookkeeping: top-level again, hidden, with its own status bar restored.
    aPlayer->Reparent( nullptr );
    aPlayer->Hide();

    if( wxFrame* frame = dynamic_cast<wxFrame*>( aPlayer ) )
    {
        if( wxStatusBar* sb = frame->GetStatusBar() )
            sb->Show();
    }
#endif
}


void KICAD_MANAGER_FRAME::onEditorTabCloseRequest( wxAuiNotebookEvent& evt )
{
#ifdef __WXMSW__
    if( !m_editorTabs )
        return;

    wxWindow* page = m_editorTabs->GetPage( evt.GetSelection() );

    // Find the docked editor whose host panel is the page being closed, detach its frame
    // (keeping it alive), and drop it from the registry.  The notebook then destroys the
    // now-empty host page — but the editor frame was reparented out above, so it survives.
    for( std::vector<std::pair<int, wxWindow*>>::iterator it = m_dockedEditors.begin();
         it != m_dockedEditors.end(); ++it )
    {
        if( it->second != page )
            continue;

        if( wxWindow* player = wxWindow::FindWindowById( it->first ) )
            DetachDockedEditor( player );

        m_dockedEditors.erase( it );
        break;
    }

    // wxAuiNotebook does not reliably fire PAGE_CHANGED when the LAST page is removed, so the
    // menu would otherwise stay on the just-closed editor.  When no editor tabs remain, restore
    // the Project Manager's own menu explicitly.
    if( m_dockedEditors.empty() )
    {
        syncShellMenuToActiveTab( true );
        syncShellStatusBarToActiveTab();   // last editor gone -> restore the manager's own footer
    }
#endif
}


EDA_BASE_FRAME* KICAD_MANAGER_FRAME::getActiveDockedEditorFrame()
{
#ifdef __WXMSW__
    if( !m_editorTabs )
        return nullptr;

    int sel = m_editorTabs->GetSelection();

    if( sel == wxNOT_FOUND )
        return nullptr;

    wxWindow* page = m_editorTabs->GetPage( sel );

    for( const std::pair<int, wxWindow*>& entry : m_dockedEditors )
    {
        if( entry.second != page )
            continue;

        // Look the player up by window-id (not a stored pointer) so a destroyed frame is
        // detected as null rather than dereferenced — same guard as PruneDeadEditorTabs().
        return dynamic_cast<EDA_BASE_FRAME*>( wxWindow::FindWindowById( entry.first ) );
    }
#endif
    return nullptr;
}


void KICAD_MANAGER_FRAME::syncShellMenuToActiveTab( bool aForcePM )
{
#ifdef __WXMSW__
    // Only the single-window shell with the unified menu bar re-hosts editors as tabs and owns
    // a single shared top menu; in every other configuration each editor shows its own menu.
    if( !m_editorTabs || !ADVANCED_CFG::GetCfg().m_SingleWindowShell
            || !ADVANCED_CFG::GetCfg().m_UnifiedMenuBar )
    {
        return;
    }

    // Rebuilding the bar destroys the title-bar buttons and deletes the menus they point at,
    // then calls m_auimgr.Update(); both move focus, which fires wxEVT_CHILD_FOCUS and lands
    // back here through onEditorAreaFocus()/onShellPaneFocus(). That nested pass ran
    // TITLEBAR_PANEL::SetMenus() again and deleted the half-torn-down menus a second time --
    // a double free that surfaced as an access violation on a tab switch (the freed
    // ACTION_MENU's vtable lives in the editor's KIFACE, so the crash was reported against
    // _eeschema.dll / _pcbnew.dll rather than here).
    if( m_syncingShellMenu )
        return;

    m_syncingShellMenu = true;

    // aForcePM: the caller is on the Project Manager (a non-editor pane has focus, or the last
    // tab just closed), so show the PM menu even though an editor tab may still be selected.
    EDA_BASE_FRAME* editor = aForcePM ? nullptr : getActiveDockedEditorFrame();

    // Set the state BEFORE rebuilding, not after: the focus handlers read it to decide whether
    // a rebuild is needed, so leaving it stale for the duration invites the re-entry above.
    m_shellMenuShowsEditor = ( editor != nullptr );

    if( editor )
    {
        // Show the active editor's menu (File/Edit/View/Place/Route/Inspect/Tools/Preferences):
        // the shell owns the wxMenuBar, but each ACTION_MENU dispatches through the editor's own
        // tool manager, so clicks reach the editor.
        buildCommonMenuBarFrom( editor );
        buildTitleBarMenuButtons();
    }
    else
    {
        // Project Manager context (no editor tab, or forced): restore the manager's own menu.
        // The manager's doReCreateMenuBar() already refreshes the title-bar buttons on this path.
        doReCreateMenuBar();
    }

    m_syncingShellMenu = false;
#endif
}


void KICAD_MANAGER_FRAME::onEditorTabChanged( wxAuiNotebookEvent& evt )
{
    syncShellMenuToActiveTab();
    syncAiPanelToActiveTab();
    syncShellStatusBarToActiveTab();
    evt.Skip();
}


void KICAD_MANAGER_FRAME::syncShellStatusBarToActiveTab()
{
#ifdef __WXMSW__
    if( !m_editorTabs || !ADVANCED_CFG::GetCfg().m_SingleWindowShell
            || !ADVANCED_CFG::GetCfg().m_UnifiedStatusBar )
    {
        return;
    }

    wxStatusBar* shellSb = GetStatusBar();

    if( !shellSb )
        return;

    // Native-footer mode: each docked editor shows its OWN status bar (kept visible by
    // DockEditorAsTab), exactly like standalone KiCad.  So all this has to do is hide the shell's
    // own status bar while an editor tab is in front — otherwise there would be two footers — and
    // show it again on the Project Manager tab.
    const bool editorActive = getActiveDockedEditorFrame() != nullptr;

    if( shellSb->IsShown() == !editorActive )
        return;   // already in the right state — skip the relayout to avoid flicker

    shellSb->Show( !editorActive );

    // The frame's client area changes when the status bar is shown/hidden; force a resize so the
    // AUI panes (and the embedded editor frame + its native footer) reflow to fill it.
    SendSizeEvent();
#endif
}


void KICAD_MANAGER_FRAME::syncAiPanelToActiveTab()
{
    if( !m_aiChatPanel )
        return;

    EDA_BASE_FRAME* editor = getActiveDockedEditorFrame();

    if( !editor )
        return;   // launcher / no editor tab — leave the panel's current context

    wxString setter;

    if( editor->GetFrameType() == FRAME_SCH )
        setter = wxT( "anvilSetSchematic" );
    else if( editor->GetFrameType() == FRAME_PCB_EDITOR )
        setter = wxT( "anvilSetPcb" );
    else
        return;   // a non-schematic/PCB tab (Gerber, Calculator, …) — keep current context

    wxString projPath = Prj().GetProjectPath();
    wxString file     = editor->GetCurrentFileName();

    projPath.Replace( wxT( "\\" ), wxT( "/" ) );
    projPath.Replace( wxT( "\"" ), wxT( "\\\"" ) );
    file.Replace( wxT( "\\" ), wxT( "/" ) );
    file.Replace( wxT( "\"" ), wxT( "\\\"" ) );

    // anvilSetSchematic / anvilSetPcb set the panel's app context AND re-hello the backend
    // with the new file, so the one shell panel always acts on the front tab's document.
    wxString script = wxString::Format(
            wxT( "if (window.%s) window.%s(\"%s\", \"%s\");" ),
            setter, setter, projPath, file );
    m_aiChatPanel->RunScriptAsync( script );
}


bool KICAD_MANAGER_FRAME::AiChatPanelShown()
{
    if( !m_aiChatPanel )
        return false;

    wxAuiPaneInfo& pane = m_auimgr.GetPane( AiChatPanelName() );
    return pane.IsOk() && pane.IsShown();
}


void KICAD_MANAGER_FRAME::ToggleAiChat()
{
    if( !m_aiChatPanel )
        return;

    wxAuiPaneInfo& pane = m_auimgr.GetPane( AiChatPanelName() );

    if( !pane.IsOk() )
        return;

    bool            show = !pane.IsShown();
    KICAD_SETTINGS* cfg  = kicadSettings();

    pane.Show( show );

    if( show )
    {
        int width = ( cfg && cfg->m_AiChatPanelWidth > 0 ) ? cfg->m_AiChatPanelWidth : 380;
        SetAuiPaneSize( m_auimgr, pane, width, -1 );
        syncAiPanelToActiveTab();

        // Cursor-style "fresh on open": reopening the panel starts a blank chat.
        // This fires ONLY here (an explicit pane show), not in syncAiPanelToActiveTab
        // which also runs on tab flips. chat.html's anvilPanelOpened() guards itself
        // (no-op if blank, kept if a build is in flight), so this is safe to call.
        m_aiChatPanel->RunScriptAsync( wxT( "if (window.anvilPanelOpened) window.anvilPanelOpened();" ) );
    }
    else
    {
        if( cfg )
            cfg->m_AiChatPanelWidth = m_aiChatPanel->GetSize().x;

        m_auimgr.Update();
    }

    if( cfg )
        cfg->m_ShowAiChat = show;

    if( m_titleBar )
        m_titleBar->RefreshLayoutToggles();
}


void KICAD_MANAGER_FRAME::ShowAiSplitLayout()
{
    if( !m_aiChatPanel )
        return;

    wxAuiPaneInfo& pane = m_auimgr.GetPane( AiChatPanelName() );

    if( !pane.IsOk() )
        return;

    pane.Show( true );

    // Side-by-side "split" layout: give the AI panel ~40% of the client width so it sits
    // beside the active editor like a VS Code editor split, rather than the narrow sidebar
    // width that the chat-bubble toggle restores.
    int clientW = GetClientSize().x;
    int aiW     = clientW > 0 ? ( clientW * 2 ) / 5 : FromDIP( 380 );

    if( aiW < FromDIP( 380 ) )
        aiW = FromDIP( 380 );

    SetAuiPaneSize( m_auimgr, pane, aiW, -1 );
    syncAiPanelToActiveTab();

    // Cursor-style "fresh on open" — see ToggleAiChat(). Only on explicit show.
    m_aiChatPanel->RunScriptAsync( wxT( "if (window.anvilPanelOpened) window.anvilPanelOpened();" ) );

    if( KICAD_SETTINGS* cfg = kicadSettings() )
    {
        cfg->m_ShowAiChat       = true;
        cfg->m_AiChatPanelWidth = aiW;
    }

    if( m_titleBar )
        m_titleBar->RefreshLayoutToggles();
}


namespace
{
// Recursively repaint a shell-owned panel subtree with the Anvil chrome colours.  Safe only for
// the shell's own widgets — never call on the editor tab pages (reparented editor frames keep
// their own theme + a drawing canvas that must stay untouched).
void anvilRecolorShellTree( wxWindow* aWindow, const wxColour& aBg, const wxColour& aFg )
{
    if( !aWindow )
        return;

    aWindow->SetBackgroundColour( aBg );
    aWindow->SetForegroundColour( aFg );

    for( wxWindow* child : aWindow->GetChildren() )
        anvilRecolorShellTree( child, aBg, aFg );

    aWindow->Refresh();
}
} // namespace


void KICAD_MANAGER_FRAME::applyAnvilShellTheme()
{
    // Anvil chrome palette (matches sch_edit_frame.cpp and the MSW dark-mode palette).
    const wxColour bgDeep  ( 24, 20, 42 );
    const wxColour bgPanel ( 33, 27, 56 );
    const wxColour accent  ( 139, 92, 246 );
    const wxColour capAct  ( 88, 52, 156 );
    const wxColour capInact( 43, 35, 70 );
    const wxColour border  ( 58, 46, 99 );
    const wxColour sash    ( 40, 33, 66 );
    const wxColour text    ( 255, 255, 255 );

    // 1) Dock-pane chrome: the background behind/between panes, sashes, borders, captions.
    if( wxAuiDockArt* dockArt = m_auimgr.GetArtProvider() )
    {
        dockArt->SetColour( wxAUI_DOCKART_BACKGROUND_COLOUR, bgDeep );
        dockArt->SetColour( wxAUI_DOCKART_SASH_COLOUR, sash );
        dockArt->SetColour( wxAUI_DOCKART_BORDER_COLOUR, border );
        dockArt->SetColour( wxAUI_DOCKART_GRIPPER_COLOUR, bgPanel );
        dockArt->SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, capAct );
        dockArt->SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, capAct );
        dockArt->SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, capInact );
        dockArt->SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, capInact );
        dockArt->SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, text );
        dockArt->SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, text );
    }

    // 2) Tab strips of the side notebook and the centre editor notebook.
    for( wxAuiNotebook* nb : { m_notebook, m_editorTabs } )
    {
        if( !nb )
            continue;

        if( wxAuiTabArt* tabArt = nb->GetArtProvider() )
        {
            tabArt->SetColour( bgPanel );
            tabArt->SetActiveColour( accent );
        }

        nb->SetBackgroundColour( bgPanel );
        nb->Refresh();
    }

    // 3) Shell-owned side panels (project tree + launcher).  Not the editor tab pages.
    anvilRecolorShellTree( m_projectTreePane, bgPanel, text );
    anvilRecolorShellTree( m_launcher, bgPanel, text );

    // 4) Status bar.
    if( wxStatusBar* sb = GetStatusBar() )
    {
        sb->SetBackgroundColour( bgPanel );
        sb->SetForegroundColour( text );
        sb->Refresh();
    }

    Refresh();
}


void KICAD_MANAGER_FRAME::onShellPaneFocus( wxChildFocusEvent& aEvent )
{
#ifdef __WXMSW__
    // The Project Explorer (a non-editor pane) gained focus: the user is on the Project
    // Manager, so bring its own menu back.  Only rebuild when an editor menu is currently
    // shown, so repeated clicks in the tree don't rebuild the bar.
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell && ADVANCED_CFG::GetCfg().m_UnifiedMenuBar
            && m_shellMenuShowsEditor )
    {
        syncShellMenuToActiveTab( true );
    }
#endif
    aEvent.Skip();
}


void KICAD_MANAGER_FRAME::onEditorAreaFocus( wxChildFocusEvent& aEvent )
{
#ifdef __WXMSW__
    // Focus returned to the editor-tab area: show the active editor's menu again.  Only rebuild
    // when the PM menu is currently shown and an editor tab actually exists.
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell && ADVANCED_CFG::GetCfg().m_UnifiedMenuBar
            && !m_shellMenuShowsEditor && getActiveDockedEditorFrame() )
    {
        syncShellMenuToActiveTab( false );
    }
#endif
    aEvent.Skip();
}


void KICAD_MANAGER_FRAME::schedulePrewarmEditors()
{
    // Background editor pre-warm: only in the single-window shell, and only when the
    // separate ShellPrewarmEditors flag is on, so the behaviour stays additive/reversible.
    if( !ADVANCED_CFG::GetCfg().m_SingleWindowShell
            || !ADVANCED_CFG::GetCfg().m_ShellPrewarmEditors )
    {
        return;
    }

    // The library/tool editors whose first open "loads the whole app".  All are
    // project-independent, so warming them needs no open project and cannot touch project
    // state.  Schematic/PCB are intentionally excluded: they must parse the project file
    // on open anyway, and the user reaches them through the normal project flow.
    m_prewarmQueue = { FRAME_SCH_SYMBOL_EDITOR, FRAME_FOOTPRINT_EDITOR,
                       FRAME_GERBER, FRAME_PL_EDITOR };

    // Start after the manager window has painted and become responsive; the queue then
    // warms one editor per tick (sequential, never concurrent), yielding to the UI in
    // between so clicking/typing stays smooth while the editors load silently.
    m_prewarmTimer.StartOnce( 1500 );
}


void KICAD_MANAGER_FRAME::prewarmNextEditor( wxTimerEvent& aEvent )
{
    if( m_prewarmQueue.empty() )
        return;

    FRAME_T type = static_cast<FRAME_T>( m_prewarmQueue.front() );
    m_prewarmQueue.erase( m_prewarmQueue.begin() );

    try
    {
        // Create-but-don't-show: this builds the editor frame (loads its KIFACE, creates
        // its GAL canvas, initialises its library tree) and caches it in KIWAY.  We never
        // Show() it, so nothing appears on screen; ShowPlayer()/Execute() later find the
        // ready player and just dock it as a tab — instantly.
        Kiway().Player( type, true );
    }
    catch( const IO_ERROR& )
    {
        // KIFACE missing or failed to load: pre-warm is best-effort, so skip and carry on.
    }

    // Warm the next one on a short tick so the GUI thread breathes between heavy loads.
    if( !m_prewarmQueue.empty() )
        m_prewarmTimer.StartOnce( 400 );
}


bool KICAD_MANAGER_FRAME::DockEditorAsTab( KIWAY_PLAYER* aPlayer, const wxString& aTitle )
{
#ifdef __WXMSW__
    if( !m_editorTabs || !aPlayer )
        return false;

    // Drop any tab whose editor was destroyed (e.g. on a prior project close) so we
    // never leave an orphan tab or add a duplicate when the editor is re-opened.
    PruneDeadEditorTabs();

    // Already docked?  Match by window-id and select its tab.
    for( const std::pair<int, wxWindow*>& entry : m_dockedEditors )
    {
        if( entry.first == aPlayer->GetId() )
        {
            int idx = m_editorTabs->GetPageIndex( entry.second );

            if( idx != wxNOT_FOUND )
                m_editorTabs->SetSelection( idx );

            return true;
        }
    }

    // A plain panel is the notebook page; the editor frame is reparented inside it so
    // wxAuiNotebook only ever manages an ordinary child window, sized via the sizer.
    wxPanel*    host  = new wxPanel( m_editorTabs, wxID_ANY );
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    host->SetSizer( sizer );

    // Strip the editor frame's top-level decorations: no caption, resize border, or
    // system menu of its own — the shell's single title/menu bar (Track 1) owns the
    // chrome.  Turn it into a child window so it lives inside the tab.
    if( HWND child = static_cast<HWND>( aPlayer->GetHandle() ) )
    {
        // wx-side reparent first so the sizer / child bookkeeping is correct.
        aPlayer->Reparent( host );

        // CRITICAL: wxWidgets does NOT reliably perform the native ::SetParent for a
        // top-level wxFrame, so without this call the editor keeps the DESKTOP as its
        // real parent — it gets WS_CHILD set but still floats borderless at (0,0) over
        // the shell, hiding the activity rail and Project Explorer.  Force it natively.
        if( HWND hostHwnd = static_cast<HWND>( host->GetHandle() ) )
            ::SetParent( child, hostHwnd );

        // Strip the frame's top-level decorations (caption / resize border / sys menu)
        // and turn it into a child window — the shell owns the chrome.  Set the style
        // AFTER the parent change so the WS_CHILD conversion sticks.
        LONG_PTR style = ::GetWindowLongPtr( child, GWL_STYLE );
        style &= ~( WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
                    | WS_SYSMENU | WS_POPUP | WS_OVERLAPPED | WS_DLGFRAME | WS_BORDER );
        style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        ::SetWindowLongPtr( child, GWL_STYLE, style );

        sizer->Add( aPlayer, 1, wxEXPAND );

        // A style change only takes visual effect after a frame recompute.
        ::SetWindowPos( child, nullptr, 0, 0, 0, 0,
                        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
    }
    else
    {
        aPlayer->Reparent( host );
        sizer->Add( aPlayer, 1, wxEXPAND );
    }

    // The editor frame keeps its own status bar.
    //
    // Default (UnifiedStatusBar off): hide it so it does not double up with the shell's status
    // bar at the bottom of the window.
    //
    // UnifiedStatusBar on: do the opposite — keep the editor's OWN native footer visible (so the
    // schematic / PCB tab shows exactly the same coords / grid / zoom / units footer as standalone
    // KiCad).  syncShellStatusBarToActiveTab() then hides the shell's own bar while an editor tab
    // is in front so there is still only one footer.
    if( wxStatusBar* sb = aPlayer->GetStatusBar() )
        sb->Show( ADVANCED_CFG::GetCfg().m_SingleWindowShell
                  && ADVANCED_CFG::GetCfg().m_UnifiedStatusBar );

    // Guard against a blank tab when a freshly created frame has no title yet.
    wxString tabLabel = aTitle;

    if( tabLabel.IsEmpty() )
        tabLabel = aPlayer->GetTitle();

    if( tabLabel.IsEmpty() )
        tabLabel = _( "Editor" );

    m_editorTabs->AddPage( host, tabLabel, true );
    m_dockedEditors.emplace_back( aPlayer->GetId(), host );

    aPlayer->Show( true );
    host->Layout();

    // The reparented frame does not always honour the box sizer on first dock (a wxFrame
    // is an unusual sizer item), so size it explicitly to the host's client area now;
    // subsequent shell resizes are handled by the sizer / host EVT_SIZE.
    aPlayer->SetSize( host->GetClientSize() );

    // Give the editor keyboard focus so hotkeys work immediately (the floating path
    // did this via player->SetFocus(); the docked path must do it too).
    aPlayer->SetFocus();

    // Make the shell's top menu reflect the just-docked editor.  AddPage(..., true) above may
    // have fired PAGE_CHANGED before m_dockedEditors held this entry (so its handler fell back
    // to the manager menu); now that the registry is populated, set the editor's menu for real.
    syncShellMenuToActiveTab();

    // Point the shell-owned common AI panel at the just-docked document for the same reason.
    syncAiPanelToActiveTab();

    // Mirror the just-docked editor's (now hidden) status bar into the shell footer.
    syncShellStatusBarToActiveTab();

    return true;
#else
    // Non-Windows: native reparenting recipe differs (GTK/Cocoa); fall back to a
    // floating frame for now so the caller shows it as before.
    return false;
#endif
}


bool KICAD_MANAGER_FRAME::DockPlayerAsTab( KIWAY_PLAYER* aPlayer )
{
    // Cross-KIFACE bridge (KIFACE_TAB_HOST): an editor opened a sibling editor and is asking
    // the shell to host it as a tab.  Reuse the same docking path the shell uses when it
    // opens editors itself, so an editor launched via "Update PCB" / "Update Schematic"
    // behaves identically to one opened from the Project Explorer.  DockEditorAsTab() is
    // idempotent: if the player is already docked it just re-selects its tab.
    if( !aPlayer )
        return false;

    return DockEditorAsTab( aPlayer, aPlayer->GetTitle() );
}


bool KICAD_MANAGER_FRAME::IsPlayerDocked( KIWAY_PLAYER* aPlayer )
{
    if( !m_editorTabs || !aPlayer )
        return false;

    // Matched by window-id (same key DockEditorAsTab() stores), then confirm the host page
    // is still a live tab so a stale entry from a destroyed editor never reads as docked.
    for( const std::pair<int, wxWindow*>& entry : m_dockedEditors )
    {
        if( entry.first == aPlayer->GetId() )
            return m_editorTabs->GetPageIndex( entry.second ) != wxNOT_FOUND;
    }

    return false;
}


void KICAD_MANAGER_FRAME::onNotebookPageCountChanged( wxAuiNotebookEvent& evt )
{
    HideTabsIfNeeded();
}


void KICAD_MANAGER_FRAME::onNotebookPageCloseRequest( wxAuiNotebookEvent& evt )
{
    wxAuiNotebook* notebook = (wxAuiNotebook*) evt.GetEventObject();
    wxWindow*      page = notebook->GetPage( evt.GetSelection() );

    if( PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( page ) )
    {
        if( panel->GetClosable() )
        {
            if( !panel->GetCanClose() )
                evt.Veto();

            CallAfter(
                    [this]()
                    {
                        SaveOpenJobSetsToLocalSettings();
                    } );
        }
        else
        {
            evt.Veto();
        }
    }
}


wxStatusBar* KICAD_MANAGER_FRAME::OnCreateStatusBar( int number, long style, wxWindowID id,
                                                     const wxString& name )
{
    return new KISTATUSBAR( number, this, id,
                            static_cast<KISTATUSBAR::STYLE_FLAGS>(  KISTATUSBAR::NOTIFICATION_ICON
                                                                  | KISTATUSBAR::CANCEL_BUTTON
                                                                  | KISTATUSBAR::WARNING_ICON ) );
}


void KICAD_MANAGER_FRAME::CreatePCM()
{
    // creates the PLUGIN_CONTENT_MANAGER, if not exists
    if( m_pcm )
        return;

    m_pcm = std::make_shared<PLUGIN_CONTENT_MANAGER>(
            [this]( int aUpdateCount )
            {
                if( Pgm().m_Quitting )
                    return;

                m_pcmUpdateCount = aUpdateCount;

                if( aUpdateCount > 0 )
                {
                    Pgm().GetNotificationsManager().CreateOrUpdate(
                            wxS( "pcm" ),
                            _( "PCM Updates Available" ),
                            wxString::Format( _( "%d package update(s) available" ), aUpdateCount ),
                            wxT( "" ) );
                }
                else
                {
                    Pgm().GetNotificationsManager().Remove( wxS( "pcm" ) );
                }

                CallAfter(
                        [this]()
                        {
                            updatePcmButtonBadge();
                        } );
            });

    m_pcm->SetRepositoryList( kicadSettings()->m_PcmRepositories );
}


void KICAD_MANAGER_FRAME::setupTools()
{
    // Create the manager
    m_toolManager = new TOOL_MANAGER;
    m_toolManager->SetEnvironment( nullptr, nullptr, nullptr, config(), this );
    m_actions = new KICAD_MANAGER_ACTIONS();

    m_toolDispatcher = new TOOL_DISPATCHER( m_toolManager );

    // Attach the events to the tool dispatcher
    Bind( wxEVT_CHAR, &TOOL_DISPATCHER::DispatchWxEvent, m_toolDispatcher );
    Bind( wxEVT_CHAR_HOOK, &TOOL_DISPATCHER::DispatchWxEvent, m_toolDispatcher );

    // Register tools
    m_toolManager->RegisterTool( new COMMON_CONTROL );
    m_toolManager->RegisterTool( new KICAD_MANAGER_CONTROL );
    m_toolManager->InitTools();
}


void KICAD_MANAGER_FRAME::setupUIConditions()
{
    EDA_BASE_FRAME::setupUIConditions();

    ACTION_MANAGER* manager = m_toolManager->GetActionManager();

    wxASSERT( manager );

    auto activeProject =
            [this] ( const SELECTION& )
            {
                return m_active_project;
            };

#define ENABLE( x ) ACTION_CONDITIONS().Enable( x )

    ACTION_CONDITIONS activeProjectCond;
    activeProjectCond.Enable( activeProject );

    manager->SetConditions( ACTIONS::saveAs,                       activeProjectCond );
    manager->SetConditions( KICAD_MANAGER_ACTIONS::closeProject,   activeProjectCond );
    manager->SetConditions( KICAD_MANAGER_ACTIONS::archiveProject, activeProjectCond );
    manager->SetConditions( KICAD_MANAGER_ACTIONS::newJobsetFile,  activeProjectCond );
    manager->SetConditions( KICAD_MANAGER_ACTIONS::openJobsetFile, activeProjectCond );

    auto historyCond =
            [this]( const SELECTION& )
            {
                return HistoryPanelShown();
            };

    manager->SetConditions( KICAD_MANAGER_ACTIONS::showLocalHistory, ACTION_CONDITIONS().Check( historyCond ) );

    // These are just here for text boxes, search boxes, etc. in places such as the standard
    // file dialogs.
    manager->SetConditions( ACTIONS::cut,     ENABLE( SELECTION_CONDITIONS::ShowNever ) );
    manager->SetConditions( ACTIONS::copy,    ENABLE( SELECTION_CONDITIONS::ShowNever ) );
    manager->SetConditions( ACTIONS::paste,   ENABLE( SELECTION_CONDITIONS::ShowNever ) );

#undef ENABLE
}


wxWindow* KICAD_MANAGER_FRAME::GetToolCanvas() const
{
    return m_projectTreePane;
}


APP_SETTINGS_BASE* KICAD_MANAGER_FRAME::config() const
{
    APP_SETTINGS_BASE* ret = PgmTop().PgmSettings();
    wxASSERT( ret );
    return ret;
}


KICAD_SETTINGS* KICAD_MANAGER_FRAME::kicadSettings() const
{
    KICAD_SETTINGS* ret = dynamic_cast<KICAD_SETTINGS*>( config() );
    wxASSERT( ret );
    return ret;
}


void KICAD_MANAGER_FRAME::PreloadAllLibraries()
{
    CallAfter(
            [&]()
            {
                KIFACE *schface = Kiway().KiFACE( KIWAY::FACE_SCH );
                schface->PreloadLibraries( &Kiway() );

                KIFACE *pcbface = Kiway().KiFACE( KIWAY::FACE_PCB );
                pcbface->PreloadLibraries( &Kiway() );

                Pgm().PreloadDesignBlockLibraries( &Kiway() );
            } );
}


wxString KICAD_MANAGER_FRAME::GetCurrentFileName() const
{
    return GetProjectFileName();
}


const wxString KICAD_MANAGER_FRAME::GetProjectFileName() const
{
    return Pgm().GetSettingsManager().IsProjectOpen() ? Prj().GetProjectFullName()
                                                      : wxString( wxEmptyString );
}


const wxString KICAD_MANAGER_FRAME::SchFileName()
{
   wxFileName   fn( GetProjectFileName() );

   // Prefer the Anvil-native schematic; fall back to an existing .kicad_sch.
   fn.SetExt( FILEEXT::AnvilSchematicFileExtension );

   if( !fn.FileExists() )
   {
       wxFileName kicadFn( fn );
       kicadFn.SetExt( FILEEXT::KiCadSchematicFileExtension );

       if( kicadFn.FileExists() )
           return kicadFn.GetFullPath();
   }

   return fn.GetFullPath();
}


const wxString KICAD_MANAGER_FRAME::SchLegacyFileName()
{
   wxFileName   fn( GetProjectFileName() );

   fn.SetExt( FILEEXT::LegacySchematicFileExtension );
   return fn.GetFullPath();
}


const wxString KICAD_MANAGER_FRAME::PcbFileName()
{
   wxFileName   fn( GetProjectFileName() );

   // Prefer the Anvil-native board; fall back to an existing .kicad_pcb.
   fn.SetExt( FILEEXT::AnvilPcbFileExtension );

   if( !fn.FileExists() )
   {
       wxFileName kicadFn( fn );
       kicadFn.SetExt( FILEEXT::PcbFileExtension );

       if( kicadFn.FileExists() )
           return kicadFn.GetFullPath();
   }

   return fn.GetFullPath();
}


const wxString KICAD_MANAGER_FRAME::PcbLegacyFileName()
{
   wxFileName   fn( GetProjectFileName() );

   fn.SetExt( FILEEXT::LegacyPcbFileExtension );
   return fn.GetFullPath();
}


void KICAD_MANAGER_FRAME::ReCreateTreePrj()
{
    m_projectTreePane->ReCreateTreePrj();
}


const SEARCH_STACK& KICAD_MANAGER_FRAME::sys_search()
{
    return PgmTop().SysSearch();
}


wxString KICAD_MANAGER_FRAME::help_name()
{
    return PgmTop().GetHelpFileName();
}


void KICAD_MANAGER_FRAME::OnSize( wxSizeEvent& event )
{
    if( m_auimgr.GetManagedWindow() )
        m_auimgr.Update();

#ifdef __WXMSW__
    // Keep the title-bar maximize/restore glyph in sync when the window is maximized
    // or restored via the OS (double-click caption, snap, Win+Up) — not just the button.
    if( m_titleBar )
        m_titleBar->UpdateMaximizeGlyph();
#endif

    PrintPrjInfo();

#if defined( _WIN32 )
    KISTATUSBAR* statusBar = static_cast<KISTATUSBAR*>( GetStatusBar() );
    statusBar->SetEllipsedTextField( m_FileWatcherInfo, 1 );
#endif

    event.Skip();
}


void KICAD_MANAGER_FRAME::DoWithAcceptedFiles()
{
    // All fileNames are now in m_AcceptedFiles vector.
    // Check if contains a project file name and load project.
    // If not, open files in dedicated app.
    for( const wxFileName& fileName : m_AcceptedFiles )
    {
        wxString ext = fileName.GetExt();

        if( ext == FILEEXT::AnvilProjectFileExtension || ext == FILEEXT::ProjectFileExtension
            || ext == FILEEXT::LegacyProjectFileExtension )
        {
            wxString fn = fileName.GetFullPath();
            m_toolManager->RunAction<wxString*>( *m_acceptedExts.at( fileName.GetExt() ), &fn );

            return;
        }
    }

    // Then stock gerber files in gerberFiles and run action for other files.
    wxString gerberFiles;

    // Gerbview editor should be able to open Gerber and drill files
    for( const wxFileName& fileName : m_AcceptedFiles )
    {
        wxString ext = fileName.GetExt();

        if( ext == FILEEXT::GerberJobFileExtension
            || ext == FILEEXT::DrillFileExtension
            || FILEEXT::IsGerberFileExtension( ext ) )
        {
            gerberFiles += wxT( '\"' );
            gerberFiles += fileName.GetFullPath() + wxT( '\"' );
            gerberFiles = gerberFiles.Pad( 1 );
        }
        else
        {
            wxString fn = fileName.GetFullPath();
            m_toolManager->RunAction<wxString*>( *m_acceptedExts.at( fileName.GetExt() ), &fn );
        }
    }

    // Execute Gerbviewer
    if( !gerberFiles.IsEmpty() )
    {
        wxString fullEditorName = FindKicadFile( GERBVIEW_EXE );

        if( wxFileExists( fullEditorName ) )
        {
            wxString command = fullEditorName + " " + gerberFiles;
            m_toolManager->RunAction<wxString*>( *m_acceptedExts.at( FILEEXT::GerberFileExtension ), &command );
        }
    }
}


bool KICAD_MANAGER_FRAME::canCloseWindow( wxCloseEvent& aEvent )
{
    KICAD_SETTINGS* settings = kicadSettings();
    settings->m_OpenProjects = GetSettingsManager()->GetOpenProjects();

    for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
    {
        wxWindow* page = m_notebook->GetPage( i );

        if( PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( page ) )
        {
            if( !panel->GetCanClose() )
                return false;
        }
    }

    // CloseProject will recursively ask all the open editors if they need to save changes.
    // If any of them cancel then we need to cancel closing the KICAD_MANAGER_FRAME.
    if( CloseProject( true ) )
    {
        // Don't propagate event to frames which have already been closed
        aEvent.StopPropagation();

        return true;
    }
    else
    {
        if( aEvent.CanVeto() )
            aEvent.Veto();

        return false;
    }
}


void KICAD_MANAGER_FRAME::doCloseWindow()
{
#ifdef _WINDOWS_
    // For some obscure reason, on Windows, when killing Kicad from the Windows task manager
    // if a editor frame (schematic, library, board editor or fp editor) is open and has
    // some edition to save, OnCloseWindow is run twice *at the same time*, creating race
    // conditions between OnCloseWindow() code.
    // Therefore I added (JPC) a ugly hack to discard the second call (unwanted) during
    // execution of the first call (only one call is right).
    // Note also if there is no change made in editors, this behavior does not happen.
    static std::atomic<unsigned int> lock_close_event( 0 );

    if( ++lock_close_event > 1 )    // Skip extra calls
    {
        return;
    }
#endif

    m_projectTreePane->Show( false );
    Pgm().m_Quitting = true;

    // KiCad Next single-window shell: stop advertising ourselves as the KIWAY tab host before
    // we are destroyed, so any late editor → sibling-editor launch floats instead of calling
    // into a dead shell.
    if( Kiway().GetTabHost() == this )
        Kiway().SetTabHost( nullptr );

    Destroy();

#ifdef _WINDOWS_
    lock_close_event = 0;   // Reenable event management
#endif
}


void KICAD_MANAGER_FRAME::SaveOpenJobSetsToLocalSettings( bool aIsExplicitUserSave )
{
    PROJECT_LOCAL_SETTINGS& cfg = Prj().GetLocalSettings();

    if( !aIsExplicitUserSave && !cfg.ShouldAutoSave() )
        return;

    cfg.m_OpenJobSets.clear();

    for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
    {
        if( PANEL_JOBSET* jobset = dynamic_cast<PANEL_JOBSET*>( m_notebook->GetPage( i ) ) )
        {
            wxFileName jobsetFn( jobset->GetFilePath() );
            jobsetFn.MakeRelativeTo( Prj().GetProjectPath() );
            cfg.m_OpenJobSets.emplace_back( jobsetFn.GetFullPath() );
        }
    }

    cfg.SaveToFile( Prj().GetProjectPath() );
}


void KICAD_MANAGER_FRAME::OnExit( wxCommandEvent& event )
{
    Close( true );
}


bool KICAD_MANAGER_FRAME::CloseProject( bool aSave )
{
    if( !Kiway().PlayersClose( false ) )
        return false;

    // Players just closed; drop their now-orphaned docked tabs (single-window shell).
    PruneDeadEditorTabs();

    // Abort any in-progress background load, since the threads depend on the project not changing
    KIFACE *schface = Kiway().KiFACE( KIWAY::FACE_SCH );
    schface->CancelPreload();

    KIFACE *pcbface = Kiway().KiFACE( KIWAY::FACE_PCB );
    pcbface->CancelPreload();

    // Save the project file for the currently loaded project.
    if( m_active_project )
    {
        SETTINGS_MANAGER& mgr = Pgm().GetSettingsManager();

        if( Prj().GetLocalSettings().ShouldAutoSave() && Prj().GetProjectFile().ShouldAutoSave() )
        {
            mgr.TriggerBackupIfNeeded( NULL_REPORTER::GetInstance() );

            if( aSave )
                mgr.SaveProject();
        }

        // Ensure the Last_Save tag is at HEAD before closing. This handles the case where
        // autosave commits were made after the last explicit save - without this, the next
        // project load would offer to restore the autosave state, which is incorrect after
        // a clean close.
        wxString projPath = Prj().GetProjectPath();

        if( !projPath.IsEmpty() && Kiway().LocalHistory().HistoryExists( projPath ) )
        {
            if( Kiway().LocalHistory().HeadNewerThanLastSave( projPath ) )
            {
                // Commit the current on-disk state and tag it so Last_Save matches HEAD
                if( Kiway().LocalHistory().CommitFullProjectSnapshot( projPath, wxS( "Close" ) ) )
                {
                    Kiway().LocalHistory().TagSave( projPath, wxS( "project" ) );
                }
            }
        }

        m_active_project = false;
        // Enforce local history size limit (if enabled) once all pending saves/backups are done.
        if( Pgm().GetCommonSettings() && Pgm().GetCommonSettings()->m_Backup.enabled )
        {
            unsigned long long int limit = Pgm().GetCommonSettings()->m_Backup.limit_total_size;

            if( limit > 0 )
                Kiway().LocalHistory().EnforceSizeLimit( Prj().GetProjectPath(), (size_t) limit );
        }

        // Unregister the project saver before unloading the project to prevent
        // dangling references
        Kiway().LocalHistory().UnregisterSaver( &Prj() );

        mgr.UnloadProject( &Prj() );
    }

    SetStatusText( "" );

    // Traverse pages in reverse order so deleting them doesn't mess up our iterator.
    for( int i = (int) m_notebook->GetPageCount() - 1; i >= 0; i-- )
    {
        wxWindow* page = m_notebook->GetPage( i );

        if( PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( page ) )
        {
            if( panel->GetProjectTied() )
                m_notebook->DeletePage( i );
        }
    }

    m_projectTreePane->EmptyTreePrj();
    HideTabsIfNeeded();

    return true;
}


void KICAD_MANAGER_FRAME::OpenJobsFile( const wxFileName& aFileName, bool aCreate, bool aResaveProjectPreferences )
{
    for( size_t i = 0; i < m_notebook->GetPageCount(); i++ )
    {
        if( PANEL_JOBSET* panel = dynamic_cast<PANEL_JOBSET*>( m_notebook->GetPage( i ) ) )
        {
            if( aFileName.GetFullPath() == panel->GetFilePath() )
            {
                m_notebook->SetSelection( i );
                return;
            }
        }
    }

    try
    {
        std::unique_ptr<JOBSET> jobsFile = std::make_unique<JOBSET>( aFileName.GetFullPath().ToStdString() );

        jobsFile->LoadFromFile();

        if( aCreate && !aFileName.FileExists() )
        {
            JOBSET_DESTINATION* dest = jobsFile->AddNewDestination( JOBSET_DESTINATION_T::FOLDER );
            dest->m_outputHandler->SetOutputPath( aFileName.GetName() );
            jobsFile->SaveToFile( wxEmptyString, true );
        }

        PANEL_JOBSET* jobPanel = new PANEL_JOBSET( m_notebook, this, std::move( jobsFile ) );
        jobPanel->SetProjectTied( true );
        jobPanel->SetClosable( true );
        m_notebook->AddPage( jobPanel, aFileName.GetFullName(), true );
        HideTabsIfNeeded();

        if( aResaveProjectPreferences )
            SaveOpenJobSetsToLocalSettings();
    }
    catch( ... )
    {
        DisplayErrorMessage( this, _( "Error opening jobs file" ) );
    }
}


bool KICAD_MANAGER_FRAME::LoadProject( const wxFileName& aProjectFileNameIn )
{
    wxFileName aProjectFileName( aProjectFileNameIn );

    // Anvil dual-extension: MRU / session-restore / drag-drop may reference the sibling
    // extension of the project file actually on disk.  Heal before validating.
    if( !aProjectFileName.FileExists() )
    {
        wxFileName sibling( aProjectFileName );

        if( aProjectFileName.GetExt() == FILEEXT::ProjectFileExtension )
            sibling.SetExt( FILEEXT::AnvilProjectFileExtension );
        else if( aProjectFileName.GetExt() == FILEEXT::AnvilProjectFileExtension )
            sibling.SetExt( FILEEXT::ProjectFileExtension );

        if( sibling.GetFullPath() != aProjectFileName.GetFullPath() && sibling.FileExists() )
            aProjectFileName = sibling;
    }

    // The project file should be valid by the time we get here or something has gone wrong.
    if( !aProjectFileName.Exists() )
        return false;

    wxString fullPath = aProjectFileName.GetFullPath();

    // Check if a lock file already exists BEFORE we try to acquire it. We only want to warn
    // the user if the lock file pre-existed, not if we're about to create it ourselves.
    // The actual lock acquisition happens in SETTINGS_MANAGER::LoadProject().
    wxFileName lockFn( fullPath );
    lockFn.SetName( FILEEXT::LockFilePrefix + lockFn.GetName() );
    lockFn.SetExt( lockFn.GetExt() + wxS( "." ) + FILEEXT::LockFileExtension );
    bool lockFilePreExisted = lockFn.FileExists();

    bool lockOverrideGranted = false;

    if( lockFilePreExisted )
    {
        // A lock file exists. Create a LOCKFILE to read who owns it and decide what to do.
        LOCKFILE lockFile( fullPath );

        if( !lockFile.Valid() && lockFile.IsLockedByMe() )
        {
            // If we cannot acquire the lock but we appear to be the one who locked it, check to
            // see if there is another KiCad instance running. If not, then we can override the
            // lock. This could happen if KiCad crashed or was interrupted.
            if( !Pgm().SingleInstance()->IsAnotherRunning() )
                lockFile.OverrideLock();
        }

        if( !lockFile.Valid() )
        {
            wxString msg;
            msg.Printf( _( "Project '%s' is already open by '%s' at '%s'." ),
                        fullPath,
                        lockFile.GetUsername(),
                        lockFile.GetHostname() );

            if( !AskOverrideLock( this, msg ) )
                return false;  // User clicked Cancel - abort project loading entirely

            lockFile.OverrideLock();
            lockOverrideGranted = true;
        }

        // The LOCKFILE goes out of scope here and releases/removes the lock file.
        // SETTINGS_MANAGER::LoadProject() will create the actual persistent lock.
    }

    // Any open KIFACE's must be closed if they are not part of the new project.
    // (We never want a KIWAY_PLAYER open on a KIWAY that isn't in the same project.)
    // User is prompted here to close those KIWAY_PLAYERs:
    if( !CloseProject( true ) )
        return false;

    m_active_project = true;

    // NB: when loading a legacy project SETTINGS_MANAGER::LoadProject() will convert it to
    // current extension. Be very careful with aProjectFileName vs. Prj().GetProjectPath()
    // from here on out.

    Pgm().GetSettingsManager().LoadProject( fullPath );

    // Propagate lock override decision to the loaded project
    if( lockOverrideGranted )
        Prj().SetLockOverrideGranted( true );

    LoadWindowState( aProjectFileName.GetFullName() );

    if( aProjectFileName.IsDirWritable() )
        SetMruPath( Prj().GetProjectPath() );

    Kiway().LocalHistory().Init( Prj().GetProjectPath() );

    if( Kiway().LocalHistory().HeadNewerThanLastSave( Prj().GetProjectPath() ) )
    {
        wxString head = Kiway().LocalHistory().GetHeadHash( Prj().GetProjectPath() );

        KICAD_MESSAGE_DIALOG dlg( this, _( "KiCad found unsaved changes from your last session that are newer than "
                                           "the saved project files." ),
                                  _( "Recover Unsaved Changes" ), wxYES_NO | wxICON_QUESTION );

        dlg.SetExtendedMessage( _( "This can happen if your previous session ended unexpectedly.\n\n"
                                   "Choose 'Restore' to recover those changes, or 'Discard' to keep the "
                                   "currently saved files." ) );

        dlg.SetYesNoLabels( _( "Restore" ), _( "Discard" ) );

        if( dlg.ShowModal() == wxID_YES )
        {
            Kiway().LocalHistory().RestoreCommit( Prj().GetProjectPath(), head, this );
        }
        else
        {
            // User declined to restore - commit the current on-disk state and tag it
            // so we don't prompt again on next load
            if( Kiway().LocalHistory().CommitFullProjectSnapshot( Prj().GetProjectPath(), wxS( "Declined restore" ) ) )
                Kiway().LocalHistory().TagSave( Prj().GetProjectPath(), wxS( "project" ) );
        }
    }

    // Save history & window state to disk now.  Don't wait around for a crash.
    KICAD_SETTINGS* settings = kicadSettings();
    SaveSettings( settings );
    settings->SaveToFile( Pgm().GetSettingsManager().GetPathForSettingsFile( settings ) );

    m_projectTreePane->ReCreateTreePrj();
    m_historyPane->RefreshHistory( Prj().GetProjectPath() );

    for( const wxString& jobset : Prj().GetLocalSettings().m_OpenJobSets )
    {
        wxFileName jobsetFn( jobset );
        jobsetFn.MakeAbsolute( Prj().GetProjectPath() );

        if( jobsetFn.Exists() )
            OpenJobsFile( jobsetFn.GetFullPath(), false, false );
    }

    // Always start with the apps page
    m_notebook->SetSelection( 0 );

    // Rebuild the list of watched paths.
    // however this is possible only when the main loop event handler is running,
    // so we use it to run the rebuild function.
    wxCommandEvent cmd( wxEVT_COMMAND_MENU_SELECTED, ID_INIT_WATCHED_PATHS );

    wxPostEvent( this, cmd );

    PrintPrjInfo();

    KIPLATFORM::APP::RegisterApplicationRestart( aProjectFileName.GetFullPath() );
    m_openSavedWindows = true;

    KIPLATFORM::ENV::AddToRecentDocs( aProjectFileName.GetFullPath() );

    // Now that we have a new project, trigger a library preload, which will load in any
    // project-specific symbol and footprint libraries into the manager
    PreloadAllLibraries();
    return true;
}


void KICAD_MANAGER_FRAME::CreateNewProject( const wxFileName& aProjectFileName, bool aCreateStubFiles )
{
    wxCHECK_RET( aProjectFileName.DirExists() && aProjectFileName.IsDirWritable(),
                 "Project folder must exist and be writable to create a new project." );

    // If the project is legacy, convert it
    if( !aProjectFileName.FileExists() )
    {
        wxFileName legacyPro( aProjectFileName );
        legacyPro.SetExt( FILEEXT::LegacyProjectFileExtension );

        if( legacyPro.FileExists() )
        {
            GetSettingsManager()->LoadProject( legacyPro.GetFullPath() );
            GetSettingsManager()->SaveProject();

            wxRemoveFile( legacyPro.GetFullPath() );
        }
        else
        {
            // Copy template project file from template folder.
            wxString srcFileName = sys_search().FindValidPath( "kicad.kicad_pro" );

            wxFileName destFileName( aProjectFileName );
            destFileName.SetExt( FILEEXT::ProjectFileExtension );

            // Create a minimal project file if the template project file could not be copied
            if( !wxFileName::FileExists( srcFileName )
                || !wxCopyFile( srcFileName, destFileName.GetFullPath() ) )
            {
                wxFFile file( destFileName.GetFullPath(), "wb" );

                if( file.IsOpened() )
                    file.Write( wxT( "{\n}\n") );

                // wxFFile dtor will close the file
            }
        }
    }

    // Create a "stub" for a schematic root sheet and a board if requested.
    // It will avoid messages from the schematic editor or the board editor to create a new file
    // And forces the user to create main files under the right name for the project manager
    if( aCreateStubFiles )
    {
        const bool anvilProj = aProjectFileName.GetExt() == FILEEXT::AnvilProjectFileExtension;

        wxFileName fn( aProjectFileName.GetFullPath() );
        fn.SetExt( anvilProj ? FILEEXT::AnvilSchematicFileExtension
                             : FILEEXT::KiCadSchematicFileExtension );

        wxFileName altSch( fn );
        altSch.SetExt( anvilProj ? FILEEXT::KiCadSchematicFileExtension
                                 : FILEEXT::AnvilSchematicFileExtension );

        // If no root schematic exists under either extension, create a "stub" file ( minimal
        // schematic file ).
        if( !fn.FileExists() && !altSch.FileExists() )
        {
            wxFFile file( fn.GetFullPath(), "wb" );

            if( file.IsOpened() )
            {
                file.Write( wxString::Format( "(kicad_sch\n"
                                              "\t(version %d)\n"
                                              "\t(generator \"eeschema\")\n"
                                              "\t(generator_version \"%s\")\n"
                                              "\t(uuid %s)\n"
                                              "\t(paper \"A4\")\n"
                                              "\t(lib_symbols)\n"
                                              "\t(sheet_instances\n"
                                              "\t\t(path \"/\"\n"
                                              "\t\t\t(page \"1\")\n"
                                              "\t\t)\n"
                                              "\t)\n"
                                              "\t(embedded_fonts no)\n"
                                              ")",
                                              SEXPR_SCHEMATIC_FILE_VERSION,
                                              GetMajorMinorVersion(),
                                              KIID().AsString() ) );
            }

            // wxFFile dtor will close the file
        }

        // If a <project>.kicad_pcb or <project>.brd file does not exist,
        // create a .kicad_pcb "stub" file
        fn.SetExt( anvilProj ? FILEEXT::AnvilPcbFileExtension : FILEEXT::KiCadPcbFileExtension );
        wxFileName altPcb( fn );
        altPcb.SetExt( anvilProj ? FILEEXT::KiCadPcbFileExtension : FILEEXT::AnvilPcbFileExtension );
        wxFileName leg_fn( fn );
        leg_fn.SetExt( FILEEXT::LegacyPcbFileExtension );

        if( !fn.FileExists() && !altPcb.FileExists() && !leg_fn.FileExists() )
        {
            wxFFile file( fn.GetFullPath(), "wb" );

            if( file.IsOpened() )
            {
                // Create a small dummy file as a stub for pcbnew:
                file.Write( wxString::Format( "(kicad_pcb (version %d) (generator \"pcbnew\") (generator_version \"%s\")\n)",
                                              SEXPR_BOARD_FILE_VERSION, GetMajorMinorVersion() ) );
            }

            // wxFFile dtor will close the file
        }
    }

    // Save history & window state to disk now.  Don't wait around for a crash.
    KICAD_SETTINGS* settings = kicadSettings();
    SaveSettings( settings );
    settings->SaveToFile( Pgm().GetSettingsManager().GetPathForSettingsFile( settings ) );

    m_openSavedWindows = true;
}


void KICAD_MANAGER_FRAME::OnOpenFileInTextEditor( wxCommandEvent& event )
{
    // show all files in file dialog (in Kicad all files are editable texts):
    wxString wildcard = FILEEXT::AllFilesWildcard();

    wxString default_dir = Prj().GetProjectPath();

    wxFileDialog dlg( this, _( "Edit File in Text Editor" ), default_dir,  wxEmptyString, wildcard,
                      wxFD_OPEN );

    KIPLATFORM::UI::AllowNetworkFileSystems( &dlg );

    if( dlg.ShowModal() == wxID_CANCEL )
        return;

    wxString filename = dlg.GetPath();

    if( !dlg.GetPath().IsEmpty() && !Pgm().GetTextEditor().IsEmpty() )
        m_toolManager->RunAction<wxString*>( KICAD_MANAGER_ACTIONS::openTextEditor, &filename );
}


void KICAD_MANAGER_FRAME::OnEditAdvancedCfg( wxCommandEvent& WXUNUSED( event ) )
{
    DIALOG_EDIT_CFG dlg( this );
    dlg.ShowModal();
}


void KICAD_MANAGER_FRAME::RefreshProjectTree()
{
    m_projectTreePane->ReCreateTreePrj();
}


void KICAD_MANAGER_FRAME::ShowChangedLanguage()
{
    // call my base class
    EDA_BASE_FRAME::ShowChangedLanguage();

    // tooltips in toolbars
    RecreateToolbars();
    m_launcher->CreateLaunchers();

    // update captions
    int pageId = m_notebook->FindPage( m_launcher );

    if( pageId != wxNOT_FOUND )
        m_notebook->SetPageText( pageId, EDITORS_CAPTION );

    m_auimgr.GetPane( m_projectTreePane ).Caption( PROJECT_FILES_CAPTION );
    m_auimgr.Update();

    m_projectTreePane->FileWatcherReset();

    PrintPrjInfo();
}


void KICAD_MANAGER_FRAME::CommonSettingsChanged( int aFlags )
{
    EDA_BASE_FRAME::CommonSettingsChanged( aFlags );

    if( m_pcm && ( aFlags & ENVVARS_CHANGED ) )
        m_pcm->ReadEnvVar();

    COMMON_SETTINGS* settings = Pgm().GetCommonSettings();

    if( m_lastToolbarIconSize == 0
        || m_lastToolbarIconSize != settings->m_Appearance.toolbar_icon_size )
    {
        onToolbarSizeChanged();
        m_lastToolbarIconSize = settings->m_Appearance.toolbar_icon_size;
    }

    m_projectTreePane->ReCreateTreePrj();
}


void KICAD_MANAGER_FRAME::ProjectChanged()
{
    wxString file = GetProjectFileName();

    // empty file string means no project loaded
    if( !Prj().IsNullProject() &&
        Prj().GetProjectLock() == nullptr )
    {
        LOCKFILE lockFile( file );

        if( !lockFile.Valid() && lockFile.IsLockedByMe() )
        {
            // If we cannot acquire the lock but we appear to be the one who
            // locked it, check to see if there is another KiCad instance running.
            // If there is not, then we can override the lock.  This could happen if
            // KiCad crashed or was interrupted
            if( !Pgm().SingleInstance()->IsAnotherRunning() )
            {
                lockFile.OverrideLock();
            }
        }

        if( !lockFile.Valid() )
        {
            wxString msg;
            msg.Printf( _( "Project '%s' is already open by '%s' at '%s'." ),
                        file,
                        lockFile.GetUsername(),
                        lockFile.GetHostname() );

            if( AskOverrideLock( this, msg ) )
            {
                lockFile.OverrideLock();
            }
        }

        Prj().SetReadOnly( !lockFile.Valid() || Prj().GetProjectFile().IsReadOnly() );
        Prj().SetProjectLock( new LOCKFILE( std::move( lockFile ) ) );
    }

    wxString title;

    if( !file.IsEmpty() )
    {
        wxFileName fn( file );

        title = fn.GetName();

        if( Prj().IsReadOnly() )
            title += wxS( " " ) + _( "[Read Only]" );
    }
    else
    {
        title = _( "[no project loaded]" );
    }

    if( ADVANCED_CFG::GetCfg().m_HideVersionFromTitle )
        title += wxT( " \u2014 " ) + wxString( wxS( "Anvil" ) );
    else
        title += wxT( " \u2014 " ) + wxString( wxS( "Anvil " ) ) + GetMajorMinorVersion();

    SetTitle( title );

    // Register project file saver. Ensures project file participates in
    // autosave history commits without affecting dirty state.
    Kiway().LocalHistory().RegisterSaver( &Prj(),
            [this]( const wxString& aProjectPath, std::vector<HISTORY_FILE_DATA>& aFileData )
            {
                Prj().SaveToHistory( aProjectPath, aFileData );
            } );
}


void KICAD_MANAGER_FRAME::LoadSettings( APP_SETTINGS_BASE* aCfg )
{
    EDA_BASE_FRAME::LoadSettings( aCfg );

    auto settings = dynamic_cast<KICAD_SETTINGS*>( aCfg );

    wxCHECK( settings, /*void*/ );

    m_leftWinWidth = settings->m_LeftWinWidth;
    m_showHistoryPanel = settings->m_ShowHistoryPanel;
}


void KICAD_MANAGER_FRAME::SaveSettings( APP_SETTINGS_BASE* aCfg )
{
    EDA_BASE_FRAME::SaveSettings( aCfg );

    KICAD_SETTINGS* settings = dynamic_cast<KICAD_SETTINGS*>( aCfg );

    wxCHECK( settings, /*void*/ );

    settings->m_LeftWinWidth = m_projectTreePane->GetSize().x;
    settings->m_ShowHistoryPanel = m_historyPane && m_auimgr.GetPane( m_historyPane ).IsShown();

    if( !m_isClosing )
        settings->m_OpenProjects = GetSettingsManager()->GetOpenProjects();
}


void KICAD_MANAGER_FRAME::OfferAiImportCleanup( const wxString& aWhat )
{
    if( !m_aiChatPanel )
        return;

    KIDIALOG dlg( this,
                  wxString::Format( _( "%s imported.  Imported designs often need small fixes "
                                       "(missing power flags, unconnected pins, layout "
                                       "leftovers).  Review it with Anvil AI?" ), aWhat ),
                  _( "Import Complete" ), wxYES_NO | wxICON_INFORMATION );
    dlg.SetYesNoLabels( _( "Review with Anvil AI" ), _( "Not now" ) );
    dlg.DoNotShowCheckbox( __FILE__, __LINE__ );

    if( dlg.ShowModal() != wxID_YES )
        return;

    if( !AiChatPanelShown() )
        ToggleAiChat();

    const wxString prompt =
            _( "This design was just imported from another EDA tool. Run ERC, then fix what it "
               "reports - add power flags for undriven rails, wire or no-connect unconnected "
               "pins, and delete stray wires/labels. Loop until ERC is clean, then summarise "
               "what you changed." );

    wxString script = wxS( "if(window.anvilSuggestPrompt)window.anvilSuggestPrompt(" )
                      + wxString::FromUTF8( nlohmann::json(
                                std::string( prompt.utf8_str() ) ).dump() )
                      + wxS( ");" );

    WEBVIEW_PANEL* panel = m_aiChatPanel;
    panel->CallAfter( [panel, script]() { panel->RunScriptAsync( script ); } );
}


void KICAD_MANAGER_FRAME::PrintPrjInfo()
{
    // wxStatusBar's wxELLIPSIZE_MIDDLE flag doesn't work (at least on Mac).

    wxString     status = wxString::Format( _( "Project: %s" ), Prj().GetProjectFullName() );
    KISTATUSBAR* statusBar = static_cast<KISTATUSBAR*>( GetStatusBar() );
    statusBar->SetEllipsedTextField( status, 0 );
}


bool KICAD_MANAGER_FRAME::IsProjectActive()
{
    return m_active_project;
}


void KICAD_MANAGER_FRAME::OnIdle( wxIdleEvent& aEvent )
{
    /**
     * We start loading the saved previously open windows on idle to avoid locking up the GUI
     * earlier in project loading. This gives us the visual effect of a opened KiCad project but
     * with a "busy" progress reporter
     */
    if( !m_openSavedWindows )
        return;

    m_openSavedWindows = false;

    if( Pgm().GetCommonSettings()->m_Session.remember_open_files )
    {
        int previousOpenCount = std::count_if( Prj().GetLocalSettings().m_files.begin(),
                                               Prj().GetLocalSettings().m_files.end(),
                [&]( const PROJECT_FILE_STATE& f )
                {
                    return !f.fileName.EndsWith( FILEEXT::ProjectFileExtension ) && f.open;
                } );

        if( previousOpenCount > 0 )
        {
            APP_PROGRESS_DIALOG progressReporter( _( "Restoring session" ), wxEmptyString, previousOpenCount, this );

            // We don't currently support opening more than one view per file
            std::set<wxString> openedFiles;

            int i = 0;

            for( const PROJECT_FILE_STATE& file : Prj().GetLocalSettings().m_files )
            {
                if( file.open && !openedFiles.count( file.fileName ) )
                {
                    progressReporter.Update( i++, wxString::Format( _( "Restoring '%s'" ), file.fileName ) );

                    openedFiles.insert( file.fileName );
                    wxFileName fn( file.fileName );

                    if( fn.GetExt() == FILEEXT::LegacySchematicFileExtension
                        || fn.GetExt() == FILEEXT::KiCadSchematicFileExtension
                        || fn.GetExt() == FILEEXT::AnvilSchematicFileExtension )
                    {
                        GetToolManager()->RunAction( KICAD_MANAGER_ACTIONS::editSchematic );
                    }
                    else if( fn.GetExt() == FILEEXT::LegacyPcbFileExtension
                             || fn.GetExt() == FILEEXT::KiCadPcbFileExtension
                             || fn.GetExt() == FILEEXT::AnvilPcbFileExtension )
                    {
                        GetToolManager()->RunAction( KICAD_MANAGER_ACTIONS::editPCB );
                    }
                }

                wxYield();
            }
        }
    }

    // clear file states regardless if we opened windows or not due to setting
    Prj().GetLocalSettings().ClearFileState();

    // After restore from history, mark open editors as dirty so user is prompted to save
    if( m_restoredFromHistory )
    {
        m_restoredFromHistory = false;

        // Mark schematic editor as dirty if open
        if( KIWAY_PLAYER* schFrame = Kiway().Player( FRAME_SCH, false ) )
            schFrame->OnModify();

        // Mark PCB editor as dirty if open
        if( KIWAY_PLAYER* pcbFrame = Kiway().Player( FRAME_PCB_EDITOR, false ) )
            pcbFrame->OnModify();
    }

    KICAD_SETTINGS* settings = kicadSettings();

    if( KIPLATFORM::POLICY::GetPolicyBool( POLICY_KEY_PCM ) != KIPLATFORM::POLICY::PBOOL::DISABLED
        && settings->m_PcmUpdateCheck )
    {
        if( !m_pcm )
            CreatePCM();

        m_pcm->RunBackgroundUpdate();
    }

#ifdef KICAD_UPDATE_CHECK
    if( !m_updateManager && settings->m_KiCadUpdateCheck )
    {
        m_updateManager = std::make_unique<UPDATE_MANAGER>();
        m_updateManager->CheckForUpdate( this );
    }
#endif

    // This little diddy is needed to get the window put into the Mac dock icon's context menu.
    Raise();
}


void KICAD_MANAGER_FRAME::SetPcmButton( BITMAP_BUTTON* aButton )
{
    m_pcmButton = aButton;

    updatePcmButtonBadge();
}


void KICAD_MANAGER_FRAME::updatePcmButtonBadge()
{
    if( m_pcmButton )
    {
        if( m_pcmUpdateCount > 0 )
        {
            m_pcmButton->SetShowBadge( true );
            m_pcmButton->SetBadgeText( wxString::Format( "%d", m_pcmUpdateCount ) );
        }
        else
        {
            m_pcmButton->SetShowBadge( false );
        }

        m_pcmButton->Refresh();
    }
}


void KICAD_MANAGER_FRAME::onToolbarSizeChanged()
{
    // No idea why, but the same mechanism used in EDA_DRAW_FRAME doesn't work here
    // the only thing that seems to work is to blow it all up and start from scratch.
    m_auimgr.DetachPane( m_tbLeft );
    delete m_tbLeft;
    m_tbLeft = nullptr;
    RecreateToolbars();
    m_auimgr.AddPane( m_tbLeft, EDA_PANE().HToolbar().Name( "TopMainToolbar" ).Left().Layer( 2 ).Hide() );

    m_auimgr.Update();
}


void KICAD_MANAGER_FRAME::buildTitleBarMenuButtons()
{
#ifdef __WXMSW__
    if( !m_titleBar )
        return;

    wxMenuBar* bar = GetMenuBar();
    m_titleBar->SetMenus( bar );    // takes ownership of the menus (Remove()s each)

    // The menus now live in the custom title bar; drop the (now-empty) native menu bar
    // so Windows does not also render a native menu row beneath the caption.
    if( bar )
    {
        // SetMenuBar() only detaches, it does not free, and buildCommonMenuBarFrom()'s
        // `delete oldMenuBar` then sees nullptr -- so without this the emptied bar leaked
        // once per tab switch. Its menus already belong to the title bar, so this frees
        // only the now-empty shell.
        SetMenuBar( nullptr );
        delete bar;
    }

    if( m_auimgr.GetManagedWindow() )
        m_auimgr.Update();
#endif
}


#ifdef __WXMSW__
WXLRESULT KICAD_MANAGER_FRAME::MSWWindowProc( WXUINT message, WXWPARAM wParam, WXLPARAM lParam )
{
    HWND hwnd = static_cast<HWND>( GetHandle() );

    switch( message )
    {
    case WM_NCCALCSIZE:
        // Returning 0 for the "compute client size" pass makes the client area span the
        // entire window, which removes the native title bar. (Never DefWindowProc here —
        // that re-adds the caption and was the cause of the previous double-title-bar.)
        if( wParam == TRUE )
            return 0;

        break;

    case WM_NCHITTEST:
    {
        const int sx = GET_X_LPARAM( lParam );
        const int sy = GET_Y_LPARAM( lParam );

        RECT  wr;
        ::GetWindowRect( hwnd, &wr );

        POINT pt = { sx, sy };
        ::ScreenToClient( hwnd, &pt );

        // Resize borders (only when not maximized).
        if( !::IsZoomed( hwnd ) )
        {
            const int bx = ::GetSystemMetrics( SM_CXFRAME ) + ::GetSystemMetrics( SM_CXPADDEDBORDER );
            const int by = ::GetSystemMetrics( SM_CYFRAME ) + ::GetSystemMetrics( SM_CXPADDEDBORDER );

            enum { L = 1, R = 2, T = 4, B = 8 };
            int m = ( sx <  wr.left  + bx ? L : 0 ) | ( sx >= wr.right  - bx ? R : 0 )
                  | ( sy <  wr.top   + by ? T : 0 ) | ( sy >= wr.bottom - by ? B : 0 );

            switch( m )
            {
            case T | L: return HTTOPLEFT;     case T: return HTTOP;     case T | R: return HTTOPRIGHT;
            case L:     return HTLEFT;                                  case R:     return HTRIGHT;
            case B | L: return HTBOTTOMLEFT;  case B: return HTBOTTOM;  case B | R: return HTBOTTOMRIGHT;
            default: break;
            }
        }

        // The title-bar strip (minus its buttons) is the draggable caption: this gives
        // window move, double-click-maximize and Aero Snap natively.
        const int  tbH = m_titleBar ? m_titleBar->GetSize().GetHeight() : 0;
        const bool overButton = m_titleBar && m_titleBar->HitInteractive( wxPoint( pt.x, pt.y ) );

        if( pt.y >= 0 && pt.y < tbH && !overButton )
            return HTCAPTION;

        return HTCLIENT;
    }

    case WM_GETMINMAXINFO:
    {
        // Constrain maximize to the monitor work area so the taskbar stays visible and
        // content is not clipped (the window has no native frame to account for).
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>( lParam );

        if( HMONITOR mon = ::MonitorFromWindow( hwnd, MONITOR_DEFAULTTONEAREST ) )
        {
            MONITORINFO mi;
            mi.cbSize = sizeof( mi );

            if( ::GetMonitorInfo( mon, &mi ) )
            {
                mmi->ptMaxPosition.x  = mi.rcWork.left   - mi.rcMonitor.left;
                mmi->ptMaxPosition.y  = mi.rcWork.top    - mi.rcMonitor.top;
                mmi->ptMaxSize.x      = mi.rcWork.right  - mi.rcWork.left;
                mmi->ptMaxSize.y      = mi.rcWork.bottom - mi.rcWork.top;
                mmi->ptMaxTrackSize.x = mmi->ptMaxSize.x;
                mmi->ptMaxTrackSize.y = mmi->ptMaxSize.y;
                return 0;
            }
        }

        break;
    }

    default:
        break;
    }

    // Preserve EDA_BASE_FRAME's Alt-key (SC_KEYMENU) workaround — its handler is private,
    // so we replicate the one line here — then defer to wxFrame.
    if( message == WM_SYSCOMMAND && wParam == SC_KEYMENU && ( lParam >> 16 ) <= 0 )
        return 0;

    return wxFrame::MSWWindowProc( message, wParam, lParam );
}
#endif // __WXMSW__


void KICAD_MANAGER_FRAME::ToggleLocalHistory()
{
    wxAuiPaneInfo& pane = m_auimgr.GetPane( m_historyPane );
    bool show = !pane.IsShown();
    pane.Show( show );

    if( show )
        m_historyPane->RefreshHistory( Prj().GetProjectPath() );

    m_auimgr.Update();
}


void KICAD_MANAGER_FRAME::RestoreCommitFromHistory( const wxString& aHash )
{
    if( !Kiway().PlayersClose( true ) )
        return;

    if( Kiway().LocalHistory().RestoreCommit( Prj().GetProjectPath(), aHash, this ) )
    {
        m_restoredFromHistory = true;  // Mark editors dirty when they reopen
    }

    m_projectTreePane->ReCreateTreePrj();
    m_openSavedWindows = true;
    m_historyPane->RefreshHistory( Prj().GetProjectPath() );
}


bool KICAD_MANAGER_FRAME::HistoryPanelShown()
{
    return m_historyPane && m_auimgr.GetPane( m_historyPane ).IsShown();
}


void KICAD_MANAGER_FRAME::ToggleProjectExplorer()
{
    if( !m_projectTreePane )
        return;

    wxAuiPaneInfo& pane = m_auimgr.GetPane( m_projectTreePane );
    pane.Show( !pane.IsShown() );
    m_auimgr.Update();
}


bool KICAD_MANAGER_FRAME::ProjectExplorerShown()
{
    return m_projectTreePane && m_auimgr.GetPane( m_projectTreePane ).IsShown();
}


void KICAD_MANAGER_FRAME::SplitActiveEditor()
{
    // Move the active editor tab into a new side-by-side group (VS Code "split editor").
    // Needs a second tab to split into: every docked editor is a unique reparented frame,
    // so the same editor cannot be shown in both halves — splitting a lone tab is a no-op.
    if( !m_editorTabs || m_editorTabs->GetPageCount() < 2 )
        return;

    int sel = m_editorTabs->GetSelection();

    if( sel != wxNOT_FOUND )
        m_editorTabs->Split( static_cast<size_t>( sel ), wxRIGHT );
}
