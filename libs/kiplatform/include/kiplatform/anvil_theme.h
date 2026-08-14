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

// Text tiers / misc
inline const wxColour DIM         ( 150, 160, 150 );   // dim / disabled / secondary text (GRAYTEXT)
inline const wxColour DIM_MENU    ( 140, 150, 140 );   // dim text in popups / inactive glyphs
inline const wxColour ACCEL       ( 150, 175, 165 );   // accelerator (shortcut) text in popups
inline const wxColour CLOSE_HOVER ( 232,  17,  35 );   // Windows-standard red for the close-button hover
inline const wxColour ON_ACCENT   ( 255, 255, 255 );   // text / glyph drawn on top of the emerald accent
} // namespace ANVIL

#endif // KIPLATFORM_ANVIL_THEME_H_
