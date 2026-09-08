/*
* This program source code file is part of Anvil, a free EDA CAD application.
*
* Copyright (C) 2020 Mark Roszko <mark.roszko@gmail.com>
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

#include <kiplatform/app.h>
#include <kiplatform/anvil_theme.h>

#include <wx/app.h>
#include <wx/bitmap.h>
#include <wx/brush.h>
#include <wx/checkbox.h>
#include <wx/dc.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/pen.h>
#include <wx/radiobut.h>
#include <wx/renderer.h>
#include <wx/string.h>
#include <wx/window.h>
#if wxCHECK_VERSION( 3, 3, 0 )
#include <wx/msw/darkmode.h>
#endif

#include <windows.h>
#include <strsafe.h>
#include <config.h>
#include <versionhelpers.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cstdio>
#include <memory>
#include <unordered_map>

#if defined( _MSC_VER )
#include <werapi.h>     // issues on msys2
#endif

#ifdef _WIN32
extern "C"
{
    // So there exists this malware called Nahimic by A-Volute, which is marketed as an audio enhancement
    // software. In reality it's an aggressive form of malware that injects itself wildly into every process
    // on the system for god knows what reason. It even includes a tracking/analytics package, <insert tinfoil hat>
    // Our problem is this garbage basically bugs out OpenGL (why an audio driver does that, who knows, its made by morons)
    // And then we get issues reported both in our issue tracker and sentry reports as a result
    // At least these malware authors were nice to include a dumb "disable" trick where it checks if the exe is exporting
    // a symbol called NoHotPatch, so here we are.
    // Hopefully this works and stops the bug reports. Apparently the worst part is this malware aggressively gets reinstalled
    // by awful low-tier motherboard vendors like MSI, Alienware and others who bundled it into their driver packages
    // and distributed it over Windows Update
    // Did I mention they clearly had issues with other apps so instead of fixing their malware, they blacklisted a hundred common
    // apps and even some games in their own config? Obviously kicad isn't on that blacklist :(
    // This malware seems to no longer be distributed as Nahimic and replaced with "Sonar" by SteelSeries.
    // Time will tell if it's the same garbage, I'm not volunteering to install it.
    __declspec(dllexport) void NoHotPatch()
    {
        // this is a intentionally empty function
        return;
    }
}
#endif

// Anvil "Vibrant Purple & Indigo" frame theme.  Set by SetDarkModePurple() before EnableDarkMode().
static bool g_anvilPurpleDark = false;

// Anvil live theme flip: true while the LIGHT theme is active.  wxMSW's dark mode can only be
// established once, before the first window exists (MSWEnableDarkMode() refuses to run after
// that), so Anvil enables it unconditionally at start-up and then steers the OS-level per-app
// mode at runtime — see SetLiveDarkMode() below.  While the light theme is on, the settings
// object keeps answering wx's colour queries with the CLASSIC light system palette, so
// everything wx paints through wxSystemSettings comes out identical to a never-dark-moded app.
static bool g_anvilLiveLight = false;

#if wxCHECK_VERSION( 3, 3, 0 )
/// The classic (light) system colour for a wxSYS_COLOUR_* index, bypassing wx's dark-mode
/// override.  ::GetSysColor() always reports the un-themed light palette, which is exactly what
/// a light-started wx app would have used.
static wxColour anvilClassicSysColour( wxSystemColour aIndex )
{
    // wx pseudo-colours that have no Win32 COLOR_* slot: map to their classic base.
    switch( aIndex )
    {
    case wxSYS_COLOUR_LISTBOX:              aIndex = wxSYS_COLOUR_WINDOW; break;
    case wxSYS_COLOUR_LISTBOXTEXT:          aIndex = wxSYS_COLOUR_WINDOWTEXT; break;
    case wxSYS_COLOUR_LISTBOXHIGHLIGHT:     aIndex = wxSYS_COLOUR_HIGHLIGHT; break;
    case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT: aIndex = wxSYS_COLOUR_HIGHLIGHTTEXT; break;
    default: break;
    }

    // The first block of wxSYS_COLOUR_* values mirrors the Win32 COLOR_* indices one-to-one.
    int win32Index = static_cast<int>( aIndex );

    if( win32Index < 0 || win32Index > COLOR_MENUBAR )
        win32Index = COLOR_BTNFACE;

    const DWORD rgb = ::GetSysColor( win32Index );

    return wxColour( GetRValue( rgb ), GetGValue( rgb ), GetBValue( rgb ) );
}


class KICAD_DARK_MODE_SETTINGS : public wxDarkModeSettings
{
public:
    wxColour GetColour( wxSystemColour index ) override
    {
        // Live light theme: wx is stuck in "dark mode enabled" for the life of the process, so
        // it asks this object for every colour — answer with the classic light system palette.
        if( g_anvilLiveLight )
            return anvilClassicSysColour( index );

        if( g_anvilPurpleDark )
        {
            // Anvil chrome palette.  Three distinct surface levels + a visible border so adjacent
            // regions (lists/tree/text vs. panel faces vs. dialogs) don't blend into one dark blob:
            //   content  = deepest  (text fields, lists, tree, list/combo popups)
            //   panel    = mid      (dialog & panel faces, buttons, tool-bars, menus)
            //   border   = light    (control / group outlines, so panels read as separate)
            // Single source of truth = kiplatform/anvil_theme.h (namespace ANVIL).  These are
            // references, not new literals, so the palette is defined in exactly one place.
            const wxColour& content  = ANVIL::CONTENT;      // NEMI Black Ground — content/data areas
            const wxColour& panel    = ANVIL::PANEL;        // NEMI Warm Graphite — panel & dialog faces
            const wxColour& border   = ANVIL::BORDER;       // NEMI emerald edge — visible separators
            const wxColour& accent   = ANVIL::ACCENT;       // NEMI Signal Emerald highlight
            const wxColour& text     = ANVIL::BONE;         // NEMI Bone
            const wxColour& capAct   = ANVIL::CAP_ACTIVE;   // NEMI active pane caption (deep emerald)
            const wxColour& capInact = ANVIL::CAP_INACTIVE; // NEMI inactive pane caption

            switch( index )
            {
            case wxSYS_COLOUR_WINDOW:
            case wxSYS_COLOUR_LISTBOX:
                return content;

            case wxSYS_COLOUR_BTNFACE:   // == wxSYS_COLOUR_3DFACE
                return panel;

            // Dropdown / context-menu background — the lighter "premium purple" the user wants.
            // (Some wx/Windows paths colour popups from here rather than GetMenuColour().)
            case wxSYS_COLOUR_MENU:
            case wxSYS_COLOUR_MENUBAR:
                return ANVIL::POPUP_BG;   // NEMI emerald popup/menu bg

            // Control edges / separators / group-box outlines — lift them so they're visible.
            case wxSYS_COLOUR_3DLIGHT:
                return ANVIL::HOVER;

            case wxSYS_COLOUR_WINDOWFRAME:
            case wxSYS_COLOUR_ACTIVEBORDER:
            case wxSYS_COLOUR_INACTIVEBORDER:
                return border;

            case wxSYS_COLOUR_WINDOWTEXT:
            case wxSYS_COLOUR_BTNTEXT:
            case wxSYS_COLOUR_LISTBOXTEXT:
            case wxSYS_COLOUR_MENUTEXT:
            case wxSYS_COLOUR_CAPTIONTEXT:
                return text;

            // Dim (disabled/secondary) text — still readable, not muddy.
            case wxSYS_COLOUR_GRAYTEXT:
                return ANVIL::DIM;

            case wxSYS_COLOUR_HIGHLIGHT:
                return accent;

            case wxSYS_COLOUR_HIGHLIGHTTEXT:
                return ANVIL::ON_ACCENT;

            // Selected row inside list/combo dropdown popups (wxListBox-based combos: net
            // selector, font picker, filter combos).  Without these the picked row falls back
            // to the generic-dark default instead of emerald.
            case wxSYS_COLOUR_LISTBOXHIGHLIGHT:
                return accent;

            case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
                return ANVIL::ON_ACCENT;

            // Hyperlinks (HTML report / help panels) + owner-drawn menu-hover paths.  The
            // default here is a generic blue — pin both to the emerald accent.
            case wxSYS_COLOUR_HOTLIGHT:
            case wxSYS_COLOUR_MENUHILIGHT:
                return accent;

            // Info / warning bar (ERC/DRC/save/load banners) reads INFOBK/INFOTEXT.  The
            // Windows default INFOBK is a pale yellow that ignores dark mode — pin to panel.
            case wxSYS_COLOUR_INFOBK:
                return panel;

            case wxSYS_COLOUR_INFOTEXT:
                return text;

            // AUI gutter behind / between docked panes.  Mapping it here makes EVERY editor
            // frame read emerald, not just the two that override dock-art colours by hand.
            case wxSYS_COLOUR_APPWORKSPACE:
                return content;

            // Docked-pane caption (title) bars.  Feeds wxAuiDefaultDockArt in the editor
            // frames (PCB, footprint, symbol, gerbview, pl_editor, 3D, sim, cvpcb) that do
            // not set dock-art colours themselves.
            case wxSYS_COLOUR_ACTIVECAPTION:
            case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
                return capAct;

            case wxSYS_COLOUR_INACTIVECAPTION:
            case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
                return capInact;

            default:
                return wxDarkModeSettings::GetColour( index );
            }
        }

        switch( index )
        {
            // This fixes "Control Light"
        case wxSYS_COLOUR_3DLIGHT:
            return wxColour( 0x2B2B2B );

        default: return wxDarkModeSettings::GetColour( index );
        }
    }

    wxColour GetMenuColour( wxMenuColour which ) override
    {
        // Live light theme: classic light menu colours (see GetColour above).
        if( g_anvilLiveLight )
        {
            switch( which )
            {
            case wxMenuColour::StandardBg: return anvilClassicSysColour( wxSYS_COLOUR_MENU );
            case wxMenuColour::StandardFg: return anvilClassicSysColour( wxSYS_COLOUR_MENUTEXT );
            case wxMenuColour::HotBg:      return anvilClassicSysColour( wxSYS_COLOUR_MENUHILIGHT );
            case wxMenuColour::DisabledFg: return anvilClassicSysColour( wxSYS_COLOUR_GRAYTEXT );
            }
        }

        if( g_anvilPurpleDark )
        {
            switch( which )
            {
            // Lighter "premium purple" dropdown / context-menu popups (flat; native menus can't
            // do gradient/glass).  Popups only — the menu *bar* and the rest of the chrome keep
            // the darker indigo from GetColour() above.
            case wxMenuColour::StandardBg: return ANVIL::POPUP_BG;  // NEMI emerald popup bg
            case wxMenuColour::StandardFg: return ANVIL::BONE;      // NEMI Bone text
            case wxMenuColour::HotBg:      return ANVIL::ACCENT;    // NEMI Signal Emerald hover
            case wxMenuColour::DisabledFg: return ANVIL::DIM_MENU;  // dim, readable
            }
        }

        return wxDarkModeSettings::GetMenuColour( which );
    }

    wxPen GetBorderPen() override
    {
        // Live light theme: an invalid pen tells wx to draw the normal system border.
        if( g_anvilLiveLight )
            return wxPen();

        if( g_anvilPurpleDark )
            return wxPen( ANVIL::BORDER );   // visible group-box / static-box outline

        return wxDarkModeSettings::GetBorderPen();
    }
};


// ======================= Anvil green check-box / radio glyphs ==================================
//
// The check and radio glyphs Windows draws are baked into the OS visual style (blue-ish in the
// light style, the user's accent colour in dark) and follow neither the ANVIL palette nor the
// live theme toggle.  But every such glyph in the app is requested through ONE funnel,
// wxRendererNative::Get():
//
//   - native wxCheckBox / wxRadioButton: wx's always-on dark mode stamps a foreground colour on
//     them at creation (src/msw/control.cpp), which flips them to BS_OWNERDRAW, and the
//     owner-draw path renders the glyph via DrawCheckBox() / DrawRadioBitmap();
//   - wxGrid bool cells (GRID_CELL_CHECKBOX_RENDERER), wxCheckListBox rows, wxDataView toggles
//     and wxPropertyGrid bool editors all call DrawCheckBox() directly.
//
// So replacing the process-wide renderer recolours every check & radio in the application:
// Signal Emerald (ANVIL::ACCENT — identical in both themes) for the checked state, with the
// well / edge colours taken from the live palette so the theme toggle just works.  Everything
// else stays delegated to the real MSW renderer.

enum class ANVIL_GLYPH
{
    CHECKBOX,   ///< box + tick (or bar for the indeterminate state)
    RADIO,      ///< ring + dot
    CHECKMARK   ///< bare tick, no box (wxRendererNative::DrawCheckMark)
};


static wxBitmap anvilGlyphBitmap( ANVIL_GLYPH aKind, int aW, int aH, int aFlags )
{
    const bool checked  = ( aFlags & wxCONTROL_CHECKED ) != 0;
    const bool undet    = ( aFlags & wxCONTROL_UNDETERMINED ) != 0;
    const bool disabled = ( aFlags & wxCONTROL_DISABLED ) != 0;
    const bool hot      = ( aFlags & ( wxCONTROL_CURRENT | wxCONTROL_PRESSED ) ) != 0;

    // The palette is a handful of constants per (mode, size, state), so cache the rendered
    // bitmaps — grids repaint one glyph per row.  The mode bit keys the light/dark palette.
    static std::unordered_map<uint32_t, wxBitmap> s_cache;

    const uint32_t key = uint32_t( aW & 0xFFF ) | uint32_t( aH & 0xFFF ) << 12
                         | uint32_t( checked ) << 24 | uint32_t( undet ) << 25
                         | uint32_t( disabled ) << 26 | uint32_t( hot ) << 27
                         | uint32_t( aKind ) << 28 | uint32_t( ANVIL::IsLight() ) << 30;

    if( auto it = s_cache.find( key ); it != s_cache.end() )
        return it->second;

    // Colour roles.  The well (unchecked interior) is CONTENT so the glyph reads as a small
    // content field on the panel it sits on, in both themes; the checked fill is the brand
    // accent, lightened a step for hover/pressed and desaturated for disabled.
    const wxColour well = ANVIL::CONTENT;
    const wxColour tick = ANVIL::ON_ACCENT;
    wxColour       fill = ANVIL::ACCENT;
    wxColour       edge = ANVIL::CAPTION_TEXT;

    if( disabled )
    {
        fill = ANVIL::DIM;
        edge = ANVIL::BORDER;
    }
    else if( hot )
    {
        fill = fill.ChangeLightness( 115 );
        edge = ANVIL::ACCENT;
    }

    wxImage img( aW, aH );
    img.InitAlpha();
    memset( img.GetAlpha(), 0, static_cast<size_t>( aW ) * aH );

    {
        std::unique_ptr<wxGraphicsContext> gc( wxGraphicsContext::Create( img ) );

        if( !gc )
            return wxNullBitmap;    // caller falls back to the native renderer

        gc->SetAntialiasMode( wxANTIALIAS_DEFAULT );

        const double s = std::min( aW, aH );        // glyphs are square, centred in the rect
        const double x = ( aW - s ) / 2.0;
        const double y = ( aH - s ) / 2.0;

        const double lineW = std::max( 1.0, s / 13.0 );          // unchecked outline
        const double tickW = std::max( 1.6, s * 0.15 );          // tick / bar stroke

        auto strokeTick = [&]( double aInsetX, double aInsetY, double aSpan )
        {
            // aInsetX/aInsetY position the tick's bounding square, aSpan is its size.
            const wxPoint2DDouble pts[3] = {
                { aInsetX + 0.22 * aSpan, aInsetY + 0.54 * aSpan },
                { aInsetX + 0.42 * aSpan, aInsetY + 0.73 * aSpan },
                { aInsetX + 0.78 * aSpan, aInsetY + 0.31 * aSpan },
            };

            gc->StrokeLines( 3, pts );
        };

        switch( aKind )
        {
        case ANVIL_GLYPH::CHECKBOX:
        {
            const double rad = s * 0.18;    // corner radius, Win11-ish rounding

            if( checked || undet )
            {
                gc->SetBrush( wxBrush( fill ) );
                gc->SetPen( *wxTRANSPARENT_PEN );
                gc->DrawRoundedRectangle( x + 0.5, y + 0.5, s - 1.0, s - 1.0, rad );

                gc->SetPen( gc->CreatePen( wxGraphicsPenInfo( tick, tickW )
                                                   .Cap( wxCAP_ROUND )
                                                   .Join( wxJOIN_ROUND ) ) );

                if( undet )
                {
                    const wxPoint2DDouble bar[2] = { { x + 0.28 * s, y + 0.50 * s },
                                                     { x + 0.72 * s, y + 0.50 * s } };
                    gc->StrokeLines( 2, bar );
                }
                else
                {
                    strokeTick( x, y, s );
                }
            }
            else
            {
                gc->SetBrush( wxBrush( well ) );
                gc->SetPen( gc->CreatePen( wxGraphicsPenInfo( edge, lineW ) ) );
                gc->DrawRoundedRectangle( x + lineW / 2.0 + 0.5, y + lineW / 2.0 + 0.5,
                                          s - lineW - 1.0, s - lineW - 1.0, rad );
            }

            break;
        }

        case ANVIL_GLYPH::RADIO:
        {
            const double cx = aW / 2.0;
            const double cy = aH / 2.0;

            if( checked )
            {
                // Emerald ring around a well-coloured gap, emerald centre dot.
                const double ringW = std::max( 1.6, s * 0.14 );
                const double r     = ( s - ringW ) / 2.0 - 0.5;

                gc->SetBrush( wxBrush( well ) );
                gc->SetPen( gc->CreatePen( wxGraphicsPenInfo( fill, ringW ) ) );
                gc->DrawEllipse( cx - r, cy - r, 2.0 * r, 2.0 * r );

                const double rd = r * 0.5;

                gc->SetBrush( wxBrush( fill ) );
                gc->SetPen( *wxTRANSPARENT_PEN );
                gc->DrawEllipse( cx - rd, cy - rd, 2.0 * rd, 2.0 * rd );
            }
            else
            {
                const double r = ( s - lineW ) / 2.0 - 0.5;

                gc->SetBrush( wxBrush( well ) );
                gc->SetPen( gc->CreatePen( wxGraphicsPenInfo( edge, lineW ) ) );
                gc->DrawEllipse( cx - r, cy - r, 2.0 * r, 2.0 * r );
            }

            break;
        }

        case ANVIL_GLYPH::CHECKMARK:
        {
            // A bare tick sits on the surrounding surface, so it is drawn in the accent
            // itself — white would vanish on a light panel.
            gc->SetPen( gc->CreatePen( wxGraphicsPenInfo( fill, tickW )
                                               .Cap( wxCAP_ROUND )
                                               .Join( wxJOIN_ROUND ) ) );
            strokeTick( x, y, s );
            break;
        }
        }
    }

    wxBitmap bmp( img );
    s_cache.emplace( key, bmp );

    return bmp;
}


class ANVIL_GLYPH_RENDERER : public wxDelegateRendererNative
{
public:
    // Delegate everything not overridden here to the REAL platform renderer (the default
    // wxDelegateRendererNative ctor would delegate to the generic one).
    ANVIL_GLYPH_RENDERER() : wxDelegateRendererNative( wxRendererNative::GetDefault() ) {}

    void DrawCheckBox( wxWindow* aWin, wxDC& aDC, const wxRect& aRect, int aFlags = 0 ) override
    {
        if( !drawGlyph( ANVIL_GLYPH::CHECKBOX, aDC, aRect, aFlags ) )
            wxDelegateRendererNative::DrawCheckBox( aWin, aDC, aRect, aFlags );
    }

    void DrawCheckMark( wxWindow* aWin, wxDC& aDC, const wxRect& aRect, int aFlags = 0 ) override
    {
        if( !drawGlyph( ANVIL_GLYPH::CHECKMARK, aDC, aRect, aFlags ) )
            wxDelegateRendererNative::DrawCheckMark( aWin, aDC, aRect, aFlags );
    }

    void DrawRadioBitmap( wxWindow* aWin, wxDC& aDC, const wxRect& aRect, int aFlags = 0 ) override
    {
        if( !drawGlyph( ANVIL_GLYPH::RADIO, aDC, aRect, aFlags ) )
            wxDelegateRendererNative::DrawRadioBitmap( aWin, aDC, aRect, aFlags );
    }

private:
    static bool drawGlyph( ANVIL_GLYPH aKind, wxDC& aDC, const wxRect& aRect, int aFlags )
    {
        if( aRect.width <= 0 || aRect.height <= 0 )
            return true;    // nothing to draw, but nothing for the native renderer either

        wxBitmap bmp = anvilGlyphBitmap( aKind, aRect.width, aRect.height, aFlags );

        if( !bmp.IsOk() )
            return false;

        aDC.DrawBitmap( bmp, aRect.x, aRect.y, true );

        return true;
    }
};


// wx's always-on dark mode stamps a foreground colour on every wxCheckBox / wxRadioButton at
// creation (src/msw/control.cpp: SetForegroundColour(wxSYS_COLOUR_LISTBOXTEXT)) — that is what
// makes them owner-drawn.  The colour is derived ONCE, from the theme active at creation, so a
// live flip would leave stale label text on controls that outlive it.  Re-derive it here, but
// ONLY when the current colour is one of the known theme-stamped values — a colour a dialog
// set on purpose (warning red etc.) is left alone.
static void anvilRepinOwnerDrawnButtonText( wxWindow* aWin )
{
    if( wxDynamicCast( aWin, wxCheckBox ) || wxDynamicCast( aWin, wxRadioButton ) )
    {
        const wxColour cur = aWin->GetForegroundColour();

        if( cur == ANVIL::BONE_For( ANVIL::MODE::DARK )
            || cur == ANVIL::BONE_For( ANVIL::MODE::LIGHT )
            || cur == anvilClassicSysColour( wxSYS_COLOUR_WINDOWTEXT ) )
        {
            aWin->SetForegroundColour( wxSystemSettings::GetColour( wxSYS_COLOUR_LISTBOXTEXT ) );
        }
    }

    for( wxWindow* child : aWin->GetChildren() )
        anvilRepinOwnerDrawnButtonText( child );
}
#endif


bool KIPLATFORM::APP::Init()
{
#if defined( _MSC_VER ) && defined( DEBUG )
    // wxWidgets turns on leak dumping in debug but its "flawed" and will falsely dump
    // for half a hour _CRTDBG_ALLOC_MEM_DF is the usual default for MSVC.
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF );
#endif

#if defined( DEBUG )
    // undo wxwidgets trying to hide errors
    SetErrorMode( 0 );
#else
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX );
#endif

    // remove CWD from the dll search paths
    // just the smallest of security tweaks as we do load DLLs on demand
    SetDllDirectory( wxT( "" ) );

    // Moves the CWD to the end of the search list for spawning processes
    SetSearchPathMode( BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE | BASE_SEARCH_PATH_PERMANENT );

    // In order to support GUI and CLI
    // Let's attach to console when it's possible, or allocate if requested.
    AttachConsole( wxGetEnv( wxS( "KICAD_ALLOC_CONSOLE" ), nullptr ) );

    // It may be useful to log up to traces in a console, but in Release builds the log level changes to Info
    // Also we have to force the active target to stderr or else it goes to the void
    bool forceLog = wxGetEnv( wxS( "KICAD_FORCE_CONSOLE_TRACE" ), nullptr );

    if( forceLog )
    {
        wxLog::EnableLogging( true );
#ifndef DEBUG
        wxLog::SetLogLevel( wxLOG_Trace );
#endif
        wxLog::SetActiveTarget( new wxLogStderr );
    }

    return true;
}


void KIPLATFORM::APP::EnableDarkMode( bool aForce )
{
#if wxCHECK_VERSION( 3, 3, 0 )
    wxTheApp->MSWEnableDarkMode( aForce ? wxApp::DarkMode_Always : wxApp::DarkMode_Auto, new KICAD_DARK_MODE_SETTINGS() );

    // Emerald check-box / radio glyphs in both themes — see ANVIL_GLYPH_RENDERER above.
    //
    // Get() FIRST, and not for its return value: wxRendererNative::Set() does not mark the
    // global renderer slot as initialised (src/common/rendcmn.cpp), so on the next Get() the
    // slot still believes it has to create the platform renderer — and that lazy init RESETS
    // the unique_ptr, deleting whatever Set() had just put there.  Setting without this call
    // silently loses the renderer at the first repaint (symptom: stock blue glyphs).  One
    // Get() runs the lazy init while the slot is still empty, after which Set() sticks.
    wxRendererNative::Get();

    // Set() hands back ownership of the renderer it replaced; nothing else refers to it.
    delete wxRendererNative::Set( new ANVIL_GLYPH_RENDERER() );
#endif
}


void KIPLATFORM::APP::SetDarkModePurple( bool aOn )
{
    g_anvilPurpleDark = aOn;
}


void KIPLATFORM::APP::SetLiveDarkMode( bool aDark )
{
#if wxCHECK_VERSION( 3, 3, 0 )
    g_anvilLiveLight = !aDark;

    // The same undocumented uxtheme entry points wx itself loads to establish dark mode
    // (ordinals 132-136, stable since Win10 1809).  wx refuses to change its mode once a
    // window exists, so the runtime flip drives them directly: the per-app preferred mode
    // decides how native menus, scrollbars and control themes render, app-wide.
    enum ANVIL_APP_MODE
    {
        ANVIL_APP_MODE_DEFAULT     = 0,
        ANVIL_APP_MODE_ALLOW_DARK  = 1,
        ANVIL_APP_MODE_FORCE_DARK  = 2,
        ANVIL_APP_MODE_FORCE_LIGHT = 3
    };

    typedef int( WINAPI* SET_PREFERRED_APP_MODE )( int );
    typedef void( WINAPI* FLUSH_MENU_THEMES )();
    typedef HRESULT( WINAPI* DWM_SET_WINDOW_ATTRIBUTE )( HWND, DWORD, LPCVOID, DWORD );

    static SET_PREFERRED_APP_MODE   s_setPreferredAppMode = nullptr;
    static FLUSH_MENU_THEMES        s_flushMenuThemes = nullptr;
    static DWM_SET_WINDOW_ATTRIBUTE s_dwmSetWindowAttribute = nullptr;
    static bool                     s_resolved = false;

    if( !s_resolved )
    {
        s_resolved = true;

        if( HMODULE uxtheme = ::LoadLibraryExW( L"uxtheme.dll", nullptr,
                                                LOAD_LIBRARY_SEARCH_SYSTEM32 ) )
        {
            s_setPreferredAppMode = reinterpret_cast<SET_PREFERRED_APP_MODE>(
                    ::GetProcAddress( uxtheme, MAKEINTRESOURCEA( 135 ) ) );
            s_flushMenuThemes = reinterpret_cast<FLUSH_MENU_THEMES>(
                    ::GetProcAddress( uxtheme, MAKEINTRESOURCEA( 136 ) ) );
        }

        if( HMODULE dwm = ::LoadLibraryExW( L"dwmapi.dll", nullptr,
                                            LOAD_LIBRARY_SEARCH_SYSTEM32 ) )
        {
            s_dwmSetWindowAttribute = reinterpret_cast<DWM_SET_WINDOW_ATTRIBUTE>(
                    ::GetProcAddress( dwm, "DwmSetWindowAttribute" ) );
        }
    }

    if( s_setPreferredAppMode )
        s_setPreferredAppMode( aDark ? ANVIL_APP_MODE_FORCE_DARK : ANVIL_APP_MODE_FORCE_LIGHT );

    if( s_flushMenuThemes )
        s_flushMenuThemes();

    // Re-theme every window that already exists: the caption colour is per-window DWM state,
    // and themed parts (scrollbars, native control frames) only re-resolve their visual style
    // on WM_THEMECHANGED.
    const BOOL darkCaption = aDark ? TRUE : FALSE;

    for( wxWindow* wnd : wxTopLevelWindows )
    {
        // The label colour wx stamped on check-boxes / radio buttons at creation belongs to
        // the theme that was active back then — re-derive it for the new one (glyphs follow
        // the palette by themselves via ANVIL_GLYPH_RENDERER).
        anvilRepinOwnerDrawnButtonText( wnd );

        HWND hwnd = static_cast<HWND>( wnd->GetHandle() );

        if( !hwnd )
            continue;

        if( s_dwmSetWindowAttribute )
        {
            s_dwmSetWindowAttribute( hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                                     &darkCaption, sizeof( darkCaption ) );
        }

        ::SendMessageW( hwnd, WM_THEMECHANGED, 0, 0 );

        ::EnumChildWindows( hwnd,
                []( HWND aChild, LPARAM ) -> BOOL
                {
                    // Hidden branches — the pre-warmed editor frames docked as unshown tabs
                    // carry hundreds of native controls each — re-resolve their theme
                    // asynchronously: a POSTED message is processed before they can next
                    // paint, and the synchronous sweep then only pays for what is on screen.
                    if( ::IsWindowVisible( aChild ) )
                        ::SendMessageW( aChild, WM_THEMECHANGED, 0, 0 );
                    else
                        ::PostMessageW( aChild, WM_THEMECHANGED, 0, 0 );

                    return TRUE;
                },
                0 );

        ::RedrawWindow( hwnd, nullptr, nullptr,
                        RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN );
    }
#else
    ( void ) aDark;
#endif
}


bool KIPLATFORM::APP::AttachConsole( bool aTryAlloc )
{
    if( ::AttachConsole( ATTACH_PARENT_PROCESS ) || ( aTryAlloc && ::AllocConsole() ) )
    {
        #if !defined( __MINGW32__ ) // These redirections create problems on mingw:
                                    // Nothing is printed to the console

        if( ::GetStdHandle( STD_INPUT_HANDLE ) != INVALID_HANDLE_VALUE )
        {
            freopen( "CONIN$", "r", stdin );
            setvbuf( stdin, NULL, _IONBF, 0 );
        }

        if( ::GetStdHandle( STD_OUTPUT_HANDLE ) != INVALID_HANDLE_VALUE )
        {
            freopen( "CONOUT$", "w", stdout );
            setvbuf( stdout, NULL, _IONBF, 0 );
        }

        if( ::GetStdHandle( STD_ERROR_HANDLE ) != INVALID_HANDLE_VALUE )
        {
            freopen( "CONOUT$", "w", stderr );
            setvbuf( stderr, NULL, _IONBF, 0 );
        }
        #endif

        std::ios::sync_with_stdio( true );

        std::wcout.clear();
        std::cout.clear();
        std::wcerr.clear();
        std::cerr.clear();
        std::wcin.clear();
        std::cin.clear();

        return true;
    }

    return false;
}


bool KIPLATFORM::APP::IsOperatingSystemUnsupported()
{
#if defined( PYTHON_VERSION_MAJOR ) && ( ( PYTHON_VERSION_MAJOR == 3 && PYTHON_VERSION_MINOR >= 8 ) \
             || PYTHON_VERSION_MAJOR > 3 )
    // Python 3.8 switched to Windows 8+ API, we do not support Windows 7 and will not
    // attempt to hack around it. A normal user will never get here because the Python DLL
    // is missing dependencies - and because it is not dynamically loaded, Anvil will not even
    // start without patching Python or its WinAPI dependency. This is just to create a nag dialog
    // for those who run patched Python and prevent them from submitting bug reports.
    return !IsWindows8OrGreater();
#else
    return false;
#endif
}


bool KIPLATFORM::APP::RegisterApplicationRestart( const wxString& aCommandLine )
{
    // Command line arguments with spaces require quotes.
    wxString restartCmd = wxS( "\"" ) + aCommandLine + wxS( "\"" );

    // Ensure we don't exceed the maximum allowable size
    if( restartCmd.length() > RESTART_MAX_CMD_LINE - 1 )
    {
        return false;
    }

    HRESULT hr = S_OK;

    hr = ::RegisterApplicationRestart( restartCmd.wc_str(), RESTART_NO_PATCH );

    return SUCCEEDED( hr );
}


bool KIPLATFORM::APP::UnregisterApplicationRestart()
{
    // Note, this isn't required to be used on Windows if you are just closing the program
    return SUCCEEDED( ::UnregisterApplicationRestart() );
}


bool KIPLATFORM::APP::SupportsShutdownBlockReason()
{
    return true;
}


void KIPLATFORM::APP::RemoveShutdownBlockReason( wxWindow* aWindow )
{
    // Destroys any block reason that may have existed
    ShutdownBlockReasonDestroy( aWindow->GetHandle() );
}


void KIPLATFORM::APP::SetShutdownBlockReason( wxWindow* aWindow, const wxString& aReason )
{
    // Sets up the pretty message on the shutdown page on why it's being "blocked"
    // This is used in conjunction with handling WM_QUERYENDSESSION (wxCloseEvent)
    // ShutdownBlockReasonCreate does not block by itself

    ShutdownBlockReasonDestroy( aWindow->GetHandle() ); // Destroys any existing or nonexisting reason

    ShutdownBlockReasonCreate( aWindow->GetHandle(), aReason.wc_str() );
}


void KIPLATFORM::APP::ForceTimerMessagesToBeCreatedIfNecessary()
{
    // Taken from https://devblogs.microsoft.com/oldnewthing/20191108-00/?p=103080
    MSG msg;
    PeekMessage( &msg, nullptr, WM_TIMER, WM_TIMER, PM_NOREMOVE );
}


void KIPLATFORM::APP::AddDynamicLibrarySearchPath( const wxString& aPath )
{
    SetDllDirectory( aPath.c_str() );
}
