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
 * Colours are role-named.  Some roles deliberately share a value today (CAP_INACTIVE == SASH);
 * they are kept as separate roles so either can be retuned independently later.
 *
 * Brand tokens (see NEMI LMM Brand Book v2.0): ACCENT = Signal Emerald #10A37E,
 * CAP_ACTIVE = Deep Emerald #0A4938, PANEL = Warm Graphite #292926, CONTENT = Black Ground
 * #070707, TEXT = Bone/Soft-Oat #ECE7DD.
 */
namespace ANVIL
{
// Core brand tokens
inline const wxColour ACCENT      (  16, 163, 126 );   // #10A37E Signal Emerald — accent / hover / active
inline const wxColour CAP_ACTIVE  (  10,  73,  56 );   // #0A4938 Deep Emerald  — active pane caption
inline const wxColour PANEL       (  41,  41,  38 );   // #292926 Warm Graphite — panel / dialog faces
inline const wxColour CONTENT     (   7,   7,   7 );   // #070707 Black Ground  — content / data areas
inline const wxColour BONE        ( 236, 231, 221 );   // #ECE7DD Bone / Soft Oat — primary text
                                                       // (named BONE not TEXT: TEXT is a Windows macro)

// Chrome surfaces / edges
inline const wxColour POPUP_BG    (  24,  48,  42 );   // #18302A dropdown / context-menu popup bg
inline const wxColour BORDER      (  34,  90,  74 );   // #225A4A control / group-box / separator edge
inline const wxColour HOVER       (  30,  72,  60 );   // #1E483C subtle button hover fill (== 3DLIGHT)
inline const wxColour CAP_INACTIVE(  22,  22,  21 );   // #161615 inactive pane caption
inline const wxColour SASH        (  22,  22,  21 );   // #161615 dock sash (same value as CAP_INACTIVE)

// Login / sign-in screen (Anvil CAD sign-in: circuit-board brand panel + light card surface)
inline const wxColour LOGIN_GRAD_TOP    (   4,  20,  15 );   // #04140F brand panel gradient start (near-black emerald)
inline const wxColour LOGIN_GRAD_BOTTOM (  10,  58,  44 );   // #0A3A2C brand panel gradient end (deep emerald)
inline const wxColour LOGIN_TRACE       (  18,  92,  72 );   // #125C48 etched PCB trace artwork on the brand panel
inline const wxColour LOGIN_TRACE_HI    (  36, 190, 145 );   // #24BE91 lit trace / via highlight
inline const wxColour LOGIN_TILE_BG     (   9,  40,  31 );   // #09281F feature-card fill on the brand panel
inline const wxColour LOGIN_TILE_BORDER (  27,  84,  67 );   // #1B5443 feature-card edge on the brand panel
inline const wxColour LOGIN_BOARD_EDGE  (  46, 214, 160 );   // #2ED6A0 routed board outline on the brand panel
inline const wxColour LOGIN_COMP_BODY   (   8,  13,  11 );   // #080D0B moulded body of a component silhouette
inline const wxColour LOGIN_COMP_PIN    ( 127, 163, 150 );   // #7FA396 tinned lead / pin of a component silhouette

inline const wxColour LOGIN_PAGE_TOP    ( 251, 253, 252 );   // #FBFDFC top of the light page's gradient
inline const wxColour LOGIN_PAGE_BG     ( 240, 244, 242 );   // #F0F4F2 light page behind the sign-in card
inline const wxColour LOGIN_SURFACE     ( 255, 255, 255 );   // #FFFFFF the sign-in card itself
inline const wxColour LOGIN_GRID        ( 234, 237, 235 );   // #EAEDEB fine blueprint grid line on the page
inline const wxColour LOGIN_CARD_BORDER ( 219, 228, 223 );   // #DBE4DF card edge
inline const wxColour LOGIN_DIVIDER     ( 227, 233, 230 );   // #E3E9E6 hairline rules inside the card
inline const wxColour LOGIN_BADGE_BG    ( 223, 243, 234 );   // #DFF3EA pale emerald disc behind the shield mark
inline const wxColour LOGIN_INK         (  17,  24,  21 );   // #111815 headings / primary text on the card
inline const wxColour LOGIN_MUTED       ( 108, 120, 114 );   // #6C7872 secondary text on the card
inline const wxColour LOGIN_PLACEHOLDER ( 154, 166, 160 );   // #9AA6A0 input hint text
inline const wxColour LOGIN_FIELD_BG    ( 247, 250, 249 );   // #F7FAF9 input fill
inline const wxColour LOGIN_FIELD_BORDER( 216, 225, 220 );   // #D8E1DC input edge
inline const wxColour LOGIN_FIELD_FOCUS ( 143, 205, 184 );   // #8FCDB8 input edge while focused
inline const wxColour LOGIN_ERROR       ( 196,  61,  61 );   // #C43D3D inline validation / server error text
inline const wxColour LOGIN_CARD_GLOW   (  16, 163, 126 );   // #10A37E emerald halo around the sign-in card
inline const wxColour LOGIN_BAR_INK     ( 122, 136, 129 );   // #7A8881 icon + label of the page's status bar

// Text tiers / misc
inline const wxColour DIM         ( 150, 160, 150 );   // dim / disabled / secondary text (GRAYTEXT)
inline const wxColour DIM_MENU    ( 140, 150, 140 );   // dim text in popups / inactive glyphs
inline const wxColour ACCEL       ( 150, 175, 165 );   // accelerator (shortcut) text in popups
inline const wxColour CLOSE_HOVER ( 232,  17,  35 );   // Windows-standard red for the close-button hover
inline const wxColour ON_ACCENT   ( 255, 255, 255 );   // text / glyph drawn on top of the emerald accent
} // namespace ANVIL

#endif // KIPLATFORM_ANVIL_THEME_H_
