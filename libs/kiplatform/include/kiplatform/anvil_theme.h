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

#ifndef KIPLATFORM_ANVIL_THEME_H_
#define KIPLATFORM_ANVIL_THEME_H_

#include <wx/colour.h>

/**
 * NEMI LMM emerald CHROME palette — the SINGLE SOURCE OF TRUTH for the app-wide UI colors.
 *
 * These are the exact same values that used to be retyped as literals in app.cpp,
 * kicad_manager_frame.cpp and sch_edit_frame.cpp; every one of those call sites now references
 * a constant here instead, so a colour is defined in exactly one place.  Changing a colour is a
 * one-line edit (then rebuild).
 *
 * This header lives in kiplatform (the lowest layer) so the Windows dark-mode palette in
 * libs/kiplatform/os/windows/app.cpp can share it too — kiplatform cannot include from common.
 *
 * TWO MODES (2026-08 light-theme pass).  Every token carries a DARK value and a LIGHT value and
 * the whole palette flips in one call, ANVIL::SetMode().  The tokens are role-named, so a call
 * site never has to know which mode is active: it asks for "the text on a panel" (BONE) or "the
 * glyph on the emerald bar" (ON_BAR) and gets the right colour for the current theme.
 *
 * The light theme is the brand swatch: a Deep Emerald #0B4A37 menu / tool-bar band over Soft-Oat
 * #EAE7DB chrome, with white reserved for CONTENT (lists, trees, text fields and the canvas).
 * So anything painted ON those bars — ON_BAR, ICON_IDLE/HOVER/DIM, BAR_HOVER — keeps its
 * light-on-emerald value in BOTH modes, while everything painted on a panel or the title strip
 * — BONE, INK_ICON_IDLE/HOVER/DIM, CAPTION_TEXT — flips to ink-on-oat.
 *
 * DLL NOTE: these are plain inline globals, so every module (anvilcad.exe, kicommon.dll, each
 * _kiface DLL) gets its OWN copy.  That is deliberate and harmless as long as every module calls
 * SetMode() once early — see KIUI::SyncAnvilThemeMode() in common/widgets/ui_common.cpp, which is
 * the exported hook the shell uses to flip the kicommon copy, and EDA_BASE_FRAME's ctor, which
 * covers the per-kiface copies.
 *
 * Brand tokens (see NEMI LMM Brand Book v2.0): ACCENT = Signal Emerald #10A37E,
 * CAP_ACTIVE = Deep Emerald #0A4938, TEXT = Bone/Soft-Oat #ECE7DD.
 *
 * DARK SURFACES (2026-08 two-variant pass): the dark theme uses EXACTLY TWO neutral dark
 * surface values — CANVAS DARK #070707 (Black Ground: all bars/strips/wells, matching the
 * drawing canvas) and PANEL GREY #1E1E1E (panel bodies, dialogs, popups, active tab).  No
 * green-cast darks; emerald appears only as the ACCENT / hover / caption colours.
 */
