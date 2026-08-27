/*
 * This program source code file is part of Anvil, a free EDA CAD application.
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
#include "project_tree.h"
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
#include <widgets/wx_progress_reporters.h>
#include <wx/msgdlg.h>
#include <eda_base_frame.h>
#include <executable_names.h>
#include <file_history.h>
#include <local_history.h>
#include <policy_keys.h>
#include <gestfich.h>
#include <kiplatform/app.h>
#include <kiplatform/anvil_theme.h>
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
#include <tool/actions.h>
#include <wx/time.h>
#include <tools/kicad_manager_actions.h>
#include <tools/kicad_manager_control.h>
#include <toolbars_kicad_manager.h>
#include <wildcards_and_files_ext.h>
#include <widgets/app_progress_dialog.h>
#include <widgets/kistatusbar.h>
#include <widgets/bitmap_button.h>
#include <tool/common_tools.h>
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
#include <anvil_ai/anvil_ai_agent.h>       // native Claude agent driving the shell AI panel
#include <anvil_ai/anvil_ai_tool_server.h> // MCP tool socket (external Claude clients)
#include <nlohmann/json.hpp>               // parse the open_project IPC payload
#include <paths.h>                         // PATHS::GetStockDataPath (locate chat.html)
#include <anvil_auth/anvil_auth.h>         // sign-out (File > Sign Out)
#include <dialogs/dialog_anvil_login.h>    // re-login after sign-out
#include <wx/stdpaths.h>
#include <wx/file.h>                       // read ipc_port.txt for the AI IPC client
#include <wx/utils.h>                      // wxGetEnv / wxGetHomeDir (IPC port discovery)
#include <wx/filename.h>
#include <wx/webview.h>                    // wxWebView / wxEVT_WEBVIEW_LOADED
#include <wx/datetime.h>                   // wxDateTime::Now (chat.html cache-buster)
#include <wx/panel.h>
#include <wx/display.h>                     // keep title-bar popups inside the monitor
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/grid.h>                        // in-shell CSV / BOM viewer tab
#include <wx/textfile.h>                    // read CSV lines for the viewer tab
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
#include <widgets/ui_common.h>


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
        const ADVANCED_CFG& acfg = ADVANCED_CFG::GetCfg();
        wxFont rowFont    = GetFont();
        bool   rowChanged = false;

        if( acfg.m_AnvilUiFontPt > 0.0 )
        {
            rowFont.SetFractionalPointSize( acfg.m_AnvilUiFontPt );
            rowChanged = true;
        }

        // Brand face only when it is installed — SetFaceName() invalidates the font otherwise.
        if( KIUI::ApplyFontFace( rowFont, acfg.m_AnvilUiFontFace ) )
            rowChanged = true;

        if( rowChanged )
            SetFont( rowFont );

        buildRows();
        computeLayout();

        Bind( wxEVT_PAINT, &ANVIL_POPUP_MENU::onPaint, this );
        Bind( wxEVT_MOTION, &ANVIL_POPUP_MENU::onMotion, this );
        Bind( wxEVT_LEFT_UP, &ANVIL_POPUP_MENU::onClick, this );
        Bind( wxEVT_LEAVE_WINDOW,
              [this]( wxMouseEvent& ) { if( !m_child ) { m_hover = -1; Refresh(); } } );

        // Soft drop shadow (like a native menu) so the flat emerald popup reads as floating
        // above the window instead of painted onto it.  Must be set before the first Popup().
        KIPLATFORM::UI::AddDropShadow( this );

        // A transient popup only auto-dismisses on a click-outside INSIDE this application.
        // When the user switches to another app (Alt-Tab, clicking another window) MSW merely
        // drops the mouse capture, which left the menu floating on top of the other program.
        // Watch the owning frame's activation from the root popup and drop the whole chain the
        // moment the frame is deactivated — exactly what a native menu does.
        if( !m_parentPopup )
        {
            if( wxWindow* top = wxGetTopLevelParent( aParent ) )
            {
                m_activationSource = top;
                top->Bind( wxEVT_ACTIVATE, &ANVIL_POPUP_MENU::onFrameActivate, this );
            }
        }
    }

    ~ANVIL_POPUP_MENU() override
    {
        if( m_activationSource )
            m_activationSource->Unbind( wxEVT_ACTIVATE, &ANVIL_POPUP_MENU::onFrameActivate, this );
    }

    /// Pop the menu so its top-left sits at screen point @p aScreenPos.
    void PopupAt( const wxPoint& aScreenPos )
    {
        // Keep the whole menu on the monitor.  The title-bar buttons that pop these menus sit
        // right against the window's right edge, so a menu anchored at the button's left corner
        // runs past the screen and its labels (e.g. the account email) get clipped.
        wxPoint      pos = aScreenPos;
        const wxSize sz  = GetSize();

        const int    idx  = wxDisplay::GetFromPoint( aScreenPos );
        const wxRect area = wxDisplay( idx == wxNOT_FOUND ? 0u : (unsigned) idx ).GetClientArea();

        pos.x = std::max( area.GetLeft(), std::min( pos.x, area.GetRight() - sz.x ) );
        pos.y = std::max( area.GetTop(), std::min( pos.y, area.GetBottom() - sz.y ) );

        Move( pos );
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

        const wxColour& bg     = ANVIL::POPUP_BG;   // NEMI dark-emerald popup
        const wxColour& border = ANVIL::BORDER;     // NEMI emerald edge
        const wxColour& hover  = ANVIL::ACCENT;     // NEMI Signal Emerald
        const wxColour& text   = ANVIL::BONE;       // NEMI Bone
        const wxColour& dim    = ANVIL::DIM_MENU;
        const wxColour& accelC = ANVIL::ACCEL;

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

    /// The owning frame was (de)activated: on deactivation close the menu like a native one.
    void onFrameActivate( wxActivateEvent& aEvent )
    {
        if( !aEvent.GetActive() )
            DismissChain();

        aEvent.Skip();
    }

    wxMenu*           m_menu;
    wxWindow*         m_activationSource = nullptr;
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

        // Menu-bar label size is config-driven (AnvilMenuFontPt, default 0 -> tracks AnvilUiFontPt
        // = 10 pt, so the menu bar matches the rest of the UI: even fonts, NEMI consistency).  Face
        // follows AnvilUiFontFace.  No hardcoded point size.
        const ADVANCED_CFG& acfg = ADVANCED_CFG::GetCfg();
        wxFont labelFont = GetFont();

        if( acfg.m_AnvilMenuFontPt > 0.0 )
            labelFont.SetFractionalPointSize( acfg.m_AnvilMenuFontPt );
        else if( acfg.m_AnvilUiFontPt > 0.0 )
            labelFont.SetFractionalPointSize( acfg.m_AnvilUiFontPt );

        KIUI::ApplyFontFace( labelFont, acfg.m_AnvilUiFontFace );

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

        // The menu row is its own band: Deep Emerald in the light theme, the same dark strip as
        // the title row in the dark theme (see TITLEBAR_PANEL::onPaint, which paints the band).
        // Read the band colour from the palette rather than from the parent, whose background is
        // the TITLE strip colour -- white in the light theme.
        const bool      anvil    = ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame;
        const wxColour  normalBg = anvil ? ANVIL::CHROME_MENU : GetParent()->GetBackgroundColour();
        const wxColour& hoverBg  = ANVIL::BAR_HOVER;   // subtle hover, on-band (not a solid block)

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( m_hover ? hoverBg : normalBg ) );
        dc.DrawRectangle( GetClientRect() );

        dc.SetFont( GetFont() );

        // Every menu-bar entry — including "AnvilCAD MCP" — stays warm Bone primary text
        // (matches popup rows), not cold #FFF; the accent treatment on MCP read as a
        // permanently-hovered item.
        dc.SetTextForeground( ANVIL::ON_BAR );

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
            m_hoverBg( &aHoverBg ),
            m_indicator( aIndicator )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );

        // Caption glyph size: AnvilTitlebarGlyphPt (ships larger than the UI font so the
        // quick-access icons visually match the 24px toolbar icons below); 0 falls back to
        // tracking AnvilUiFontPt.  The icon FACE stays Segoe MDL2 Assets (a semantic
        // window-control glyph font, not a text face).
        const ADVANCED_CFG& acfg = ADVANCED_CFG::GetCfg();
        const double glyphPt = acfg.m_AnvilTitlebarGlyphPt > 0.0 ? acfg.m_AnvilTitlebarGlyphPt
                               : acfg.m_AnvilUiFontPt > 0.0      ? acfg.m_AnvilUiFontPt
                                                                 : 11.0;
        wxFont glyphFont( wxFontInfo( glyphPt ).FaceName( wxT( "Segoe MDL2 Assets" ) ) );

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
        dc.SetBrush( wxBrush( m_hover ? *m_hoverBg : GetParent()->GetBackgroundColour() ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        dc.SetFont( GetFont() );
        // Ink tier, not the bar tier: these glyphs sit on the title strip, which is white in the
        // light theme and the dark chrome strip in the dark theme.  The exception is a DARK hover
        // fill under a light-theme glyph -- the close button's red, above all -- where near-black
        // ink would be unreadable; there the glyph flips to the on-accent white.
        wxColour glyphFg = m_active ? ANVIL::INK_ICON_IDLE : ANVIL::INK_ICON_DIM;

        if( m_hover && m_hoverBg->IsOk()
            && ( m_hoverBg->Red() * 299 + m_hoverBg->Green() * 587 + m_hoverBg->Blue() * 114 )
                       < 128000 )
        {
            glyphFg = ANVIL::ON_ACCENT;
        }

        dc.SetTextForeground( glyphFg );

        wxSize ext = dc.GetTextExtent( m_glyph );
        dc.DrawText( m_glyph, ( sz.x - ext.x ) / 2, ( sz.y - ext.y ) / 2 );

        // VS Code-style active indicator: a short accent bar along the bottom edge while the
        // toggle's pane is shown — turns the flat glyph row into a stateful, polished control.
        if( m_indicator && m_active )
        {
            const int barW = sz.x / 2;
            const int barH = FromDIP( 2 );
            dc.SetBrush( wxBrush( ANVIL::ACCENT ) );
            dc.DrawRectangle( ( sz.x - barW ) / 2, sz.y - barH, barW, barH );
        }
    }

    wxString m_glyph;
    /// Points AT the palette token (ANVIL::HOVER / ANVIL::CLOSE_HOVER) rather than copying it:
    /// those are mutable globals that ANVIL::SetMode() rewrites, so the hover fill follows a
    /// light/dark flip on its own.  A wxColour copy taken in the ctor would freeze the theme
    /// that happened to be active when the button was built.
    const wxColour* m_hoverBg;
    bool     m_hover     = false;
    bool     m_active    = true;
    bool     m_indicator = false;
};


// Anvil: title-bar quick-access button drawing a real toolbar BITMAP icon (the same emerald
// icon set as the editor toolbars) instead of a Segoe MDL2 font glyph — so the caption's
// save/undo/redo match the toolbar icons below in type, colour and weight.
//
// App-wide icon target is 18px.  Anvil icon artwork carries ~2px of transparent padding, so the
// bitmap CELL must be 2px larger than the desired visible glyph: 18 + 2 = 20 renders an 18px icon.
static constexpr int ANVIL_TITLEBAR_ICON_PX   = 15;   // visible glyph size (what you see on screen)
static constexpr int ANVIL_TITLEBAR_ICON_CELL = ANVIL_TITLEBAR_ICON_PX + 2;   // bitmap cell (= 17)
class TITLEBAR_ICON_BUTTON : public wxWindow
{
public:
    TITLEBAR_ICON_BUTTON( wxWindow* aParent, BITMAPS aBitmap, int aWidth,
                          const wxColour& aHoverBg ) :
            wxWindow( aParent, wxID_ANY ),
            // 18px visible glyph = 20px bitmap cell (icon has ~2px built-in padding).
            m_normal( KiBitmapBundleDef( aBitmap, ANVIL_TITLEBAR_ICON_CELL ) ),
            m_disabled( KiDisabledBitmapBundleDef( aBitmap, ANVIL_TITLEBAR_ICON_CELL ) ),
            m_hoverBg( &aHoverBg )
    {
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        SetMinSize( wxSize( aWidth, FromDIP( 24 ) ) );

        Bind( wxEVT_PAINT, &TITLEBAR_ICON_BUTTON::onPaint, this );
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

    /// Same interface as TITLEBAR_GLYPH_BUTTON: bright icon while usable, dimmed otherwise.
    void SetActiveGlyph( bool aActive )
    {
        if( m_active != aActive )
        {
            m_active = aActive;
            Refresh();
        }
    }

private:
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize sz = GetClientSize();

        // Anvil mono chrome icons: the hover feedback is the glyph itself repainting Signal
        // Emerald (no background block), matching the editor toolbars and the Project Files
        // tree.  Flag off -> stock behaviour: full-height hover fill under the original icon.
        const bool mono = ADVANCED_CFG::GetCfg().m_AnvilMonoIcons;

        dc.SetPen( *wxTRANSPARENT_PEN );
        dc.SetBrush( wxBrush( ( m_hover && !mono ) ? *m_hoverBg
                                                   : GetParent()->GetBackgroundColour() ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        // 18px visible glyph; the +2 cell compensates the icon's built-in padding, matching the
        // editor toolbar (base 18 -> 18px) and the tree (18).
        const int side = FromDIP( ANVIL_TITLEBAR_ICON_CELL );
        wxBitmap  bmp = ( m_active ? m_normal : m_disabled ).GetBitmap( wxSize( side, side ) );

        if( mono && bmp.IsOk() )
        {
            bmp = KIUI::RecolorFlat( bmp, !m_active ? ANVIL::INK_ICON_DIM
                                          : m_hover ? ANVIL::INK_ICON_HOVER
                                                    : ANVIL::INK_ICON_IDLE );
        }

        if( bmp.IsOk() )
        {
            dc.DrawBitmap( bmp, ( sz.x - bmp.GetWidth() ) / 2, ( sz.y - bmp.GetHeight() ) / 2,
                           true );
        }
    }

    wxBitmapBundle  m_normal;
    wxBitmapBundle  m_disabled;
    /// @see TITLEBAR_GLYPH_BUTTON::m_hoverBg -- a pointer so the fill follows a theme flip.
    const wxColour* m_hoverBg;
    bool           m_hover  = false;
    bool           m_active = true;
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
            m_hoverBg( &aHoverBg )
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
        dc.SetBrush( wxBrush( m_hover ? *m_hoverBg : GetParent()->GetBackgroundColour() ) );
        dc.DrawRectangle( 0, 0, sz.x, sz.y );

        const int  side = FromDIP( 15 );
        const int  ox   = ( sz.x - side ) / 2;
        const int  oy   = ( sz.y - side ) / 2;
        const int  t    = side / 3;                          // docked-region thickness
        const int  r    = FromDIP( 2 );
        const wxColour fg = m_active ? ANVIL::INK_ICON_IDLE : ANVIL::INK_ICON_DIM;

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
            dc.SetBrush( wxBrush( ANVIL::ACCENT ) );
            dc.DrawRectangle( region.Deflate( FromDIP( 1 ) ) );
        }
    }

    SIDE     m_side;
    /// Points AT the palette token (ANVIL::HOVER / ANVIL::CLOSE_HOVER) rather than copying it:
    /// those are mutable globals that ANVIL::SetMode() rewrites, so the hover fill follows a
    /// light/dark flip on its own.  A wxColour copy taken in the ctor would freeze the theme
    /// that happened to be active when the button was built.
    const wxColour* m_hoverBg;
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
            m_hoverBg( &aHoverBg )
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
        dc.SetBrush( wxBrush( m_hover ? *m_hoverBg : GetParent()->GetBackgroundColour() ) );
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

    /// Points AT the palette token (ANVIL::HOVER / ANVIL::CLOSE_HOVER) rather than copying it:
    /// those are mutable globals that ANVIL::SetMode() rewrites, so the hover fill follows a
    /// light/dark flip on its own.  A wxColour copy taken in the ctor would freeze the theme
    /// that happened to be active when the button was built.
    const wxColour* m_hoverBg;
    bool     m_hover  = false;
    bool     m_active = true;
};
} // namespace


// ============================================================================
// Anvil Next: custom single-row title bar (logo + menu + window buttons).
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
        // The bar is TWO bands with different colours in the light theme (white title strip over
        // a Deep Emerald menu strip), so it paints itself rather than relying on a single
        // background colour -- see onPaint().
        SetBackgroundStyle( wxBG_STYLE_PAINT );
        Bind( wxEVT_PAINT, &TITLEBAR_PANEL::onPaint, this );

        applyTheme();

        m_sizer = new wxBoxSizer( wxHORIZONTAL );

        // Left app logo removed (brand request — no icon_kicad glyph in the title bar).  Keep a
        // small left inset so the title-bar content does not butt against the window edge and the
        // menu row below still aligns to a consistent left margin.
        m_sizer->AddSpacer( FromDIP( 10 ) );

        // The menu ("Menu Manager") lives on its OWN row beneath the title bar (Altium
        // architecture: title bar = row 1, menu = row 2), so it is added to the second row
        // further down rather than to this top row.
        m_menuSizer = new wxBoxSizer( wxHORIZONTAL );

        // Altium-style quick access: Save / Undo / Redo acting on the active editor tab.
        // Lives OUTSIDE m_menuSizer, which SetMenus() clears on every tab switch.  Buttons
        // dim via RefreshQuickAccess() when there is nothing to save/undo/redo (or on the
        // Project Manager view); clicks re-resolve the target frame, so a stale state can
        // never dispatch to a dead editor.
        const wxColour& qaHover = ANVIL::HOVER;

        // Real toolbar bitmap icons (the emerald set), not MDL2 font glyphs — so the
        // quick-access buttons match the editor toolbar icons in type, colour and weight.
        m_qaSave = new TITLEBAR_ICON_BUTTON( this, BITMAPS::save, FromDIP( 46 ), qaHover );
        m_qaUndo = new TITLEBAR_ICON_BUTTON( this, BITMAPS::undo, FromDIP( 46 ), qaHover );
        m_qaRedo = new TITLEBAR_ICON_BUTTON( this, BITMAPS::redo, FromDIP( 46 ), qaHover );
        m_qaSave->SetToolTip( _( "Save (active editor)" ) );
        m_qaUndo->SetToolTip( _( "Undo (active editor)" ) );
        m_qaRedo->SetToolTip( _( "Redo (active editor)" ) );

        // Altium-style app mark: the Anvil logo anchors the far left of the title bar
        // (Altium/OrCAD and native Windows apps lead the caption with the app icon).
        wxStaticBitmap* appMark = new wxStaticBitmap( this, wxID_ANY,
                KiBitmapBundle( BITMAPS::icon_kicad_24 ) );
        m_sizer->AddSpacer( FromDIP( 10 ) );
        m_sizer->Add( appMark, 0, wxALIGN_CENTRE_VERTICAL );

        m_sizer->AddSpacer( FromDIP( 8 ) );
        m_sizer->Add( m_qaSave, 0, wxEXPAND );
        m_sizer->Add( m_qaUndo, 0, wxEXPAND );
        m_sizer->Add( m_qaRedo, 0, wxEXPAND );

        m_qaSave->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->RunQuickAccessAction( 0 ); } );
        m_qaUndo->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->RunQuickAccessAction( 1 ); } );
        m_qaRedo->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->RunQuickAccessAction( 2 ); } );

        m_layoutBtns.push_back( m_qaSave );   // include in HitInteractive()
        m_layoutBtns.push_back( m_qaUndo );
        m_layoutBtns.push_back( m_qaRedo );

        // Altium-style document title: the active project / document name, centred between the
        // quick-access buttons and the right-hand controls.  Filled on project load and tab
        // switch by KICAD_MANAGER_FRAME::RefreshShellDocumentTitle().
        m_sizer->AddStretchSpacer( 1 );

        m_docTitle = new wxStaticText( this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_ELLIPSIZE_END );
        m_docTitle->SetForegroundColour( GetForegroundColour() );
        m_sizer->Add( m_docTitle, 0, wxALIGN_CENTRE_VERTICAL );

        m_sizer->AddStretchSpacer( 1 );

        // Altium look: "Open editor" dropdown (replaces the removed left icon rail) — pops a
        // menu of the editors/tools the rail used to launch.
        m_openEditor = makeWinButton( wxUniChar( 0xE710 ), qaHover );   // "Add" glyph
        m_openEditor->SetToolTip( _( "Open editor" ) );
        m_sizer->Add( m_openEditor, 0, wxEXPAND );
        m_openEditor->Bind( wxEVT_BUTTON,
                [this]( wxCommandEvent& )
                {
                    wxPoint p = m_openEditor->GetScreenPosition();
                    m_frame->ShowOpenEditorMenu( wxPoint( p.x, p.y + m_openEditor->GetSize().y ) );
                } );
        m_layoutBtns.push_back( m_openEditor );

        // Altium-style gear: Preferences of the active editor (additive — every editor keeps
        // its Tools-menu Preferences entry as well).
        m_gear = makeWinButton( wxUniChar( 0xE713 ), qaHover );     // Settings gear glyph
        m_gear->SetToolTip( _( "Preferences" ) );
        m_sizer->Add( m_gear, 0, wxEXPAND );
        m_gear->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->RunQuickAccessAction( 3 ); } );
        m_layoutBtns.push_back( m_gear );

        // Light / dark theme toggle -- the moon that sits between the gear and the AI mark in the
        // light mockup.  The glyph names the theme you are ABOUT to leave (moon while light, sun
        // while dark), the way a browser / editor theme switch reads.
        m_themeToggle = makeWinButton( themeGlyph(), qaHover );
        m_sizer->Add( m_themeToggle, 0, wxEXPAND );
        m_themeToggle->Bind( wxEVT_BUTTON,
                             [this]( wxCommandEvent& ) { m_frame->ToggleAppTheme(); } );
        m_layoutBtns.push_back( m_themeToggle );

        RefreshThemeToggle();   // glyph + tooltip for the theme we actually started in

        // Anvil Next single-window shell: VS Code / Cursor-style title-bar layout toggles,
        // sitting just left of the window-control buttons.  Each is a vector panel-diagram icon
        // (left/bottom/right docked region) that flips a pane and fills in while it's shown.
        m_sizer->Add( makeLayoutButton( TITLEBAR_PANEL_BUTTON::LEFT,
                                        _( "Toggle Project Explorer" ),
                                        [this]() { m_frame->ToggleProjectExplorer(); },
                                        [this]() { return m_frame->ProjectExplorerShown(); } ),
                      0, wxEXPAND );

        // Reports the real split state so the glyph fills in while the editor area is split and
        // the button reads as the toggle it now is.  It used to hardcode "false", which left the
        // one control that can undo a split looking permanently inactive.
        m_sizer->Add( makeLayoutButton( TITLEBAR_PANEL_BUTTON::BOTTOM,
                                        _( "Split Editor" ),
                                        [this]() { m_frame->ToggleSplitEditors(); },
                                        [this]() { return m_frame->EditorsSplit(); } ),
                      0, wxEXPAND );

        // AI Assistant: the Anvil "AI sparkle" logo mark (vector-drawn, see TITLEBAR_AI_BUTTON)
        // instead of the abstract panel-region diagram.  VS Code / Cursor keep their AI toggle in
        // the title bar so the panel is one click away after you close it; this is that icon.  It
        // lights up while the AI panel is open and dims when it is closed.
        m_sizer->Add( makeAiToggle( _( "Toggle AI Assistant" ),
                                    [this]() { m_frame->ToggleAiChat(); },
                                    [this]() { return m_frame->AiChatPanelShown(); } ),
                      0, wxEXPAND );

        // Account: the signed-in user, one click from the caption.  The account rows
        // (see fillAccountMenu()) pop as a themed dropdown anchored under the button; this
        // is the only entry point, the File menu carries no account submenu.
        m_account = makeWinButton( wxUniChar( 0xE77B ), qaHover );   // MDL2 "Contact" glyph
        m_sizer->Add( m_account, 0, wxEXPAND );
        m_account->Bind( wxEVT_BUTTON,
                [this]( wxCommandEvent& )
                {
                    m_frame->ShowAccountMenu( wxRect( m_account->GetScreenPosition(),
                                                     m_account->GetSize() ) );
                } );
        m_layoutBtns.push_back( m_account );

        RefreshAccount();   // tooltip names whoever is signed in right now

        m_sizer->AddSpacer( FromDIP( 6 ) );   // gap before the window-control buttons

        // Segoe MDL2 Assets caption glyphs (same as the native Windows / VS Code bar).
        // Subtle hover for minimize/maximize; the conventional red hover for close.
        const wxColour& subtleHover = ANVIL::HOVER;
        const wxColour& closeHover  = ANVIL::CLOSE_HOVER;
        m_min   = makeWinButton( wxUniChar( 0xE921 ), subtleHover );  // ChromeMinimize
        m_max   = makeWinButton( wxUniChar( 0xE922 ), subtleHover );  // ChromeMaximize
        m_close = makeWinButton( wxUniChar( 0xE8BB ), closeHover );   // ChromeClose

        // Anvil (user request): render ONLY the three window-control glyphs (minimize / maximize /
        // close) at a reduced ~13px — smaller than the +/gear caption icons (15px).  10.0pt Segoe
        // MDL2 Assets renders ~13px.  SetFont after creation overrides the shared caption glyph size
        // for just these three buttons (their paint uses GetFont()).
        {
            wxFont winGlyph( wxFontInfo( 10.0 ).FaceName( wxT( "Segoe MDL2 Assets" ) ) );

            if( winGlyph.IsOk() )
            {
                m_min->SetFont( winGlyph );
                m_max->SetFont( winGlyph );
                m_close->SetFont( winGlyph );
            }
        }

        m_sizer->Add( m_min,   0, wxEXPAND );
        m_sizer->Add( m_max,   0, wxEXPAND );
        m_sizer->Add( m_close, 0, wxEXPAND );

        // Drive these through the system commands a native caption button posts, rather than
        // calling Iconize()/Maximize() directly.  Windows then runs its own minimize/restore
        // animation and restore-rect bookkeeping, and wx raises wxEVT_MAXIMIZE — which it does
        // NOT do for a programmatic Maximize(), leaving EDA_BASE_FRAME's saved "normal" size
        // stale.  That stale size is what used to make restore-down a no-op.
        m_min  ->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { doMinimize(); } );
        m_max  ->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { doToggleMaximize(); } );
        m_close->Bind( wxEVT_BUTTON, [this]( wxCommandEvent& ) { m_frame->Close( false ); } );

        // Keep the glyph honest no matter how the state changed — the caption button, a
        // double-click on the title bar, Aero Snap (Win+Up/Down), or the taskbar.
        m_frame->Bind( wxEVT_SIZE,
                       [this]( wxSizeEvent& aEvent )
                       {
                           UpdateMaximizeGlyph();
                           aEvent.Skip();
                       } );

        UpdateMaximizeGlyph();   // show restore vs maximize for the initial window state

        // Two stacked rows to match the Altium architecture: title bar (this m_sizer) on top,
        // the menu manager on its own row underneath.
        wxBoxSizer* menuRow = new wxBoxSizer( wxHORIZONTAL );
        menuRow->AddSpacer( FromDIP( 6 ) );          // align the menu under the logo
        menuRow->Add( m_menuSizer, 0, wxEXPAND );

        wxBoxSizer* outerSizer = new wxBoxSizer( wxVERTICAL );
        outerSizer->Add( m_sizer, 1, wxEXPAND );     // row 1: title bar
        outerSizer->Add( menuRow, 1, wxEXPAND );     // row 2: menu manager

        SetSizer( outerSizer );
        SetMinSize( wxSize( -1, FromDIP( 60 ) ) );   // two rows (title + menu); pane size is the real driver

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
    /// Called on every size event, so it must not repaint unless the state actually flipped.
    void UpdateMaximizeGlyph()
    {
        if( !m_max )
            return;

        const bool maximized = m_frame->IsMaximized();

        if( maximized == m_maxGlyphShown )
            return;

        m_maxGlyphShown = maximized;
        m_max->SetGlyph( wxUniChar( maximized ? 0xE923 : 0xE922 ) );
    }

    /// Post the system command a native caption button would, so Windows performs the state
    /// change itself (animation, restore rect, wxEVT_MAXIMIZE) instead of us faking it.
    void postSysCommand( unsigned int aCommand )
    {
#ifdef __WXMSW__
        ::PostMessage( static_cast<HWND>( m_frame->GetHandle() ), WM_SYSCOMMAND,
                       static_cast<WPARAM>( aCommand ), 0 );
#else
        (void) aCommand;
#endif
    }

    void doMinimize()
    {
#ifdef __WXMSW__
        postSysCommand( SC_MINIMIZE );
#else
        m_frame->Iconize( true );
#endif
    }

    void doToggleMaximize()
    {
#ifdef __WXMSW__
        postSysCommand( m_frame->IsMaximized() ? SC_RESTORE : SC_MAXIMIZE );
#else
        m_frame->Maximize( !m_frame->IsMaximized() );
        UpdateMaximizeGlyph();
#endif
    }

    /// Re-sync every layout-toggle button's highlight to its pane's current visibility.
    /// Called after the AUI panes are built (they don't exist yet at construction time).
    void RefreshLayoutToggles()
    {
        for( const std::function<void()>& fn : m_layoutRefreshers )
            fn();
    }

    /// Dim/brighten the quick-access buttons to match the active editor's real state.
    void RefreshQuickAccess( bool aCanSave, bool aCanUndo, bool aCanRedo )
    {
        if( m_qaSave )
            m_qaSave->SetActiveGlyph( aCanSave );

        if( m_qaUndo )
            m_qaUndo->SetActiveGlyph( aCanUndo );

        if( m_qaRedo )
            m_qaRedo->SetActiveGlyph( aCanRedo );
    }

    /// Re-read the signed-in user so the account button's tooltip names them.  Called on
    /// every menu rebuild, which is also when sign-in / sign-out lands.
    void RefreshAccount()
    {
        if( !m_account )
            return;

        const ANVIL_USER user = ANVIL_AUTH::GetUser();

        m_account->SetToolTip( user.email.IsEmpty()
                                       ? _( "Account (not signed in)" )
                                       : wxString::Format( _( "Account (%s)" ), user.Label() ) );

        // Dim the glyph while signed out, matching the other stateful caption icons.
        m_account->SetActiveGlyph( !user.email.IsEmpty() );
    }

    /// Re-read the app theme and repaint the whole bar in it (called by the light/dark toggle).
    void RefreshTheme()
    {
        applyTheme();
        RefreshThemeToggle();
        Refresh();
    }

    /// Point the theme button's glyph + tooltip at the theme that is actually active.
    void RefreshThemeToggle()
    {
        if( !m_themeToggle )
            return;

        m_themeToggle->SetGlyph( themeGlyph() );
        m_themeToggle->SetToolTip( ANVIL::IsLight() ? _( "Switch to dark theme" )
                                                    : _( "Switch to light theme" ) );
    }

    /// Set the Altium-style document/project name shown in the centre of the title bar.
    void SetDocumentTitle( const wxString& aTitle )
    {
        if( m_docTitle && m_docTitle->GetLabel() != aTitle )
        {
            m_docTitle->SetLabel( aTitle );
            m_docTitle->SetToolTip( aTitle );
            Layout();
        }
    }

private:
    /// Segoe MDL2 Assets glyph for the theme toggle: crescent moon (QuietHours) while the light
    /// theme is on, sun (Brightness) while the dark theme is on.
    static wxString themeGlyph()
    {
        return wxString( ANVIL::IsLight() ? wxUniChar( 0xE708 ) : wxUniChar( 0xE706 ) );
    }

    /// Paint the two bands the bar is made of.  Row 1 (title strip) is CHROME_BG and row 2 (the
    /// menu manager) is CHROME_MENU; in the dark theme the two tokens hold the same value, so
    /// this is a single flat strip exactly as before.  Doing it here -- rather than with one
    /// background colour -- is what lets the light theme put the white title strip over the Deep
    /// Emerald menu band that the mockup shows.
    void onPaint( wxPaintEvent& )
    {
        wxAutoBufferedPaintDC dc( this );
        const wxSize          sz = GetClientSize();

        dc.SetPen( *wxTRANSPARENT_PEN );

        if( !ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame )
        {
            dc.SetBrush( wxBrush( GetBackgroundColour() ) );
            dc.DrawRectangle( 0, 0, sz.x, sz.y );
            return;
        }

        // Split where the sizer actually put the boundary between the two rows, so the bands stay
        // aligned with the buttons at any DPI / font size.  Fall back to an even split before the
        // first Layout(), when the row sizer has no size yet.
        int split = m_sizer ? m_sizer->GetSize().GetHeight() : 0;

        if( split <= 0 || split >= sz.y )
            split = sz.y / 2;

        dc.SetBrush( wxBrush( ANVIL::CHROME_BG ) );
        dc.DrawRectangle( 0, 0, sz.x, split );

        dc.SetBrush( wxBrush( ANVIL::CHROME_MENU ) );
        dc.DrawRectangle( 0, split, sz.x, sz.y - split );
    }

    /// One VS Code-style title-bar layout toggle: a glyph button that flips a pane's
    /// visibility (aToggle) and highlights itself while that pane is shown (aIsShown).
    TITLEBAR_PANEL_BUTTON* makeLayoutButton( TITLEBAR_PANEL_BUTTON::SIDE aSide,
                                             const wxString& aTooltip, std::function<void()> aToggle,
                                             std::function<bool()> aIsShown )
    {
        // Vector panel-diagram icon with a subtle hover (refined, not a solid block).
        TITLEBAR_PANEL_BUTTON* b =
                new TITLEBAR_PANEL_BUTTON( this, aSide, FromDIP( 40 ), ANVIL::HOVER );

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
        TITLEBAR_AI_BUTTON* b = new TITLEBAR_AI_BUTTON( this, FromDIP( 48 ), ANVIL::HOVER );
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
            // purple override) so the title-bar strip is the same tone as the toolbar strips.
            bg = ANVIL::CHROME_BG;  // mockup tonal pass: dark neutral chrome strip
            fg = ANVIL::BONE;       // NEMI Bone
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
    TITLEBAR_GLYPH_BUTTON* m_min = nullptr;
    TITLEBAR_GLYPH_BUTTON* m_max = nullptr;
    TITLEBAR_GLYPH_BUTTON* m_close = nullptr;
    bool                   m_maxGlyphShown = false;  // last state UpdateMaximizeGlyph() painted
    TITLEBAR_ICON_BUTTON* m_qaSave = nullptr;    // Altium-style quick access (active editor);
    TITLEBAR_ICON_BUTTON* m_qaUndo = nullptr;    // real toolbar bitmap icons, not MDL2 glyphs
    TITLEBAR_ICON_BUTTON* m_qaRedo = nullptr;
    TITLEBAR_GLYPH_BUTTON* m_gear = nullptr;     // Preferences gear (additive to the menus)
    TITLEBAR_GLYPH_BUTTON* m_openEditor = nullptr; // "Open editor" dropdown (replaces the rail)
    TITLEBAR_GLYPH_BUTTON* m_account = nullptr;  // signed-in user dropdown (File > Account)
    TITLEBAR_GLYPH_BUTTON* m_themeToggle = nullptr; // NEMI Emerald light <-> dark switch
    wxStaticText*          m_docTitle = nullptr; // active project / document name (Altium-style)
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
        m_anvilAgent( nullptr ),
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

    // Altium-style Panels button (bottom-right): pops the ACTIVE editor tab's panels menu
    // (or the shell's own panels on the Project Manager view).  The frame is re-resolved on
    // every click, so a closed tab can never leave a dangling target.
    if( BITMAP_BUTTON* panelsBtn = static_cast<KISTATUSBAR*>( GetStatusBar() )->GetPanelsButton() )
    {
        panelsBtn->Bind( wxEVT_BUTTON,
                [this]( wxCommandEvent& )
                {
                    EDA_BASE_FRAME* target = getActiveDockedEditorFrame();

                    if( !target )
                        target = this;

                    TOOL_INTERACTIVE* tool = nullptr;

                    if( target == this )
                        tool = m_toolManager->GetTool<KICAD_MANAGER_CONTROL>();
                    else if( target->GetToolManager() )
                        tool = target->GetToolManager()->GetTool<COMMON_TOOLS>();

                    // Fall back to any tool the menu can dispatch through; ACTION_MENU items
                    // resolve their own actions, so the specific tool only routes events.
                    ACTION_MENU* menu = new ACTION_MENU( false, tool );
                    target->buildPanelsMenu( menu );

                    if( menu->GetMenuItemCount() == 0 )
                    {
                        delete menu;
                        return;
                    }

                    ANVIL_POPUP_MENU* popup = new ANVIL_POPUP_MENU( GetStatusBar(), menu );

                    // The popup destroys itself on dismiss; free the menu with it.
                    popup->Bind( wxEVT_DESTROY,
                            [popup, menu]( wxWindowDestroyEvent& aEvt )
                            {
                                if( aEvt.GetEventObject() == popup )
                                    delete menu;

                                aEvt.Skip();
                            } );

                    wxPoint pos = static_cast<KISTATUSBAR*>( GetStatusBar() )->GetPanelsButton()
                                          ->GetScreenPosition();
                    popup->PopupAt( wxPoint( pos.x - popup->GetSize().x + FromDIP( 20 ),
                                             pos.y - popup->GetSize().y - FromDIP( 4 ) ) );
                } );
    }

    // Throttled quick-access state refresh (~3×/sec).  Idle does not fire while a docked
    // editor owns a modal dialog — accepted: every click re-resolves frame + state fresh,
    // so a stale dim-state can never mis-dispatch.  Do not "fix" this with a timer.
    Bind( wxEVT_IDLE,
          [this]( wxIdleEvent& aEvent )
          {
              static wxLongLong s_lastQuickAccessRefresh = 0;
              wxLongLong        now = wxGetLocalTimeMillis();

              if( now - s_lastQuickAccessRefresh > 300 )
              {
                  s_lastQuickAccessRefresh = now;
                  RefreshQuickAccess();
              }

              aEvent.Skip();
          } );

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

    // Anvil Next: the left vertical toolbar only repeats actions that already live in the
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
    // Altium look: hidden when the modern layout is on (the Project Files tree becomes the
    // far-left dock; editors open from Project Files or the title-bar "Open editor" button).
    // Auto-revealed by HideTabsIfNeeded() if a job-set is opened, since job-sets share this
    // notebook.
    m_auimgr.AddPane( m_notebook, EDA_PANE().Name( "Editors" ).Left().Layer( 3 ).Position( 0 )
                                            .CaptionVisible( false ).PaneBorder( false )
                                            .CloseButton( false ).Floatable( false ).Movable( false )
                                            .DockFixed( true )
                                            .Show( !ADVANCED_CFG::GetCfg().m_ModernMenuLayout )
                                            .MinSize( FromDIP( 40 ), -1 ).BestSize( FromDIP( 40 ), -1 ) );

    // Anvil Next single-window shell (Layer B): the center editor-tab area.  Each
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

        // Anvil Next single-window shell: register this shell as the KIWAY tab host so editor
        // KIFACEs (eeschema "Update PCB" / pcbnew "Update Schematic", etc.) dock the sibling
        // editor they open as a tab here instead of floating it as a separate window.  Cleared
        // in doCloseWindow().  Only registered when the shell's tab area actually exists.
        Kiway().SetTabHost( this );

        // Anvil Next: the modern layout's Window menu activates frames through this hook.
        // A docked editor becomes a tab selection; everything else (the shell itself, still-
        // floating tools like the 3D viewer) falls back to the default Show + Raise.  Cleared
        // in doCloseWindow() alongside the tab host.
        EDA_BASE_FRAME::SetWindowMenuActivator(
                [this]( EDA_BASE_FRAME* aFrame )
                {
                    if( aFrame == this )
                    {
                        Raise();
                        SetFocus();
                        return;
                    }

                    for( const std::pair<int, wxWindow*>& entry : m_dockedEditors )
                    {
                        if( entry.first == aFrame->GetId() )
                        {
                            int idx = m_editorTabs->GetPageIndex( entry.second );

                            if( idx != wxNOT_FOUND )
                            {
                                m_editorTabs->SetSelection( idx );
                                Raise();
                                return;
                            }
                        }
                    }

                    if( aFrame->IsIconized() )
                        aFrame->Iconize( false );

                    aFrame->Show( true );
                    aFrame->Raise();
                } );
    }

#ifdef __WXMSW__
    // Custom single-row title bar (logo + menu + window buttons), created earlier above.
    // The native caption is removed by the WM_NCCALCSIZE handler; this strip replaces it.
    m_auimgr.AddPane( m_titleBar, wxAuiPaneInfo().Name( "TitleBar" ).Top().Layer( 10 )
                                  .CaptionVisible( false ).PaneBorder( false ).Gripper( false )
                                  .DockFixed( true ).Floatable( false ).Movable( false )
                                  .Resizable( false )
                                  .MinSize( -1, FromDIP( 60 ) ).BestSize( -1, FromDIP( 60 ) ) );
#endif

    // Anvil Next: a single shell-owned "AI Assistant" panel (Cursor style).  Created only
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
            // (no Python backend).  Attached before the page loads so window.anvilSend
            // exists when chat.html runs.  Tool calls reach the schematic editor over
            // MAIL_ANVIL_AI_TOOL, since kicad.exe cannot see SCH_EDIT_FRAME directly.
            m_anvilAgent = new ANVIL_AI_AGENT( &Kiway(), this, m_aiChatPanel );
            m_anvilAgent->Attach();

            // Tell the agent which project is open right now (even on the project-manager
            // view, before any editor tab), so it works IN that project instead of quietly
            // creating a separate one. Refreshed on every LoadProject().
            m_anvilAgent->SetDocumentContext( Prj().GetProjectPath(), wxString(), wxString() );

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
                fileUrl += wxString::Format( wxT( "?t=%ld&backend=localhost:8765&project=%s&theme=%s" ),
                                             (long) wxDateTime::Now().GetTicks(), projPath,
                                             ANVIL::IsLight() ? wxT( "light" ) : wxT( "dark" ) );
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

            // Layer 3: INSIDE the hoisted editor toolbar rows (Top layers 4-6), so the top icon
            // strips span the full window width and this pane's caption starts on the same row
            // as the Project Files caption — the aligned header line of the Anvil mockups.
            m_auimgr.AddPane( m_aiChatPanel, EDA_PANE().Name( AiChatPanelName() )
                              .Right().Layer( 3 )
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

        // Anvil Next (Cursor-style): give the SHELL its own backend command channel so a
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

    // Anvil AI tool socket (127.0.0.1, loopback only): the single channel that lets an
    // agent edit the open design or open a project. It is ALWAYS up while the app runs,
    // because the in-app chat itself uses it (to open the project it just built, and for
    // live edits). The "AnvilCAD MCP" menu does NOT start/stop the socket — it flips the
    // MODE: Start = hand the app to an EXTERNAL client (chat is turned off), Stop = back to
    // in-app chat. Only one drives at a time.
    m_anvilToolServer = std::make_unique<ANVIL_AI_TOOL_SERVER>( &Kiway(), this );
    m_anvilToolServer->Start();

    // Shell-level tool: "open_project" loads a project into THIS window (so a design the
    // chat just built appears right here, not in a second window). Editor tools fall
    // through to AnvilSendTool.
    m_anvilToolServer->SetShellHandler(
            [this]( const std::string& aReq ) -> std::string
            {
                try
                {
                    nlohmann::json j = nlohmann::json::parse( aReq );

                    if( j.value( "tool", std::string() ) != "open_project" )
                        return std::string();   // not a shell tool

                    std::string path = j.contains( "input" )
                            ? j["input"].value( "path", std::string() )
                            : j.value( "path", std::string() );

                    if( path.empty() )
                        return R"({"ok":false,"message":"open_project needs a path."})";

                    wxFileName pro( wxString::FromUTF8( path ) );
                    CallAfter( [this, pro]() { LoadProject( pro ); } );
                    return R"({"ok":true,"message":"Opening project in the current window."})";
                }
                catch( const std::exception& )
                {
                    return R"({"ok":false,"message":"open_project: bad request JSON."})";
                }
            } );

    // Hooks behind the unified menu bar's "AnvilCAD MCP" menu (shown in every editor).
    // isRunning reflects the external MODE, not the socket (which is always up).
    EDA_BASE_FRAME::MCP_MENU_CONTROLLER mcpCtl;

    mcpCtl.isRunning = [this]() { return m_aiMcpExternalMode; };
    mcpCtl.start     = [this]()
                       {
                           m_aiMcpExternalMode = true;
                           setAiMcpMode( true );   // external mode -> chat OFF + banner
                           return true;
                       };
    mcpCtl.stop      = [this]()
                       {
                           m_aiMcpExternalMode = false;
                           setAiMcpMode( false );  // back to chat mode -> chat ON
                       };
    mcpCtl.port      = [this]()
                       {
                           return m_anvilToolServer ? m_anvilToolServer->GetPort() : 0;
                       };

    EDA_BASE_FRAME::SetMcpMenuController( std::move( mcpCtl ) );

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

    // Only fit the initial window size the first time Anvil is run.
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

    // Anvil Next single-window shell: warm the heavy editor KIFACEs in the background so
    // the user's first click on Symbol/Footprint/Gerber/Drawing-Sheet is instant instead
    // of "loading the whole app".  No-op unless the shell + prewarm flags are set.  Use a
    // unique timer id and bind only to it, so this never intercepts the base frame's
    // auto-save timer (ID_AUTO_SAVE_TIMER), which also raises wxEVT_TIMER on this frame.
    m_prewarmTimer.SetOwner( this, wxWindow::NewControlId() );
    Bind( wxEVT_TIMER, &KICAD_MANAGER_FRAME::prewarmNextEditor, this, m_prewarmTimer.GetId() );
    schedulePrewarmEditors();

    // Anvil Next unified shell footer: with no editor tab docked yet, make sure the shell's own
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
    // Drop the MCP menu hooks first: their lambdas capture this frame.
    EDA_BASE_FRAME::SetMcpMenuController( {} );

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

    // Altium look: the launcher-only rail is hidden; reveal this notebook when a job-set (an
    // extra page) is present so job-sets stay reachable, and hide it again when only the
    // (hidden) launcher page remains.
    if( ADVANCED_CFG::GetCfg().m_ModernMenuLayout )
    {
        wxAuiPaneInfo& pane = m_auimgr.GetPane( wxS( "Editors" ) );

        if( pane.IsOk() )
        {
            bool show = m_notebook->GetPageCount() > 1;

            if( pane.IsShown() != show )
            {
                pane.Show( show );
                m_auimgr.Update();
            }
        }
    }
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

    // UnifiedToolbar: if this editor's top toolbar was hoisted above the tab bar, give it back to
    // the editor's own AUI BEFORE the WS_CHILD reversal, so the undocked/standalone frame keeps
    // its toolbar (and the reparented widget is not left dangling in the shell).
    restoreEditorTopToolbar( dynamic_cast<EDA_BASE_FRAME*>( aPlayer ) );

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


void KICAD_MANAGER_FRAME::RunQuickAccessAction( int aWhich )
{
#ifdef __WXMSW__
    // Resolve the target on EVERY click (never cached): a closed or dying tab simply no-ops.
    EDA_BASE_FRAME* target = getActiveDockedEditorFrame();

    if( aWhich == 3 )   // gear — Preferences works everywhere, including the PM view
    {
        EDA_BASE_FRAME* prefsTarget = target ? target : this;

        if( TOOL_MANAGER* mgr = prefsTarget->GetToolManager() )
            mgr->RunAction( ACTIONS::openPreferences );

        return;
    }

    if( !target || !target->GetToolManager() )
        return;

    switch( aWhich )
    {
    case 0: target->GetToolManager()->RunAction( ACTIONS::save ); break;
    case 1: target->GetToolManager()->RunAction( ACTIONS::undo ); break;
    case 2: target->GetToolManager()->RunAction( ACTIONS::redo ); break;
    default: break;
    }

    RefreshQuickAccess();
#endif
}


void KICAD_MANAGER_FRAME::RefreshQuickAccess()
{
#ifdef __WXMSW__
    if( !m_titleBar )
        return;

    EDA_BASE_FRAME* target = getActiveDockedEditorFrame();

    bool canSave = false;
    bool canUndo = false;
    bool canRedo = false;

    if( target )
    {
        // Library editors define IsContentModified() frame-wide; the titlebar Save maps to
        // ACTIONS::save (current item) — accepted, documented behaviour.
        canSave = target->IsContentModified();
        canUndo = target->GetUndoCommandCount() > 0;
        canRedo = target->GetRedoCommandCount() > 0;
    }

    m_titleBar->RefreshQuickAccess( canSave, canUndo, canRedo );
#endif
}


void KICAD_MANAGER_FRAME::RefreshShellDocumentTitle()
{
#ifdef __WXMSW__
    if( !m_titleBar )
        return;

    // Altium-style: the loaded project and the active editor tab's document name, in the title
    // bar.  Mockup separator: a middle dot, not an em-dash.
    wxString text = wxS( "Anvil" );

    wxString projName = Prj().GetProjectName();

    if( !projName.IsEmpty() )
        text << wxT( "  ·  " ) << projName;

    if( EDA_BASE_FRAME* editor = getActiveDockedEditorFrame() )
    {
        wxString file = editor->GetCurrentFileName();

        if( !file.IsEmpty() )
            text << wxT( "  ·  " ) << wxFileName( file ).GetFullName();
    }

    m_titleBar->SetDocumentTitle( text );
#endif
}


void KICAD_MANAGER_FRAME::ShowOpenEditorMenu( const wxPoint& aScreenPos )
{
#ifdef __WXMSW__
    // Altium "Open editor" dropdown: the editors/tools the removed left icon rail used to launch.
    // Reuses the themed ANVIL_POPUP_MENU + ACTION_MENU dispatch (same as the status-bar Panels
    // button) so each item runs its stock action.
    KICAD_MANAGER_CONTROL* tool = m_toolManager->GetTool<KICAD_MANAGER_CONTROL>();

    ACTION_MENU* menu = new ACTION_MENU( false, tool );
    buildOpenEditorMenu( menu );

    if( menu->GetMenuItemCount() == 0 )
    {
        delete menu;
        return;
    }

    ANVIL_POPUP_MENU* popup = new ANVIL_POPUP_MENU( this, menu );

    // The popup destroys itself on dismiss; free the menu with it.
    popup->Bind( wxEVT_DESTROY,
            [popup, menu]( wxWindowDestroyEvent& aEvt )
            {
                if( aEvt.GetEventObject() == popup )
                    delete menu;

                aEvt.Skip();
            } );

    popup->PopupAt( aScreenPos );
#endif
}


void KICAD_MANAGER_FRAME::ShowAccountMenu( const wxRect& aButtonScreenRect )
{
#ifdef __WXMSW__
    // Title-bar account dropdown: the account rows, flat (no nested submenu) because the
    // button itself is already the "Account" affordance.
    KICAD_MANAGER_CONTROL* tool = m_toolManager->GetTool<KICAD_MANAGER_CONTROL>();

    ACTION_MENU* menu = new ACTION_MENU( false, tool );
    fillAccountMenu( menu );

    ANVIL_POPUP_MENU* popup = new ANVIL_POPUP_MENU( this, menu );

    // The popup destroys itself on dismiss; free the menu with it.
    popup->Bind( wxEVT_DESTROY,
            [popup, menu]( wxWindowDestroyEvent& aEvt )
            {
                if( aEvt.GetEventObject() == popup )
                    delete menu;

                aEvt.Skip();
            } );

    // Right-align the menu with the button rather than left-anchoring it: the account icon sits
    // at the far right of the caption, so a left-anchored menu would hang off the window (and,
    // maximized, off the screen) and clip the email.
    popup->PopupAt( wxPoint( aButtonScreenRect.GetRight() - popup->GetSize().x + 1,
                             aButtonScreenRect.GetBottom() + 1 ) );
#endif
}


void KICAD_MANAGER_FRAME::RefreshAccountButton()
{
#ifdef __WXMSW__
    if( m_titleBar )
        m_titleBar->RefreshAccount();
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
            || !UseUnifiedMenuBar() )
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
        buildCommonMenuBarFrom( editor, this );
        buildTitleBarMenuButtons();
    }
    else
    {
        // Project Manager context (no editor tab, or forced): restore the manager's own menu.
        // The manager's doReCreateMenuBar() already refreshes the title-bar buttons on this path.
        doReCreateMenuBar();
    }

    RefreshShellDocumentTitle();

    m_syncingShellMenu = false;
#endif
}


void KICAD_MANAGER_FRAME::onEditorTabChanged( wxAuiNotebookEvent& evt )
{
    syncShellMenuToActiveTab();
    syncAiPanelToActiveTab();
    syncShellStatusBarToActiveTab();
    syncShellToolbarToActiveTab();   // reveal the front tab's hoisted toolbar (UnifiedToolbar)

    // Dragging a tab into or out of a group changes the split state without going through
    // ToggleSplitEditors(), so re-read it here to keep the title-bar toggle honest.
    if( m_titleBar )
        m_titleBar->RefreshLayoutToggles();

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
    // DockEditorAsTab), exactly like standalone Anvil.  So all this has to do is hide the shell's
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


void KICAD_MANAGER_FRAME::hoistEditorTopToolbar( EDA_BASE_FRAME* aEditor )
{
#ifdef __WXMSW__
    if( !aEditor || !m_editorTabs || !ADVANCED_CFG::GetCfg().m_SingleWindowShell
            || !ADVANCED_CFG::GetCfg().m_UnifiedToolbar )
    {
        return;
    }

    const int idx = aEditor->GetId();

    for( const HOISTED_EDITOR_TOOLBAR& h : m_hoistedToolbars )
    {
        if( h.editorId == idx )
            return;   // already hoisted
    }

    ACTION_TOOLBAR* main      = aEditor->GetTopMainToolbar();
    ACTION_TOOLBAR* aux       = aEditor->GetTopAuxToolbar();
    ACTION_TOOLBAR* activeBar = aEditor->GetActiveBarToolbar();

    if( !main && !aux && !activeBar )
        return;   // frame has no top toolbar (e.g. a viewer) — leave it as-is

    // NOTE: the LEFT/RIGHT drawing rails are intentionally NOT hoisted — they stay vertical next to
    // the canvas (standard EDA ergonomics; keeps route/via/zone one click from the board).  Only the
    // Standard toolbar (main) + its aux row are lifted to the top band.  Laying the vertical drawing
    // rails out horizontally needs a toolbar orientation flip (wxAuiToolBar::SetOrientation is
    // protected, and AUI does not reliably re-orient a rail built vertical), so that is deferred.

    EDA_AUI_MANAGER& editorMgr = aEditor->GetAuiManager();

    // Lift every toolbar out of the editor's own AUI and dock them in the shell's top strip.  Each
    // widget is reparented to the shell, but ACTION_TOOLBAR keeps a direct TOOL_MANAGER pointer, so
    // its buttons still drive THIS editor.  Added hidden; syncShellToolbarToActiveTab() reveals only
    // the front tab's.  Everything docks in the SAME top layer/row so wxAUI flows them left-to-right
    // and, when the window is too narrow, wraps the overflow onto the next row automatically — fully
    // dynamic (re-lays-out on every resize): fills row 1 on a wide desktop, wraps to row 2/3 on a
    // small laptop, no hardcoded widths.  aFlip turns the side drawing rails (created vertical)
    // horizontal so they join the top band too (full Altium ribbon).
    auto hoistOne = [&]( ACTION_TOOLBAR* tb, const wxString& tag, int layer, int pos )
    {
        if( !tb )
            return;

        if( editorMgr.GetPane( tb ).IsOk() )
            editorMgr.DetachPane( tb );

        tb->Reparent( this );
        tb->SetAuiManager( &m_auimgr );

        // Same AUI layer + a higher Position keeps a toolbar on the SAME physical row as the one
        // before it (side-by-side), and wxAuiManager wraps it to a new row only when the row is
        // actually full — so free right-hand space is used before a new row is created.
        m_auimgr.AddPane( tb, EDA_PANE().HToolbar()
                                        .Name( wxString::Format( "Hoist%s_%d", tag, idx ) )
                                        .Top().Layer( layer ).Position( pos ).Hide() );
    };

    // NOTHING-CLIPPED priority (user rule: nothing may be lost, excess can spill to another row):
    // main Standard toolbar on the top row (Layer 6) and the aux settings row (Track/Via/layer/grid/
    // Zoom/Override) on its OWN full-width row below it (Layer 5).  Each row gets the whole window
    // width, so no control is ever cut off the right edge — the "second/third row" absorbs overflow.
    // Higher AUI layer docks higher up, so Layer 6 (main) sits above Layer 5 (aux).
    hoistOne( main, wxT( "Main" ), 6, 0 );
    hoistOne( aux,  wxT( "Aux" ),  5, 0 );
    // Active Bar gets its OWN dedicated top row (its own AUI layer, below the Standard + selectors
    // rows, just above the tab strip).  All of its flat tool icons fit on that full-width row, so
    // every tool stays visible — no dropdowns, no ">>" chevron, nothing clipped.
    hoistOne( activeBar, wxT( "Active" ), 4, 0 );

    editorMgr.Update();                          // editor canvas reflows into the freed rows
    m_hoistedToolbars.push_back( { idx, main, aux, activeBar, nullptr, nullptr } );
    m_auimgr.Update();
    syncShellToolbarToActiveTab();
#endif
}


void KICAD_MANAGER_FRAME::restoreEditorTopToolbar( EDA_BASE_FRAME* aEditor )
{
#ifdef __WXMSW__
    if( !aEditor )
        return;

    for( auto it = m_hoistedToolbars.begin(); it != m_hoistedToolbars.end(); ++it )
    {
        if( it->editorId != aEditor->GetId() )
            continue;

        EDA_AUI_MANAGER& editorMgr = aEditor->GetAuiManager();

        // Reverse hoistOne(): pull from the shell AUI, reparent back to the editor, and re-dock
        // into the editor's own AUI at the same home (name/dock/layer/orientation) it came from.
        auto restoreTop = [&]( ACTION_TOOLBAR* tb, const wxString& name, int layer )
        {
            if( !tb )
                return;

            if( m_auimgr.GetPane( tb ).IsOk() )
                m_auimgr.DetachPane( tb );

            tb->Reparent( aEditor );
            tb->SetAuiManager( &editorMgr );
            editorMgr.AddPane( tb, EDA_PANE().HToolbar().Name( name ).Top().Layer( layer ) );
        };

        // The side rails were laid out horizontal for the top band — re-dock them on their original
        // edge so a standalone (undocked) editor looks normal.  Docking them in a .Left()/.Right()
        // (VToolbar) dock makes wxAuiManager re-orient them vertical again (SetOrientation is
        // protected, so we rely on the dock direction rather than calling it directly).
        auto restoreSide = [&]( ACTION_TOOLBAR* tb, const wxString& name, bool onLeft )
        {
            if( !tb )
                return;

            if( m_auimgr.GetPane( tb ).IsOk() )
                m_auimgr.DetachPane( tb );

            tb->Reparent( aEditor );
            tb->SetAuiManager( &editorMgr );

            if( onLeft )
                editorMgr.AddPane( tb, EDA_PANE().VToolbar().Name( name ).Left() );
            else
                editorMgr.AddPane( tb, EDA_PANE().VToolbar().Name( name ).Right() );
        };

        restoreTop(  it->main,      wxT( "TopMainToolbar" ),  6 );
        restoreTop(  it->aux,       wxT( "TopAuxToolbar" ),   4 );
        restoreTop(  it->activeBar, wxT( "ActiveBarToolbar" ), 3 );
        restoreSide( it->left,  wxT( "LeftToolbar" ),  true );
        restoreSide( it->right, wxT( "RightToolbar" ), false );

        m_hoistedToolbars.erase( it );
        m_auimgr.Update();
        editorMgr.Update();
        return;
    }
#endif
}


void KICAD_MANAGER_FRAME::onDockedEditorDestroyed( wxWindowDestroyEvent& aEvent )
{
    // Let wx and any other handlers still see the destroy notification.
    aEvent.Skip();

#ifdef __WXMSW__
    // Shutdown-ordering guard: when the SHELL itself is being destroyed, its wxAuiManager member is
    // torn down before its child editor frames are, so the editor destroys below would touch a dead
    // m_auimgr.  There is also nothing to protect then -- no further idle passes will run -- so bail.
    if( IsBeingDeleted() )
        return;

    // wxWindowDestroyEvent is a command event, so it also bubbles up from descendant windows.  Only
    // act when the frame we bound this on (a docked editor, keyed by its window id) is the one being
    // destroyed; a bubbled child destroy has a different id and matches nothing below.
    wxWindow* dying = aEvent.GetWindow();

    if( !dying )
        return;

    const int dyingId = dying->GetId();

    // Drop any stale docked-editor registry entry so later id lookups can't resolve to a dead frame.
    for( std::vector<std::pair<int, wxWindow*>>::iterator it = m_dockedEditors.begin();
         it != m_dockedEditors.end(); ++it )
    {
        if( it->first == dyingId )
        {
            m_dockedEditors.erase( it );
            break;
        }
    }

    // If this editor still has toolbars hoisted into the shell (i.e. it is being destroyed WITHOUT
    // going through DetachDockedEditor()/restoreEditorTopToolbar() first), tear them down now while
    // we still hold valid pointers.  These ACTION_TOOLBARs are reparented to the shell, so the dying
    // editor never frees them; left alone they would remain dangling children of the shell and the
    // next idle wxWindow::SendIdleEvents pass would call UpdateWindowUI() on freed memory.  Detach
    // each from the shell AUI and Destroy() it (a non-top-level window deletes immediately and
    // unlinks itself from the shell's child list), so nothing dangling can be walked.
    for( std::vector<HOISTED_EDITOR_TOOLBAR>::iterator it = m_hoistedToolbars.begin();
         it != m_hoistedToolbars.end(); ++it )
    {
        if( it->editorId != dyingId )
            continue;

        for( ACTION_TOOLBAR* tb : { it->main, it->aux, it->activeBar, it->left, it->right } )
        {
            if( !tb )
                continue;

            if( m_auimgr.GetPane( tb ).IsOk() )
                m_auimgr.DetachPane( tb );

            tb->Destroy();
        }

        m_hoistedToolbars.erase( it );
        m_auimgr.Update();
        break;
    }
#endif
}


void KICAD_MANAGER_FRAME::syncShellToolbarToActiveTab()
{
#ifdef __WXMSW__
    if( m_hoistedToolbars.empty() )
        return;

    EDA_BASE_FRAME* active   = getActiveDockedEditorFrame();
    const int       activeId = active ? active->GetId() : -1;
    bool            changed  = false;

    for( const HOISTED_EDITOR_TOOLBAR& h : m_hoistedToolbars )
    {
        const bool show = ( h.editorId == activeId );

        for( ACTION_TOOLBAR* tb : { h.main, h.aux, h.activeBar, h.left, h.right } )
        {
            if( !tb )
                continue;

            wxAuiPaneInfo& pane = m_auimgr.GetPane( tb );

            if( pane.IsOk() && pane.IsShown() != show )
            {
                pane.Show( show );
                changed = true;
            }
        }
    }

    if( changed )
        m_auimgr.Update();
#endif
}


void KICAD_MANAGER_FRAME::syncAiPanelToActiveTab()
{
    if( !m_aiChatPanel || !m_anvilAgent )
        return;

    const wxString projPath = Prj().GetProjectPath();
    EDA_BASE_FRAME* editor   = getActiveDockedEditorFrame();

    const bool isSch = editor && editor->GetFrameType() == FRAME_SCH;
    const bool isPcb = editor && editor->GetFrameType() == FRAME_PCB_EDITOR;

    // The agent both remembers the context (for the model's system prompt) and pushes it
    // into the page via window.anvilSetContext, so the panel always shows and acts on the
    // current document. On the project-manager view (no editor tab, or a Gerber/Calculator
    // tab) still push the open PROJECT — otherwise the panel shows "no project" and the AI
    // quietly creates a separate one instead of working in this project.
    if( isSch || isPcb )
    {
        wxString file = editor->GetCurrentFileName();
        m_anvilAgent->SetDocumentContext( projPath, isSch ? file : wxString(),
                                          isPcb ? file : wxString() );
    }
    else
    {
        m_anvilAgent->SetDocumentContext( projPath, wxString(), wxString() );
    }
}


void KICAD_MANAGER_FRAME::setAiMcpMode( bool aMcpOn )
{
    // Mutual exclusion: when the AnvilCAD MCP server is running, an external client owns
    // the tool channel, so the in-app chat is turned OFF (only one drives the app at a
    // time). Make the state visible in BOTH the chat panel (a banner) and the status bar.
    if( m_aiChatPanel )
    {
        m_aiChatPanel->RunScriptAsync( aMcpOn
                ? wxS( "if(window.anvilSetEnabled)window.anvilSetEnabled(false);" )
                : wxS( "if(window.anvilSetEnabled)window.anvilSetEnabled(true);" ) );
    }

    if( wxStatusBar* sb = GetStatusBar() )
    {
        sb->SetStatusText( aMcpOn
                ? wxString::Format( _( "AnvilCAD MCP: ON  ·  port %d  ·  chat paused" ),
                                    m_anvilToolServer ? m_anvilToolServer->GetPort() : 0 )
                : wxString(),
                1 );
    }
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
        m_auimgr.Update();
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
    // 2026-08 mockup tonal pass: strips on CHROME_BG, panel bodies on CHROME_PANEL, flat
    // uniform captions (WX_AUI_DOCK_ART::DrawCaption), hairline sashes/borders.
    const wxColour& bgDeep   = ANVIL::CONTENT;
    const wxColour& bgBar    = ANVIL::CHROME_BG;
    const wxColour& bgPanel  = ANVIL::CHROME_PANEL;
    const wxColour& accent   = ANVIL::ACCENT;
    const wxColour& bgHeader = ANVIL::CHROME_HEADER;
    const wxColour& capAct   = ANVIL::CHROME_HEADER;
    const wxColour& capInact = ANVIL::CHROME_HEADER;
    const wxColour& border   = ANVIL::CHROME_LINE;
    const wxColour& sash     = ANVIL::CHROME_SASH;
    const wxColour& text     = ANVIL::BONE;

    // 1) Dock-pane chrome: the background behind/between panes, sashes, borders, captions.
    if( wxAuiDockArt* dockArt = m_auimgr.GetArtProvider() )
    {
        dockArt->SetColour( wxAUI_DOCKART_BACKGROUND_COLOUR, bgDeep );
        dockArt->SetColour( wxAUI_DOCKART_SASH_COLOUR, sash );
        dockArt->SetColour( wxAUI_DOCKART_BORDER_COLOUR, border );
        dockArt->SetColour( wxAUI_DOCKART_GRIPPER_COLOUR, bgHeader );
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
            // The strip is a CHROME band, so it carries the same tone as the title / tool-bar
            // band above it (oat in the light theme, dark grey in the dark one).
            tabArt->SetColour( ANVIL::TAB_STRIP );

            // Active tab: the CONTENT tone in both themes, so the selected tab reads as a
            // continuation of the canvas below it rather than as another chrome block.
            tabArt->SetActiveColour( ANVIL::TAB_ACTIVE );

            // A wxAuiNotebook hands each of its wxAuiTabCtrl strips a CLONE of this provider
            // and only re-clones on layout events (AddPage, font change...), so recolouring
            // the master alone leaves every existing tab strip painting the PREVIOUS theme —
            // the "tab bar is one toggle behind" bug.  Re-setting the provider pushes fresh
            // clones, carrying the colours set above, into every tab ctrl right now.
            nb->SetArtProvider( tabArt->Clone() );
        }

        nb->SetBackgroundColour( bgPanel );

        // The strip a wxAuiNotebook draws its tabs on is a child wxAuiTabCtrl, not the notebook
        // itself: colouring only the notebook leaves that strip -- and the empty run to the
        // right of the last tab -- on the stock system grey.  Colour the tab ctrls with the
        // strip band tone so the whole header row reads as one band, and leave every other
        // child (the pages) on the panel-body tone.
        for( wxWindow* child : nb->GetChildren() )
        {
            const bool isTabStrip = dynamic_cast<wxAuiTabCtrl*>( child ) != nullptr;

            child->SetBackgroundColour( isTabStrip ? ANVIL::TAB_STRIP : bgPanel );
            child->SetForegroundColour( text );
            child->Refresh();
        }

        nb->Refresh();
    }

    // 3) Shell-owned side panels (project tree + launcher).  Not the editor tab pages.
    anvilRecolorShellTree( m_projectTreePane, bgPanel, text );
    anvilRecolorShellTree( m_launcher, bgPanel, text );

    // 4) Status bar — mono face (mockup: status read-outs are data).
    if( wxStatusBar* sb = GetStatusBar() )
    {
        sb->SetBackgroundColour( bgBar );
        sb->SetForegroundColour( text );
        sb->SetFont( KIUI::GetStatusFont( sb ) );
        sb->Refresh();
    }

    Refresh();
}


void KICAD_MANAGER_FRAME::ToggleAppTheme()
{
    COMMON_SETTINGS* cfg = Pgm().GetCommonSettings();

    if( !cfg )
        return;

    const bool goLight = !ANVIL::IsLight();

    cfg->m_Appearance.app_theme = goLight ? APP_THEME::LIGHT : APP_THEME::DARK;

    // One repaint, not a cascade.  The flip walks the title bar, every dock pane and every
    // docked editor; without freezing, each of those repaints as it is touched and the switch
    // reads as a slow ripple instead of an instant change.
    Freeze();

    // Persist straight away rather than at shutdown.  The native half of the theme -- wx's MSW
    // dark mode, which owns pop-up menus, scrollbars and the inside of native controls -- can
    // only be established at start-up, so the setting has to be on disk even if the user quits
    // the moment after clicking.
    Pgm().GetSettingsManager().Save( cfg );

    // Flip the palette copies this module can reach (its own + kicommon's), then repaint.
    KIUI::SyncAnvilTheme();

    applyAnvilShellTheme();

    // The Project Files icons are baked into a wxImageList at load time, tinted for whichever
    // theme was active then — so unlike the toolbar / title-bar glyphs (recoloured at draw time)
    // they have to be rebuilt, or bone-white icons stay on a white panel.
    if( m_projectTreePane && m_projectTreePane->m_TreeProject )
    {
        m_projectTreePane->m_TreeProject->LoadIcons();

        // The tree's hover band / selection / scrollbar are drawn by the native Explorer
        // visual style picked when the control was CREATED, so a runtime flip leaves e.g. a
        // dark hover row on the white light-theme tree.  Re-point the uxtheme to match.
        KIPLATFORM::UI::SetDarkExplorerTheme( m_projectTreePane->m_TreeProject, !goLight );

        m_projectTreePane->m_TreeProject->Refresh();
    }

    if( m_titleBar )
        m_titleBar->RefreshTheme();

    // The AI panel is an HTML page in a WebView, so it is themed by CSS rather than by the
    // palette: hand it the same flip through the hook chat.html installs (see anvilSetTheme).
    if( m_aiChatPanel )
    {
        m_aiChatPanel->RunScriptAsync(
                wxString::Format( wxT( "window.anvilSetTheme && window.anvilSetTheme('%s');" ),
                                  goLight ? wxT( "light" ) : wxT( "dark" ) ) );
    }

    // Every docked editor lives in its own _kiface DLL with its own copy of the palette globals,
    // so the flip has to be dispatched through the virtual -- that is what makes it run inside
    // that DLL.  Resolve by window id (not a stored pointer) so a torn-down editor reads as null.
    for( const std::pair<int, wxWindow*>& entry : m_dockedEditors )
    {
        if( EDA_BASE_FRAME* editor =
                    dynamic_cast<EDA_BASE_FRAME*>( wxWindow::FindWindowById( entry.first ) ) )
        {
            editor->ReapplyAnvilTheme();
        }
    }

    Layout();
    Thaw();
    Refresh();
}


void KICAD_MANAGER_FRAME::onShellPaneFocus( wxChildFocusEvent& aEvent )
{
#ifdef __WXMSW__
    // The Project Explorer (a non-editor pane) gained focus: the user is on the Project
    // Manager, so bring its own menu back.  Only rebuild when an editor menu is currently
    // shown, so repeated clicks in the tree don't rebuild the bar.
    // Keep the active editor tab's menu while an editor is open: clicking the Project Files
    // tree should NOT flip the top menu back to the home set (that made the dynamic menu look
    // broken).  Only fall back to the Project Manager menu when no editor tab is open.
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell && UseUnifiedMenuBar()
            && m_shellMenuShowsEditor && !getActiveDockedEditorFrame() )
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
    if( ADVANCED_CFG::GetCfg().m_SingleWindowShell && UseUnifiedMenuBar()
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


// Pick the delimiter from the header line: the BOM engine can emit comma / semicolon /
// tab separated files (field_delimiter is user-configurable), so never assume comma --
// take whichever separator occurs most on the first line.
static wxChar detectCsvDelimiter( const wxString& aHeader )
{
    const int commas = aHeader.Freq( ',' );
    const int semis  = aHeader.Freq( ';' );
    const int tabs   = aHeader.Freq( '\t' );

    if( tabs >= commas && tabs >= semis && tabs > 0 )
        return '\t';

    if( semis >= commas && semis > 0 )
        return ';';

    return ',';
}


// Quote-aware split of one CSV record: "a,b" stays one cell, "" is a literal quote.
static std::vector<wxString> splitCsvLine( const wxString& aLine, wxChar aDelim )
{
    std::vector<wxString> cells;
    wxString              cur;
    bool                  inQuotes = false;

    for( size_t i = 0; i < aLine.length(); ++i )
    {
        const wxUniChar c = aLine[i];

        if( inQuotes )
        {
            if( c == '"' )
            {
                if( i + 1 < aLine.length() && aLine[i + 1] == '"' )   // escaped "" -> "
                {
                    cur += '"';
                    ++i;
                }
                else
                {
                    inQuotes = false;
                }
            }
            else
            {
                cur += c;
            }
        }
        else if( c == '"' )
        {
            inQuotes = true;
        }
        else if( c == aDelim )
        {
            cells.push_back( cur );
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }

    cells.push_back( cur );
    return cells;
}


bool KICAD_MANAGER_FRAME::OpenCsvTab( const wxString& aPath )
{
#ifdef __WXMSW__
    // In-shell document tabs only exist in the single-window shell (Windows).  When the flag
    // is off, or there is no tab host, tell the caller to use the external launcher instead.
    if( !m_editorTabs || !ADVANCED_CFG::GetCfg().m_SingleWindowShell )
        return false;

    wxFileName fn( aPath );

    if( !fn.FileExists() )
        return false;

    const wxString fullPath = fn.GetFullPath();

    // Already open?  Each document page is tagged with its full path as the panel name --
    // re-select it instead of opening a duplicate tab.  (Editor tabs use a plain wxPanel too,
    // but carry no name, so they never collide with this match.)
    for( size_t i = 0; i < m_editorTabs->GetPageCount(); ++i )
    {
        if( wxWindow* pg = m_editorTabs->GetPage( i ); pg && pg->GetName() == fullPath )
        {
            m_editorTabs->SetSelection( i );
            return true;
        }
    }

    // wxTextFile copes with the usual encodings / line endings.  An unreadable file falls
    // back to the external opener (return false) rather than showing an empty tab.
    wxTextFile tf( fullPath );

    if( !tf.Open() )
        return false;

    std::vector<std::vector<wxString>> rows;
    size_t                             maxCols = 0;
    wxChar                             delim = ',';

    for( wxString line = tf.GetFirstLine(); !tf.Eof(); line = tf.GetNextLine() )
    {
        if( rows.empty() )
            delim = detectCsvDelimiter( line );

        std::vector<wxString> cells = splitCsvLine( line, delim );
        maxCols = std::max( maxCols, cells.size() );
        rows.push_back( std::move( cells ) );
    }

    tf.Close();

    if( maxCols == 0 )
        maxCols = 1;

    // The first row is the column header (every BOM export carries one); the rest are data.
    const int nCols = static_cast<int>( maxCols );
    const int nRows = rows.empty() ? 0 : static_cast<int>( rows.size() ) - 1;

    wxPanel*    panel = new wxPanel( m_editorTabs, wxID_ANY );
    panel->SetName( fullPath );                       // tag for the already-open check above
    wxBoxSizer* sizer = new wxBoxSizer( wxVERTICAL );
    panel->SetSizer( sizer );

    wxGrid* grid = new wxGrid( panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               wxWANTS_CHARS | wxBORDER_NONE );
    grid->CreateGrid( std::max( 0, nRows ), nCols );
    grid->EnableEditing( false );                     // read-only viewer
    grid->DisableDragRowSize();
    grid->SetColLabelAlignment( wxALIGN_LEFT, wxALIGN_CENTRE );

    if( !rows.empty() )
    {
        for( int col = 0; col < nCols; ++col )
        {
            const wxString label = col < static_cast<int>( rows[0].size() ) ? rows[0][col]
                                                                            : wxString();
            grid->SetColLabelValue( col, label );
        }
    }

    for( int r = 1; r < static_cast<int>( rows.size() ); ++r )
    {
        for( int col = 0; col < nCols && col < static_cast<int>( rows[r].size() ); ++col )
            grid->SetCellValue( r - 1, col, rows[r][col] );
    }

    grid->AutoSizeColumns();

    sizer->Add( grid, 1, wxEXPAND );
    panel->Layout();

    // Add as a normal notebook page.  It is NOT registered in m_dockedEditors, so the shell's
    // editor bookkeeping (PruneDeadEditorTabs / menu-sync / close-detach) treats it as an inert
    // page: closing just destroys it, and the menu falls back to the Project Manager's own.
    m_editorTabs->AddPage( panel, fn.GetFullName(), true );

    return true;
#else
    return false;
#endif
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

    // Keep the editor frame hidden and frozen during the reparent / WS_CHILD surgery so it
    // never flashes as a separate top-level window before it becomes a tab.
    aPlayer->Hide();
    aPlayer->Freeze();

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
    // Anvil).  syncShellStatusBarToActiveTab() then hides the shell's own bar while an editor tab
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

    // Safety net: if this editor frame is ever destroyed while still docked (any path other than an
    // explicit tab close, which reparents its toolbars back first), evict its hoisted toolbars from
    // the shell so no dangling toolbar child is left for the idle UI-update walk to dereference.
    // See onDockedEditorDestroyed(): this was a use-after-free crash in wxAuiToolBar::DoIdleUpdate.
    aPlayer->Bind( wxEVT_DESTROY, &KICAD_MANAGER_FRAME::onDockedEditorDestroyed, this );

    host->Layout();

    // The reparented frame does not always honour the box sizer on first dock (a wxFrame
    // is an unusual sizer item), so size it explicitly to the host's client area BEFORE it is
    // shown; subsequent shell resizes are handled by the sizer / host EVT_SIZE.
    aPlayer->SetSize( host->GetClientSize() );

    // Now that it is correctly parented and sized, reveal it in one paint (no wrong-size or
    // separate-window flash) and give it keyboard focus so hotkeys work immediately.
    aPlayer->Thaw();
    aPlayer->Show( true );
    aPlayer->SetFocus();

    // Make the shell's top menu reflect the just-docked editor.  AddPage(..., true) above may
    // have fired PAGE_CHANGED before m_dockedEditors held this entry (so its handler fell back
    // to the manager menu); now that the registry is populated, set the editor's menu for real.
    syncShellMenuToActiveTab();

    // Point the shell-owned common AI panel at the just-docked document for the same reason.
    syncAiPanelToActiveTab();

    // Mirror the just-docked editor's (now hidden) status bar into the shell footer.
    syncShellStatusBarToActiveTab();

    // UnifiedToolbar: lift this editor's top toolbar out of the tab and dock it above the tab
    // strip (Altium order).  No-op unless the flag is on; safe because the toolbar dispatches
    // through a stored TOOL_MANAGER pointer, not window-tree events.
    hoistEditorTopToolbar( dynamic_cast<EDA_BASE_FRAME*>( aPlayer ) );

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


void KICAD_MANAGER_FRAME::OpenAnvilFile( const wxString& aPath )
{
    // Opening a file focuses the one running window (VS Code / Cursor behaviour).
    if( IsIconized() )
        Iconize( false );

    Raise();
    RequestUserAttention();

    if( aPath.IsEmpty() )
        return;   // bare second launch: nothing to open, we already raised the window.

    wxFileName fn( aPath );
    fn.MakeAbsolute();

    if( !fn.FileExists() )
    {
        DisplayErrorMessage( this, wxString::Format( _( "File '%s' does not exist." ),
                                                     fn.GetFullPath() ) );
        return;
    }

    const wxString ext = fn.GetExt();

    // 1) Project file → open / switch to it.  Anvil & legacy projects route through LoadProject
    //    too (it offers to import them).  FILEEXT is the single source of this mapping.
    if( ext == FILEEXT::AnvilProjectFileExtension
        || ext == FILEEXT::ProjectFileExtension
        || ext == FILEEXT::LegacyProjectFileExtension )
    {
        LoadProject( fn );
        return;
    }

    // 2) Schematic or board file.  Opening is Anvil-only: only a native anvil_sch / anvil_pcb
    //    opens directly in the editor.  A foreign Anvil (or legacy) schematic/board is never
    //    opened natively — it reaches an editor only through the import flow — so route it to its
    //    owning project's import offer instead.  FILEEXT is the single source of the mapping.
    const TOOL_ACTION* editorAction = nullptr;

    if( ext == FILEEXT::AnvilSchematicFileExtension )
        editorAction = &KICAD_MANAGER_ACTIONS::editSchematic;
    else if( ext == FILEEXT::AnvilPcbFileExtension )
        editorAction = &KICAD_MANAGER_ACTIONS::editPCB;

    if( !editorAction )
    {
        // Foreign (kicad_*) / legacy schematic or board → import, don't open natively.
        if( FILEEXT::IsForeignFamilyExt( ext )
            || ext == FILEEXT::LegacySchematicFileExtension
            || ext == FILEEXT::LegacyPcbFileExtension )
        {
            // Hand the owning project (sibling .kicad_pro / legacy .pro) to LoadProject, which
            // puts up the "Import & Convert" offer.  If there is no sibling project, tell the
            // user how Anvil designs enter Anvil.
            wxFileName projectFn = fn;
            projectFn.SetExt( FILEEXT::ProjectFileExtension );          // sibling kicad_pro

            if( !projectFn.FileExists() )
                projectFn.SetExt( FILEEXT::LegacyProjectFileExtension );  // or legacy .pro

            if( projectFn.FileExists() )
            {
                LoadProject( projectFn );
                return;
            }

            DisplayInfoMessage( this, wxString::Format(
                    _( "'%s' is a Anvil file.\n\nAnvil opens Anvil designs through import: use "
                       "File > Import > Anvil Project." ),
                    fn.GetFullName() ) );
            return;
        }

        DisplayErrorMessage( this, wxString::Format( _( "Don't know how to open '%s' in Anvil." ),
                                                     fn.GetFullPath() ) );
        return;
    }

    // A project and its schematic / board share one basename in one directory, so the owning
    // project is the sibling carrying the project extension.  Make it the active project (if it
    // isn't already) so ShowPlayer resolves the right file to open as a tab.  Compare by
    // directory + basename (ignoring the .anvil_/.kicad_ spelling) so we don't needlessly reload
    // the project that is already open.
    wxFileName projectFn = fn;
    projectFn.SetExt( FILEEXT::AnvilProjectFileExtension );

    if( !projectFn.FileExists() )
        projectFn.SetExt( FILEEXT::ProjectFileExtension );   // Anvil-native sibling

    wxFileName activeFn( GetProjectFileName() );

    const bool sameProject = IsProjectActive()
                             && activeFn.GetPath() == projectFn.GetPath()
                             && activeFn.GetName() == projectFn.GetName();

    if( projectFn.FileExists() && !sameProject )
        LoadProject( projectFn );

    if( !IsProjectActive() )
    {
        DisplayInfoMessage( this, wxString::Format( _( "Open or create a project for '%s' first." ),
                                                    fn.GetFullName() ) );
        return;
    }

    // Fire the editor action exactly as the sidebar button does: KICAD_MANAGER_CONTROL::ShowPlayer
    // creates (or re-selects) the editor and, with the single-window shell on, docks it as a tab.
    GetToolManager()->RunAction( *editorAction );
}


void KICAD_MANAGER_FRAME::HandleForwardedOpen( const wxString& aPath )
{
    // A later launch handed us this file — bring our window forward regardless of what we do
    // with the path (opening a file behaves like focusing the app).
    if( IsIconized() )
        Iconize( false );

    Raise();
    RequestUserAttention();

    if( aPath.IsEmpty() )
        return;   // bare second launch: just focus.

    wxFileName fn( aPath );
    fn.MakeAbsolute();

    if( !fn.FileExists() )
    {
        DisplayErrorMessage( this, wxString::Format( _( "File '%s' does not exist." ),
                                                     fn.GetFullPath() ) );
        return;
    }

    // A file belonging to a DIFFERENT project than the one open here gets its own window instead
    // of silently swapping the active project (and its unsaved edits) out from under the user —
    // same rule as VS Code opening a different folder.  Anvil keeps one project per directory, so
    // "same directory as the active project" == "belongs to the open project" (this also covers
    // sub-sheets whose basename differs from the project).  A fresh "--new" instance bypasses the
    // single-instance handoff and opens the file in its own window.
    if( IsProjectActive() )
    {
        const wxString activeDir = wxFileName( GetProjectFileName() ).GetPath();

        if( fn.GetPath() != activeDir )
        {
            const wxString exe = wxStandardPaths::Get().GetExecutablePath();

            wxExecute( wxString::Format( wxS( "\"%s\" --new \"%s\"" ), exe, fn.GetFullPath() ),
                       wxEXEC_ASYNC );
            return;
        }
    }

    // Same project (or nothing open yet) → open it here as a tab.
    OpenAnvilFile( fn.GetFullPath() );
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
    KISTATUSBAR* sb = new KISTATUSBAR( number, this, id,
                                       static_cast<KISTATUSBAR::STYLE_FLAGS>( KISTATUSBAR::NOTIFICATION_ICON
                                                                            | KISTATUSBAR::CANCEL_BUTTON
                                                                            | KISTATUSBAR::WARNING_ICON
                                                                            | KISTATUSBAR::PANELS_BUTTON ) );

    size_t sbFieldCnt = static_cast<size_t>( sb->GetFieldsCount() );
    std::vector<int> sbFieldSizes( sbFieldCnt );

    for( size_t i = 0; i < sbFieldCnt; i++ )
        sbFieldSizes[i] = sb->GetStatusWidth( static_cast<int>( i ) );

    if( sbFieldCnt )
        sbFieldSizes[0] = -3;

    sb->SetStatusWidths( sbFieldCnt, sbFieldSizes.data() );

    return sb;
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

    // Anvil Next single-window shell: stop advertising ourselves as the KIWAY tab host before
    // we are destroyed, so any late editor → sibling-editor launch floats instead of calling
    // into a dead shell.
    if( Kiway().GetTabHost() == this )
        Kiway().SetTabHost( nullptr );

    // The Window-menu activator captures `this`; drop it before destruction so a late menu
    // event falls back to the default Show + Raise instead of calling into a dead shell.
    EDA_BASE_FRAME::SetWindowMenuActivator( nullptr );

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

        // Wait for any in-flight autosave so the HEAD check below isn't racing it.
        Kiway().LocalHistory().WaitForPendingSave();

        if( !projPath.IsEmpty() && Kiway().LocalHistory().HistoryExists( projPath ) )
        {
            if( Kiway().LocalHistory().HeadNewerThanLastSave( projPath ) )
            {
                // Tag unconditionally: even on no-op snapshots Last_Save must anchor at HEAD.
                Kiway().LocalHistory().CommitFullProjectSnapshot( projPath, wxS( "Close" ) );
                Kiway().LocalHistory().TagSave( projPath, wxS( "project" ) );
            }
        }

        // The editors clean up autosaves for sheets actually dirtied in their session.
        // Anything still on disk here was deferred by the user in the recovery dialog
        // and must survive so the dialog can offer it again on the next open.

        m_active_project = false;
        // Enforce local history size limit (if enabled) once all pending saves/backups are done.
        if( Pgm().GetCommonSettings() && Pgm().GetCommonSettings()->m_Backup.enabled )
        {
            unsigned long long int limit = Pgm().GetCommonSettings()->m_Backup.limit_total_size;

            if( limit > 0 )
            {
                WX_PROGRESS_REPORTER reporter( this, _( "Local History" ), 3, PR_NO_ABORT );
                Kiway().LocalHistory().EnforceSizeLimit( Prj().GetProjectPath(), (size_t) limit, &reporter );
            }
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
    // extension of the project file actually on disk.  Heal before validating (this also
    // prefers the .anvil_pro twin of a half-converted directory over its .kicad_pro).
    aProjectFileName =
            wxFileName( FILEEXT::HealToExistingFamilySibling( aProjectFileName.GetFullPath() ) );

    // The project file should be valid by the time we get here or something has gone wrong.
    if( !aProjectFileName.Exists() )
        return false;

    // Foreign projects are never opened natively: every direct-open path (File>Open, CLI,
    // double-click, MRU, drag-drop, unarchive) funnels through here, so this is the single
    // place Anvil projects get routed into the import & convert flow instead.
    if( FILEEXT::IsForeignFamilyExt( aProjectFileName.GetExt() )
            || aProjectFileName.GetExt().IsSameAs( FILEEXT::LegacyProjectFileExtension, false ) )
    {
        return OfferImportForeignProject( aProjectFileName );
    }

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
            // see if there is another Anvil instance running. If not, then we can override the
            // lock. This could happen if Anvil crashed or was interrupted.  Live check, not the
            // startup snapshot — see PGM_BASE::IsAnotherInstanceRunningLive().
            if( !Pgm().IsAnotherInstanceRunningLive() )
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

    // Heal companions written by older builds: rename this project's own
    // <basename>.kicad_{prl,dru,jobset} to the native spelling when no anvil sibling
    // exists yet.  Project basename only — libraries are never touched.  Announced on the
    // status bar so the on-disk rename (visible to VCS as delete+add) isn't silent.
    {
        const std::string* companions[] = { &FILEEXT::ProjectLocalSettingsFileExtension,
                                            &FILEEXT::DesignRulesFileExtension,
                                            &FILEEXT::KiCadJobSetFileExtension };
        wxArrayString      healed;

        for( const std::string* ext : companions )
        {
            wxFileName native( Prj().GetProjectPath(), Prj().GetProjectName(), *ext );
            wxFileName foreign( native );
            foreign.SetExt( FILEEXT::FamilySiblingExt( native.GetExt() ) );

            if( !native.FileExists() && foreign.FileExists()
                    && wxRenameFile( foreign.GetFullPath(), native.GetFullPath() ) )
            {
                healed.Add( native.GetFullName() );
            }
        }

        if( !healed.IsEmpty() )
        {
            SetStatusText( wxString::Format( _( "Renamed to Anvil extensions: %s" ),
                                             wxJoin( healed, ',' ) ) );
        }
    }

    LoadWindowState( aProjectFileName.GetFullName() );

    if( aProjectFileName.IsDirWritable() )
        SetMruPath( Prj().GetProjectPath() );

    if( Kiway().LocalHistory().HeadNewerThanLastSave( Prj().GetProjectPath() ) )
    {
        wxString head = Kiway().LocalHistory().GetHeadHash( Prj().GetProjectPath() );

        KICAD_MESSAGE_DIALOG dlg( this, _( "Anvil found unsaved changes from your last session that are newer than "
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
            // User declined; commit on-disk state and tag unconditionally so Last_Save anchors
            // at HEAD even if no new commit was needed.
            Kiway().LocalHistory().CommitFullProjectSnapshot( Prj().GetProjectPath(), wxS( "Declined restore" ) );
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

    // Keep the AI agent pointed at the project that is now open, so the chat works in it.
    if( m_anvilAgent )
        m_anvilAgent->SetDocumentContext( Prj().GetProjectPath(), wxString(), wxString() );

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

            // Preserve the requested native extension: forcing .kicad_pro here left a stray
            // Anvil project file next to every freshly created .anvil_pro project.
            wxFileName destFileName( aProjectFileName );

            if( destFileName.GetExt() != FILEEXT::AnvilProjectFileExtension )
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
            // locked it, check to see if there is another Anvil instance running.
            // If there is not, then we can override the lock.  This could happen if
            // Anvil crashed or was interrupted.  Live check, not the startup
            // snapshot — see PGM_BASE::IsAnotherInstanceRunningLive().
            if( !Pgm().IsAnotherInstanceRunningLive() )
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
    RefreshShellDocumentTitle();

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
     * earlier in project loading. This gives us the visual effect of a opened Anvil project but
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

    // Sign-in / sign-out lands as a menu rebuild, so this is where the account button's
    // tooltip and dimmed/lit state get re-read.
    m_titleBar->RefreshAccount();

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
        {
            // ...but a maximized window is deliberately placed at (-border, -border) with
            // size + 2*border so that a NATIVE frame falls off-screen.  We have no native
            // frame, so without insetting here that border's worth of real content is what
            // falls off-screen — the clipped "Project Files" caption and status bar.
            if( ::IsZoomed( hwnd ) )
            {
                NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>( lParam );

                const int bx = ::GetSystemMetrics( SM_CXFRAME )
                               + ::GetSystemMetrics( SM_CXPADDEDBORDER );
                const int by = ::GetSystemMetrics( SM_CYFRAME )
                               + ::GetSystemMetrics( SM_CXPADDEDBORDER );

                params->rgrc[0].left   += bx;
                params->rgrc[0].top    += by;
                params->rgrc[0].right  -= bx;
                params->rgrc[0].bottom -= by;
            }

            return 0;
        }

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


bool KICAD_MANAGER_FRAME::EditorsSplit()
{
    // A split wxAuiNotebook grows one wxAuiTabCtrl per tab group, so "more than one tab ctrl"
    // is the state itself, not a flag we have to track alongside it.  Reading it back from the
    // notebook also covers the splits the user makes by dragging a tab (wxAUI_NB_TAB_SPLIT),
    // which never come through ToggleSplitEditors() at all.
    return m_editorTabs && m_editorTabs->GetAllTabCtrls().size() > 1;
}


void KICAD_MANAGER_FRAME::ToggleSplitEditors()
{
    if( !m_editorTabs )
        return;

    // Collapse every group back into one (VS Code's "single" editor layout).  Splitting used to
    // be a one-way door: nothing in the tree could undo it, so a user who clicked the title-bar
    // button -- whose icon reads as an ordinary panel toggle -- was left with two stacked editors,
    // each with its own tab strip and status bar, and no way back short of dragging tabs around.
    if( EditorsSplit() )
    {
        m_editorTabs->UnsplitAll();
        return;
    }

    // Needs a second tab to split into: every docked editor is a unique reparented frame,
    // so the same editor cannot be shown in both halves — splitting a lone tab is a no-op.
    if( m_editorTabs->GetPageCount() < 2 )
        return;

    int sel = m_editorTabs->GetSelection();

    if( sel != wxNOT_FOUND )
        m_editorTabs->Split( static_cast<size_t>( sel ), wxRIGHT );
}
