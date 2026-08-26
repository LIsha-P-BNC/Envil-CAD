/*
 * This program source code file is part of Anvil, a free EDA CAD application.
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

#include <wx/aui/aui.h>
#include <wx/aui/framemanager.h>
#include <wx/aui/auibook.h>
#include <wx/bitmap.h>
#include <wx/dc.h>
#include <wx/settings.h>

#include <advanced_config.h>
#include <kiplatform/anvil_theme.h>
#include <kiplatform/ui.h>
#include <pgm_base.h>
#include <tool/action_toolbar.h>
#include <settings/common_settings.h>
#include <widgets/panel_notebook_base.h>
#include <widgets/wx_aui_art_providers.h>
#include <widgets/ui_common.h>
#include <gal/color4d.h>

// NEMI brand: build the app-wide UI font (Space Grotesk @ AnvilUiFontPt) from a base font, so AUI
// chrome text (tab labels, dock captions, toolbar labels) matches the rest of the UI instead of
// staying at the wx default (*wxNORMAL_FONT ~ system 9pt).  These art providers are not windows,
// so they hold their own private fonts; nothing else syncs them to the frame font.
static wxFont anvilChromeFont( const wxFont& aBase )
{
    const ADVANCED_CFG& acfg = ADVANCED_CFG::GetCfg();
    wxFont              font = aBase;

    if( acfg.m_AnvilUiFontPt > 0.0 )
        font.SetFractionalPointSize( acfg.m_AnvilUiFontPt );

    // Only when the brand face is really installed: SetFaceName() invalidates the font
    // otherwise, and an invalid font reports point size 0 — which collapsed m_captionSize below
    // to 6 px and clipped every dock-pane title ("Project Files", "AI Assistant", ...).
    KIUI::ApplyFontFace( font, acfg.m_AnvilUiFontFace );

    return font;
}


WX_AUI_TOOLBAR_ART::WX_AUI_TOOLBAR_ART() :
        wxAuiDefaultToolBarArt()
{
    saturateHighlightColor();

    // Toolbar button text labels (on text-bearing bars) follow the app UI font.
    m_font = anvilChromeFont( m_font );
}


WX_AUI_TAB_ART::WX_AUI_TAB_ART() :
        wxAuiGenericTabArt()
{
    // Notebook tab labels (editor tabs + side-panel tabs) follow the app UI font.  The measuring
    // font must match so tab sizes are computed at the same size they are drawn.
    SetNormalFont( anvilChromeFont( m_normalFont ) );
    SetSelectedFont( anvilChromeFont( m_selectedFont ) );
    SetMeasuringFont( anvilChromeFont( m_measuringFont ) );
}


#if wxCHECK_VERSION( 3, 3, 0 )
wxSize WX_AUI_TOOLBAR_ART::GetToolSize( wxReadOnlyDC& aDc, wxWindow* aWindow,
                                        const wxAuiToolBarItem& aItem )
#else
wxSize WX_AUI_TOOLBAR_ART::GetToolSize( wxDC& aDc, wxWindow* aWindow,
                                        const wxAuiToolBarItem& aItem )
#endif
{
    // Based on the upstream wxWidgets implementation, but simplified for our application.
    // Anvil modern layout: the button cell tracks the (compact) Anvil glyph size + a little
    // breathing room instead of the stock Common setting (24px) — the tighter icon rows of the
    // mockups.  Layout off -> stock cell size.
    int iconPx = Pgm().GetCommonSettings()->m_Appearance.toolbar_icon_size;

    if( ADVANCED_CFG::GetCfg().m_ModernToolbarLayout )
        iconPx = ACTION_TOOLBAR::ANVIL_TOOLBAR_ICON_PX + 6;

    int size = aWindow->FromDIP( iconPx );

    int width = size;
    int height = size;

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.SetFont( m_font );
        int tx, ty;

        if( m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM )
        {
            aDc.GetTextExtent( wxT( "ABCDHgj" ), &tx, &ty );
            height += ty;

            if( !aItem.GetLabel().empty() )
            {
                aDc.GetTextExtent( aItem.GetLabel(), &tx, &ty );
                width = wxMax( width, tx + aWindow->FromDIP( 6 ) );
            }
        }
        else if( m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT )
        {
            width += aWindow->FromDIP( 3 ); // space between left border and bitmap
            width += aWindow->FromDIP( 3 ); // space between bitmap and text

            if( !aItem.GetLabel().empty() )
            {
                aDc.GetTextExtent( aItem.GetLabel(), &tx, &ty );
                width += tx;
                height = wxMax( height, ty );
            }
        }
    }

    if( aItem.HasDropDown() )
    {
        int dropdownWidth = GetElementSize( wxAUI_TBART_DROPDOWN_SIZE );
        width += dropdownWidth + aWindow->FromDIP( 4 );
    }

    return wxSize( width, height );
}


void WX_AUI_TOOLBAR_ART::DrawButton( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem,
                                     const wxRect& aRect )
{
    // Based on upstream implementation
    int bmpX = 0, bmpY = 0;
    int textX = 0, textY = 0;

    const wxBitmap& bmp = aItem.GetCurrentBitmapFor( aWindow );
    const wxSize    bmpSize = bmp.IsOk() ? bmp.GetLogicalSize() : wxSize( 0, 0 );

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.SetFont( m_font );

        int textWidth = 0, textHeight = 0;
        int tx, ty;

        aDc.GetTextExtent( wxT( "ABCDHgj" ), &tx, &textHeight );
        aDc.GetTextExtent( aItem.GetLabel(), &textWidth, &ty );

        if( m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM )
        {
            bmpX = aRect.x + ( aRect.width / 2 ) - ( bmpSize.x / 2 );

            bmpY = aRect.y + ( ( aRect.height - textHeight ) / 2 ) - ( bmpSize.y / 2 );

            textX = aRect.x + ( aRect.width / 2 ) - ( textWidth / 2 ) + 1;
            textY = aRect.y + aRect.height - textHeight - 1;
        }
        else if( m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT )
        {
            bmpX = aRect.x + aWindow->FromDIP( 3 );

            bmpY = aRect.y + ( aRect.height / 2 ) - ( bmpSize.y / 2 );

            textX = bmpX + aWindow->FromDIP( 3 ) + bmpSize.x;
            textY = aRect.y + ( aRect.height / 2 ) - ( textHeight / 2 );
        }
    }
    else
    {
        bmpX = aRect.x + ( aRect.width / 2 ) - ( bmpSize.x / 2 );
        bmpY = aRect.y + ( aRect.height / 2 ) - ( bmpSize.y / 2 );
    }

    bool isThemeDark = KIPLATFORM::UI::IsDarkTheme();

    // Anvil "Vibrant Purple" chrome: the tool-bars are a deep navy and the upstream feedback model
    // *darkens* the highlight colour (factors 20/40/50).  On our dark bar that collapses the
    // hover / pressed / checked background to near-black.  When the purple frame theme is on, lighten
    // toward the accent instead so the feedback reads as a clearly-visible purple that matches the
    // rest of the chrome.  This art provider is installed for every ACTION_TOOLBAR (see
    // action_toolbar.cpp), so the fix applies to every editor.  Theme off -> byte-identical upstream.
    const bool anvilPurple = ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame;

    // Anvil mono chrome icons: every icon is a flat Bone-white glyph and the glyph ITSELF
    // repaints Signal Emerald while hovered/pressed — so the hover/pressed background fills
    // below are skipped (the icon is the feedback).  Applies to the horizontal top bars and
    // the vertical left/right bars alike, in every editor, since this art provider is
    // installed for every ACTION_TOOLBAR.
    const bool anvilMono = anvilPurple && ADVANCED_CFG::GetCfg().m_AnvilMonoIcons;

    const int pressedLightness      = anvilPurple ? 100 : ( isThemeDark ? 20 : 150 );
    const int hoverLightness        = anvilPurple ?  75 : ( isThemeDark ? 40 : 170 );
    const int checkedLightness      = anvilPurple ?  70 : ( isThemeDark ? 40 : 170 );
    const int checkedHoverLightness = anvilPurple ?  90 : ( isThemeDark ? 50 : 180 );

    const bool disabled = aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED;
    const bool pressed  = aItem.GetState() & wxAUI_BUTTON_STATE_PRESSED;
    const bool hovered  = ( aItem.GetState() & wxAUI_BUTTON_STATE_HOVER ) || aItem.IsSticky();
    const bool checked  = aItem.GetState() & wxAUI_BUTTON_STATE_CHECKED;

    // Subtle "on" marker: a faint tint + a thin accent underline instead of the solid filled
    // block.  Every ON toggle (grid, snap, display modes...) used to get the same loud fill as
    // the active tool, so a normal toolbar read as "many tools selected at once" in every editor.
    auto drawCheckedMarker = [&]()
    {
        // The tint has to move AWAY from the bar it sits on.  On the Deep Emerald rows it is a
        // darker emerald (45); on a LIGHT bar -- the aux/value row and the drawing-tools Active
        // Bar in the light theme -- darkening instead paints a near-black block that swallows
        // the ink-dark glyph drawn on top of it, so use the Soft-Oat cream header tone there
        // (an accent-tinted mint block read as a mis-painted cell) and let the accent underline
        // below carry the "on" signal.  In the dark theme CHROME_HEADER stays a subtle
        // near-black step above the bar, so the same branch works in both modes.
        aDc.SetPen( *wxTRANSPARENT_PEN );

        if( anvilPurple )
        {
            aDc.SetBrush( wxBrush( m_anvilDarkBar ? m_highlightColour.ChangeLightness( 45 )
                                                  : wxColour( ANVIL::CHROME_HEADER ) ) );
        }
        else
        {
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( isThemeDark ? 25 : 185 ) ) );
        }

        aDc.DrawRectangle( aRect );

        const int inset = aWindow->FromDIP( 3 );
        const int bar   = aWindow->FromDIP( 2 );
        aDc.SetBrush( wxBrush( m_highlightColour ) );
        aDc.DrawRectangle( wxRect( aRect.x + inset, aRect.y + aRect.height - bar - 1,
                                   aRect.width - 2 * inset, bar ) );
    };

    if( !disabled )
    {
        if( anvilMono )
        {
            // No hover/pressed background block — but keep the checked marker even under the
            // cursor, so an ON toggle doesn't read as OFF the moment it is hovered.
            if( checked )
                drawCheckedMarker();
        }
        else if( pressed )
        {
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( pressedLightness ) ) );
            aDc.DrawRectangle( aRect );
        }
        else if( hovered )
        {
            aDc.SetPen( wxPen( m_highlightColour ) );
            aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( hoverLightness ) ) );

            // draw an even lighter background for checked item hovers (since
            // the hover background is the same color as the check background)
            if( checked )
                aDc.SetBrush( wxBrush( m_highlightColour.ChangeLightness( checkedHoverLightness ) ) );

            aDc.DrawRectangle( aRect );
        }
        else if( checked )
        {
            // it's important to put this code in an else statement after the
            // hover, otherwise hovers won't draw properly for checked items
            ( void ) checkedLightness;
            drawCheckedMarker();
        }
    }

    if( bmp.IsOk() )
    {
        if( anvilMono )
        {
            // Flat mono glyph: Bone-white at rest, Signal Emerald under the cursor / while
            // pressed, dimmed while disabled — matching the title bar and the Project Files
            // tree.  Recoloured at draw time so every bundle scale / DPI stays crisp.
            // Two ink tiers: glyphs on the Deep Emerald tool-bar rows stay bone-white
            // (ICON_*), glyphs on a light bar — the value/aux row in the light theme — go
            // near-black (INK_ICON_*).  Identical values in the dark theme.
            wxColour flat = m_anvilDarkBar ? ANVIL::ICON_IDLE : ANVIL::INK_ICON_IDLE;

            if( disabled )
                flat = m_anvilDarkBar ? ANVIL::ICON_DIM : ANVIL::INK_ICON_DIM;
            else if( pressed || hovered )
                flat = m_anvilDarkBar ? ANVIL::ICON_HOVER : ANVIL::INK_ICON_HOVER;

            aDc.DrawBitmap( KIUI::RecolorFlat( bmp, flat ), bmpX, bmpY, true );
        }
        else
        {
            aDc.DrawBitmap( bmp, bmpX, bmpY, true );
        }
    }

    // set the item's text color based on if it is disabled.  Under the Anvil theme the label
    // has to follow the BAR it is painted on, not the wx system colour: in the light theme the
    // system text colour is near-black, which would be unreadable on the Deep Emerald bar.
    if( anvilPurple )
    {
        aDc.SetTextForeground( m_anvilDarkBar ? ANVIL::ON_BAR : ANVIL::BONE );

        if( disabled )
            aDc.SetTextForeground( m_anvilDarkBar ? ANVIL::ICON_DIM : ANVIL::DIM );
    }
    else
    {
        aDc.SetTextForeground( wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );

        if( aItem.GetState() & wxAUI_BUTTON_STATE_DISABLED )
            aDc.SetTextForeground( wxSystemSettings::GetColour( wxSYS_COLOUR_GRAYTEXT ) );
    }

    if( ( m_flags & wxAUI_TB_TEXT ) && !aItem.GetLabel().empty() )
    {
        aDc.DrawText( aItem.GetLabel(), textX, textY );
    }
}


void WX_AUI_TOOLBAR_ART::saturateHighlightColor()
{
#ifdef __WXOSX__
    // Use a slightly stronger highlight colour over grey toolbar backgrounds
    KIGFX::COLOR4D highlight( m_highlightColour );
    m_highlightColour = highlight.Saturate( 0.6 ).ToColour();
#endif
}


void WX_AUI_TOOLBAR_ART::DrawBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect )
{
    if( m_anvilTheme )
    {
        aDc.SetPen( wxPen( m_anvilBg ) );
        aDc.SetBrush( wxBrush( m_anvilBg ) );
        aDc.DrawRectangle( aRect );
        return;
    }

    wxAuiDefaultToolBarArt::DrawBackground( aDc, aWindow, aRect );
}


void WX_AUI_TOOLBAR_ART::DrawPlainBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect )
{
    // Anvil tool-bars use wxAUI_TB_PLAIN_BACKGROUND, so this (not DrawBackground) paints the bar.
    if( m_anvilTheme )
    {
        aDc.SetPen( wxPen( m_anvilBg ) );
        aDc.SetBrush( wxBrush( m_anvilBg ) );
        aDc.DrawRectangle( aRect );

        // Mockup detail: a 1px hairline closes the horizontal icon rows off from the content
        // below.  Vertical bars skip it — the dock sashes already separate them.
        const bool vertical = aWindow->GetWindowStyleFlag() & wxAUI_TB_VERTICAL;

        if( !vertical )
        {
            aDc.SetPen( wxPen( ANVIL::CHROME_LINE ) );
            aDc.DrawLine( aRect.x, aRect.y + aRect.height - 1, aRect.x + aRect.width,
                          aRect.y + aRect.height - 1 );
        }

        return;
    }

    wxAuiDefaultToolBarArt::DrawPlainBackground( aDc, aWindow, aRect );
}


void WX_AUI_TOOLBAR_ART::UpdateColoursFromSystem()
{
    wxAuiDefaultToolBarArt::UpdateColoursFromSystem();
    saturateHighlightColor();

    // A Windows theme/colour-change event drove the base call above, which reset the colours to
    // the system palette.  Re-pin the Anvil colours so the purple bar survives the event.
    if( m_anvilTheme )
    {
        m_baseColour = m_anvilBg;
        m_highlightColour = m_anvilHighlight;
    }
}


class ToolbarCommandCapture : public wxEvtHandler
{
public:
    ToolbarCommandCapture() { m_lastId = 0; }
    int GetCommandId() const { return m_lastId; }

    bool ProcessEvent( wxEvent& evt ) override
    {
        if( evt.GetEventType() == wxEVT_MENU )
        {
            m_lastId = evt.GetId();
            return true;
        }

        if( GetNextHandler() )
            return GetNextHandler()->ProcessEvent( evt );

        return false;
    }

private:
    int m_lastId;
};


int WX_AUI_TOOLBAR_ART::ShowDropDown( wxWindow* wnd, const wxAuiToolBarItemArray& items )
{
    wxMenu menuPopup;
    bool   skipNextSeparator = true;

    size_t i, count = items.GetCount();
    for( i = 0; i < count; ++i )
    {
        wxAuiToolBarItem& item = items.Item( i );

        if( item.GetKind() == wxITEM_SEPARATOR )
        {
            if( !skipNextSeparator )
            {
                menuPopup.AppendSeparator();
                skipNextSeparator = true;
            }
        }
        else if( item.GetKind() == wxITEM_NORMAL || item.GetKind() == wxITEM_CHECK || item.GetKind() == wxITEM_RADIO )
        {
            wxString text = item.GetShortHelp();

            if( text.empty() )
                text = item.GetLabel();

            if( text.empty() )
                text = wxT( " " );

            wxString firstLine = text.BeforeFirst( '\n' );
            wxString accel;
            wxString label = firstLine.BeforeFirst( '\t', &accel );

            text = label;

            if( !accel.empty() )
            {
                // Remove brackets from accelerator string so it's recognized
                if( accel.starts_with( "(" ) && accel.ends_with( ")" ) )
                    accel = accel.Mid( 1, accel.size() - 2 );

                text << "\t" << accel;
            }

            bool       checked = item.GetState() & wxAUI_BUTTON_STATE_CHECKED;
            wxItemKind menuKind = wxITEM_NORMAL;

            if( ( item.GetKind() == wxITEM_CHECK || item.GetKind() == wxITEM_RADIO ) && checked )
                menuKind = static_cast<wxItemKind>( item.GetKind() );

            wxMenuItem* m = new wxMenuItem( &menuPopup, item.GetId(), text, item.GetShortHelp(), menuKind );

            if( !m->IsCheckable() )
                m->SetBitmap( item.GetBitmapBundle() );

            menuPopup.Append( m );

            if( m->IsCheckable() )
                m->Check( checked );

            skipNextSeparator = false;
        }
    }

    // find out where to put the popup menu of window items
    wxPoint pt = ::wxGetMousePosition();
    pt = wnd->ScreenToClient( pt );

    // find out the screen coordinate at the bottom of the tab ctrl
    wxRect cli_rect = wnd->GetClientRect();
    pt.y = cli_rect.y + cli_rect.height;

    ToolbarCommandCapture* cc = new ToolbarCommandCapture;
    wnd->PushEventHandler( cc );
    wnd->PopupMenu( &menuPopup, pt );
    int command = cc->GetCommandId();
    wnd->PopEventHandler( true );

    return command;
}


WX_AUI_DOCK_ART::WX_AUI_DOCK_ART() :
        wxAuiDefaultDockArt()
{
#if defined( _WIN32 )
    // Dock-pane caption titles ("Properties", "Schematic Hierarchy", ...) in the app UI font
    // (Space Grotesk @ AnvilUiFontPt); wx likes to use "small" (system ~9pt).
    m_captionFont = anvilChromeFont( *wxNORMAL_FONT );

    // Increase the box the caption rests in size a bit
    m_captionSize = ( m_captionFont.GetPointSize() * 7 ) / 4 + 6;
#endif

    SetColour( wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR,
               wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );
    SetColour( wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR,
               wxSystemSettings::GetColour( wxSYS_COLOUR_BTNTEXT ) );

    // Turn off the ridiculous looking gradient
    m_gradientType = wxAUI_GRADIENT_NONE;

    // Anvil mono chrome (dark Anvil frame only): one flat caption style for every pane in every
    // frame (this art provider is installed by EDA_BASE_FRAME for all of them), hairline sashes
    // and 1px pane borders — the aligned caption rows + separator lines of the Anvil mockups.
    m_anvilCaptions = ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame;

    if( m_anvilCaptions )
    {
        m_captionSize = 24;

        SetColour( wxAUI_DOCKART_SASH_COLOUR, ANVIL::CHROME_SASH );
        SetColour( wxAUI_DOCKART_BORDER_COLOUR, ANVIL::CHROME_LINE );
        SetColour( wxAUI_DOCKART_BACKGROUND_COLOUR, ANVIL::CHROME_PANEL );
        SetMetric( wxAUI_DOCKART_SASH_SIZE, 4 );
        SetMetric( wxAUI_DOCKART_PANE_BORDER_SIZE, 1 );
    }
}


void WX_AUI_DOCK_ART::DrawCaption( wxDC& aDc, wxWindow* aWindow, const wxString& aText,
                                   const wxRect& aRect, wxAuiPaneInfo& aPane )
{
    if( !m_anvilCaptions )
    {
        wxAuiDefaultDockArt::DrawCaption( aDc, aWindow, aText, aRect, aPane );
        return;
    }

    // Flat caption strip — identical for active/inactive panes (the mockup has no focus tint) —
    // with a 1px hairline along the bottom so every panel header reads as one aligned system.
    aDc.SetPen( *wxTRANSPARENT_PEN );
    aDc.SetBrush( wxBrush( ANVIL::CHROME_HEADER ) );
    aDc.DrawRectangle( aRect );

    aDc.SetPen( wxPen( ANVIL::CHROME_LINE ) );
    aDc.DrawLine( aRect.x, aRect.y + aRect.height - 1, aRect.x + aRect.width,
                  aRect.y + aRect.height - 1 );

    // Small UPPERCASE grey label, vertically centred.
    wxFont font = m_captionFont;

    if( font.GetFractionalPointSize() > 8.0 )
        font.SetFractionalPointSize( font.GetFractionalPointSize() - 1.5 );

    aDc.SetFont( font );
    aDc.SetTextForeground( ANVIL::CAPTION_TEXT );

    wxCoord tw = 0, th = 0;
    aDc.GetTextExtent( wxT( "ABCDEFHXfgkj" ), &tw, &th );

    wxRect clip = aRect;
    clip.Deflate( aWindow->FromDIP( 3 ), 0 );

    aDc.SetClippingRegion( clip );
    aDc.DrawText( aText.Upper(), aRect.x + aWindow->FromDIP( 8 ),
                  aRect.y + ( aRect.height - th ) / 2 );
    aDc.DestroyClippingRegion();
}


void WX_AUI_TAB_ART::DrawBackground( wxDC& dc, wxWindow* WXUNUSED( wnd ), const wxRect& rect )
{
    // Flat, not the stock gradient -- see the header for why.  m_baseColour is whatever the
    // theme last handed us through SetColour() (CHROME_HEADER), so this needs no palette
    // lookup of its own and stays correct in both themes.
    dc.SetPen( *wxTRANSPARENT_PEN );
    dc.SetBrush( wxBrush( m_baseColour ) );
    dc.DrawRectangle( rect );

    dc.SetPen( wxPen( ANVIL::CHROME_LINE ) );
    dc.DrawLine( rect.x, rect.y + rect.height - 1, rect.x + rect.width, rect.y + rect.height - 1 );
}


void WX_AUI_TAB_ART::DrawTab( wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& page, const wxRect& in_rect,
                              int close_button_state, wxRect* out_tab_rect, wxRect* out_button_rect,
                              int* x_extent )
{
    PANEL_NOTEBOOK_BASE* panel = dynamic_cast<PANEL_NOTEBOOK_BASE*>( page.window );

    if( panel && !panel->GetClosable() )
        close_button_state = wxAUI_BUTTON_STATE_HIDDEN;

    return wxAuiGenericTabArt::DrawTab( dc, wnd, page, in_rect, close_button_state, out_tab_rect,
                                        out_button_rect, x_extent );
}