namespace ANVIL
{
enum class MODE
{
    DARK,   ///< NEMI Emerald Dark — the shipping default.
    LIGHT   ///< NEMI Emerald Light — Soft-Oat chrome + panels, Deep Emerald menu/tool-bar.
};


// clang-format off
/**
 * The palette table: one row per token, `name, dark R,G,B, light R,G,B`.
 *
 * A row whose two triples are identical is a colour that is the same in both themes (the brand
 * accent, the login screen, the Windows close-button red...).  Retune a colour here — never at a
 * call site — and both themes stay consistent.
 */
#define ANVIL_COLOUR_TABLE( _ )                                                                    \
    /* ---- Core brand tokens ------------------------------------------------------------- */    \
    /*  name              dark  R    G    B        light  R    G    B                       */    \
    _( ACCENT,                  16, 163, 126,             16, 163, 126 )  /* Signal Emerald  */    \
    _( CAP_ACTIVE,              10,  73,  56,             10,  73,  56 )  /* Deep Emerald    */    \
    _( PANEL,                   30,  30,  30,            234, 231, 219 )  /* dialog faces    */    \
    _( CONTENT,                  7,   7,   7,            255, 255, 255 )  /* content areas   */    \
    _( BONE,                   236, 231, 221,             20,  20,  15 )  /* primary text    */    \
                                                                                                   \
    /* ---- Chrome icon tiers: icons drawn ON the Deep Emerald menu / tool-bar ------------- */    \
    /* Emerald in both themes, so these stay light-on-emerald in the light theme too.        */    \
    _( ICON_IDLE,              236, 231, 221,            232, 239, 234 )  /* bar icon rest   */    \
    _( ICON_HOVER,              16, 163, 126,             79, 224, 180 )  /* bar icon hover  */    \
    _( ICON_DIM,               150, 160, 150,            122, 144, 137 )  /* bar icon dim    */    \
                                                                                                   \
    /* ---- Ink icon tiers: icons/glyphs drawn on a LIGHT surface ------------------------- */    \
    /* The title strip, the Project Files tree and the side panels.  Bone in the dark theme  */    \
    /* (identical to ICON_*), near-black ink in the light theme.                             */    \
    _( INK_ICON_IDLE,          236, 231, 221,             60,  60,  54 )  /* panel icon rest */    \
    _( INK_ICON_HOVER,          16, 163, 126,             16, 163, 126 )  /* panel icon hover*/    \
    _( INK_ICON_DIM,           150, 160, 150,            176, 175, 166 )  /* panel icon dim  */    \
                                                                                                   \
    /* ---- Filled-glyph tier: icons whose art is a SOLID shape, not thin line-work -------- */    \
    /* The Project Files folder is a filled silhouette; flattening it to the ink tier above  */    \
    /* paints a solid near-black blob in the light theme, so filled glyphs get a Soft-Oat    */    \
    /* cream instead.  Bone in the dark theme (identical to INK_ICON_IDLE there).            */    \
    _( FILL_ICON_IDLE,         236, 231, 221,            226, 221, 205 )  /* filled glyph    */    \
                                                                                                   \
    /* ---- Main-window chrome surface tiers ---------------------------------------------- */    \
    /* Dark theme (2026-08 two-variant pass): EXACTLY TWO neutral dark surfaces, no green     */    \
    /* cast.  PANEL GREY #1E1E1E — the title/menu/status strips and ALL tool-bar rows (the    */    \
    /* dark analogue of the light theme's Deep Emerald band), plus the panel BODIES (Project  */    \
    /* Files tree, Appearance, Properties, AI Assistant), dialog faces, popups and the        */    \
    /* active tab.  CANVAS DARK #070707 — sashes, heading strips, the tab strip and every     */    \
    /* content well — matches the drawing canvas' Black Ground, so the content zone reads as  */    \
    /* ONE surface the grey chrome lifts off of.  Every dark surface below must be one of     */    \
    /* those two values.                                                                     */    \
    /* Light theme: the title + status strips are oat, the menu row and the main tool-bar are */    \
    /* Deep Emerald (that inversion is the whole light mockup), and the panel BODIES are      */    \
    /* white -- only the HEADING strips above them (pane captions + the editor tab strip) are */    \
    /* oat.  That is the CHROME_PANEL / CHROME_HEADER split: one is the body a list or tree   */    \
    /* sits on, the other is the caption row that titles it.                                 */    \
    _( CHROME_BG,               30,  30,  30,            247, 245, 236 )  /* title/status    */    \
    _( CHROME_MENU,             30,  30,  30,             11,  74,  55 )  /* menu row band   */    \
    _( CHROME_BAR,              30,  30,  30,             11,  74,  55 )  /* main tool-bar   */    \
    _( CHROME_BAR2,             30,  30,  30,            242, 240, 230 )  /* aux/value row   */    \
    _( CHROME_PANEL,            30,  30,  30,            255, 255, 255 )  /* panel bodies    */    \
    _( CHROME_HEADER,           30,  30,  30,            234, 231, 219 )  /* heading strips  */    \
    _( CHROME_LINE,             38,  38,  38,            216, 212, 196 )  /* hairlines       */    \
    _( CHROME_SASH,              7,   7,   7,            226, 222, 208 )  /* pane seams      */    \
    _( CAPTION_TEXT,           122, 122, 122,            126, 124, 112 )  /* pane captions   */    \
    /* Editor tab strip.  Its own token rather than CHROME_HEADER: the strip is a CHROME band  */    \
    /* (it must match the title / tool-bar band), while CHROME_HEADER also drives the pane      */    \
    /* CAPTIONS and the checked-toolbar marker, which must stay a step apart from the bars.     */    \
    /* Both themes follow one rule -- strip = the chrome tone, active tab = the CONTENT tone,   */    \
    /* so the selected tab reads as a continuation of the canvas below it.                     */    \
    _( TAB_STRIP,               30,  30,  30,            234, 231, 219 )  /* tab strip band  */    \
    _( TAB_ACTIVE,               7,   7,   7,            255, 255, 255 )  /* active tab fill */    \
                                                                                                   \
    /* ---- On-the-emerald-bar text + hover ----------------------------------------------- */    \
    _( ON_BAR,                 236, 231, 221,            236, 231, 221 )  /* text on the bar */    \
    _( BAR_HOVER,               21,  48,  40,             20,  84,  65 )  /* hover on the bar*/    \
                                                                                                   \
    /* ---- Native-control edge ----------------------------------------------------------- */    \
    /* Overdrawn flat by KIPLATFORM::UI::FlattenNativeBorder so a stock light-grey / white    */    \
    /* dark-mode border never shows through.                                                 */    \
    _( CONTROL_EDGE,            46,  46,  46,            207, 202, 186 )                            \
                                                                                                   \
    /* ---- Chrome surfaces / edges ------------------------------------------------------- */    \
    /* Dark popups follow the two-variant system: PANEL GREY #1E1E1E surfaces with a neutral  */    \
    /* border — emerald lives in the ACCENT hover row, not the surface itself.               */    \
    _( POPUP_BG,                30,  30,  30,            247, 245, 236 )  /* popup / menu bg */    \
    _( BORDER,                  48,  48,  48,            208, 203, 186 )  /* control edge    */    \
    _( HOVER,                   21,  48,  40,            221, 232, 224 )  /* subtle hover    */    \
    _( CAP_INACTIVE,             7,   7,   7,            234, 231, 219 )  /* inactive caption*/    \
    _( SASH,                     7,   7,   7,            226, 222, 208 )  /* dock sash       */    \
                                                                                                   \
    /* ---- Login / sign-in screen (identical in both themes) ----------------------------- */    \
    _( LOGIN_GRAD_TOP,           7,   7,   7,              7,   7,   7 )                            \
    _( LOGIN_GRAD_BOTTOM,       10,  73,  56,             10,  73,  56 )                            \
    _( LOGIN_TRACE,             16,  82,  64,             16,  82,  64 )                            \
    _( LOGIN_TRACE_HI,          16, 163, 126,             16, 163, 126 )                            \
    _( LOGIN_TILE_BG,           41,  41,  38,             41,  41,  38 )                            \
    _( LOGIN_TILE_ACTIVE,       10,  73,  56,             10,  73,  56 )                            \
    _( LOGIN_TILE_BORDER,       63,  63,  58,             63,  63,  58 )                            \
    _( LOGIN_BOARD_EDGE,        16, 163, 126,             16, 163, 126 )                            \
    _( LOGIN_COMP_BODY,         10,  10,   9,             10,  10,   9 )                            \
    _( LOGIN_COMP_PIN,         154, 151, 140,            154, 151, 140 )                            \
    _( LOGIN_PAGE_TOP,         254, 254, 254,            254, 254, 254 )                            \
    _( LOGIN_PAGE_BG,          246, 244, 232,            246, 244, 232 )                            \
    _( LOGIN_SURFACE,          236, 231, 221,            236, 231, 221 )                            \
    _( LOGIN_GRID,             220, 214, 200,            220, 214, 200 )                            \
    _( LOGIN_CARD_BORDER,      214, 208, 194,            214, 208, 194 )                            \
    _( LOGIN_DIVIDER,          223, 217, 204,            223, 217, 204 )                            \
    _( LOGIN_BADGE_BG,         221, 235, 225,            221, 235, 225 )                            \
    _( LOGIN_INK,                7,   7,   7,              7,   7,   7 )                            \
    _( LOGIN_MUTED,            138, 138, 133,            138, 138, 133 )                            \
    _( LOGIN_PLACEHOLDER,      166, 164, 155,            166, 164, 155 )                            \
    _( LOGIN_FIELD_BG,         247, 245, 236,            247, 245, 236 )                            \
    _( LOGIN_FIELD_BORDER,     210, 204, 189,            210, 204, 189 )                            \
    _( LOGIN_FIELD_FOCUS,       16, 163, 126,             16, 163, 126 )                            \
    _( LOGIN_ERROR,            196,  61,  61,            196,  61,  61 )                            \
    _( LOGIN_CARD_GLOW,         16, 163, 126,             16, 163, 126 )                            \
    _( LOGIN_BAR_INK,          138, 138, 133,            138, 138, 133 )                            \
                                                                                                   \
    /* ---- Text tiers / misc ------------------------------------------------------------- */    \
    _( DIM,                    150, 160, 150,            111, 111, 104 )  /* disabled text   */    \
    _( DIM_MENU,               140, 150, 140,            138, 138, 130 )  /* dim popup text  */    \
    _( ACCEL,                  150, 175, 165,            154, 154, 146 )  /* shortcut text   */    \
    _( CLOSE_HOVER,            232,  17,  35,            232,  17,  35 )  /* Windows red     */    \
    _( ON_ACCENT,              255, 255, 255,            255, 255, 255 )  /* text on emerald */
// clang-format on


#define ANVIL_DEFINE_COLOUR( name, dr, dg, db, lr, lg, lb ) inline wxColour name( dr, dg, db );
ANVIL_COLOUR_TABLE( ANVIL_DEFINE_COLOUR )
#undef ANVIL_DEFINE_COLOUR


/// The mode this module's copy of the palette currently holds.  See the DLL note above.
inline MODE g_mode = MODE::DARK;


/**
 * Repaint the whole palette in @a aMode.
 *
 * Cheap (a few dozen wxColour assignments) and idempotent, so it is safe to call from every
 * module's start-up path.  It does NOT repaint any window: the caller re-applies the frame
 * themes and Refresh()es afterwards.
 */
inline void SetMode( MODE aMode )
{
    g_mode = aMode;

#define ANVIL_ASSIGN_COLOUR( name, dr, dg, db, lr, lg, lb )                                        \
    name = ( aMode == MODE::LIGHT ) ? wxColour( lr, lg, lb ) : wxColour( dr, dg, db );
    ANVIL_COLOUR_TABLE( ANVIL_ASSIGN_COLOUR )
#undef ANVIL_ASSIGN_COLOUR
}


inline MODE GetMode()
{
    return g_mode;
}


/// True while the NEMI Emerald **Light** theme is active (this module's copy).
inline bool IsLight()
{
    return g_mode == MODE::LIGHT;
}

} // namespace ANVIL

#endif // KIPLATFORM_ANVIL_THEME_H_
