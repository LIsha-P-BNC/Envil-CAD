/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2020 Jon Evans <jon@craftyjon.com>
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

// NOTE: This file should only be included in color_settings.cpp

#ifndef _BUILTIN_COLOR_THEMES_H
#define _BUILTIN_COLOR_THEMES_H

#define CSS_COLOR( r, g, b, a ) COLOR4D().FromCSSRGBA( r, g, b, a )

static const std::map<int, COLOR4D> s_defaultTheme =
        {
            // ---- NEMI Emerald Dark (schematic) : Black Ground #070707 + emerald accents ----
            { LAYER_SCHEMATIC_ANCHOR,     CSS_COLOR( 244, 114, 182, 1 ) },
            { LAYER_SCHEMATIC_AUX_ITEMS,  CSS_COLOR( 236, 231, 221, 1 ) },
            // Black Ground, per the header comment above.  The light theme's white sheet comes
            // from s_anvilLightOverrides below, never from this row.
            { LAYER_SCHEMATIC_BACKGROUND, CSS_COLOR( 7,   7,   7,   1 ) },
            { LAYER_HOVERED,              CSS_COLOR( 248, 245, 238, 1 ) },
            { LAYER_BRIGHTENED,           CSS_COLOR( 255, 45,  201, 1 ) },
            { LAYER_BUS,                  CSS_COLOR( 45,  212, 191, 1 ) },
            { LAYER_BUS_JUNCTION,         CSS_COLOR( 45,  212, 191, 1 ) },
            { LAYER_DEVICE_BACKGROUND,    CSS_COLOR( 22,  22,  21,  1 ) },
            { LAYER_DEVICE,               CSS_COLOR( 236, 231, 221, 1 ) },
            { LAYER_SCHEMATIC_CURSOR,     CSS_COLOR( 236, 231, 221, 1 ) },
            { LAYER_DNP_MARKER,           CSS_COLOR( 220, 9,   13,  0.85 ) },
            { LAYER_EXCLUDED_FROM_SIM,    CSS_COLOR( 138, 138, 133, 0.95 ) },
            { LAYER_ERC_ERR,              CSS_COLOR( 255, 59,  48,  1 ) },
            { LAYER_ERC_WARN,             CSS_COLOR( 255, 176, 32,  1 ) },
            { LAYER_ERC_EXCLUSION,        CSS_COLOR( 138, 138, 133, 0.8 ) },
            { LAYER_FIELDS,               CSS_COLOR( 138, 138, 133, 1 ) },
            // Two-variant dark pass: the grid furniture is NEUTRAL dark grey, never green —
            // emerald is reserved for content (wires, pins, the drawing-sheet frame).
            { LAYER_SCHEMATIC_GRID,       CSS_COLOR( 28,  28,  28,  1 ) },
            { LAYER_SCHEMATIC_GRID_AXES,  CSS_COLOR( 42,  42,  42,  1 ) },
            { LAYER_HIDDEN,               CSS_COLOR( 100, 110, 105, 1 ) },
            { LAYER_JUNCTION,             CSS_COLOR( 52,  211, 153, 1 ) },
            { LAYER_GLOBLABEL,            CSS_COLOR( 251, 146, 60,  1 ) },
            { LAYER_HIERLABEL,            CSS_COLOR( 250, 204, 21,  1 ) },
            { LAYER_LOCLABEL,             CSS_COLOR( 236, 231, 221, 1 ) },
            { LAYER_NETCLASS_REFS,        CSS_COLOR( 138, 138, 133, 1 ) },
            { LAYER_DRAG_NET_COLLISION,   CSS_COLOR( 255, 59,  48,  0.9 ) },
            { LAYER_RULE_AREAS,           CSS_COLOR( 255, 0,   0,   1 ) },
            { LAYER_NOCONNECT,            CSS_COLOR( 96,  165, 250, 1 ) },
            { LAYER_NOTES,                CSS_COLOR( 214, 209, 199, 1 ) },
            { LAYER_PRIVATE_NOTES,        CSS_COLOR( 150, 210, 195, 1 ) },
            { LAYER_NOTES_BACKGROUND,     CSS_COLOR( 0,   0,   0,   0 ) },
            { LAYER_PIN,                  CSS_COLOR( 94,  234, 212, 1 ) },
            { LAYER_PINNAM,               CSS_COLOR( 203, 213, 225, 1 ) },
            { LAYER_PINNUM,               CSS_COLOR( 236, 231, 221, 1 ) },
            { LAYER_REFERENCEPART,        CSS_COLOR( 125, 211, 252, 1 ) },
#ifdef __WXMAC__
            // Try to mimic the system highlight color on Mac
            { LAYER_SELECTION_SHADOWS,      COLOR4D( 0.93, 0.91, 0.85, 0.6 ) },
#else
            { LAYER_SELECTION_SHADOWS,      COLOR4D( 0.93, 0.91, 0.85, 0.8 ) },
#endif
            { LAYER_SHEET,                  CSS_COLOR( 94,  234, 212, 1 ) },
            { LAYER_SHEET_BACKGROUND,       CSS_COLOR( 255, 255, 255, 0 ) },
            { LAYER_SHEETFILENAME,          CSS_COLOR( 45,  212, 191, 1 ) },
            { LAYER_SHEETFIELDS,            CSS_COLOR( 138, 138, 133, 1 ) },
            { LAYER_SHEETLABEL,             CSS_COLOR( 250, 204, 21,  1 ) },
            { LAYER_SHEETNAME,              CSS_COLOR( 94,  234, 212, 1 ) },
            { LAYER_VALUEPART,              CSS_COLOR( 170, 180, 175, 1 ) },
            { LAYER_WIRE,                   CSS_COLOR( 16,  185, 129, 1 ) },
            { LAYER_SCHEMATIC_DRAWINGSHEET, CSS_COLOR( 45,  212, 191, 1 ) },
            { LAYER_SCHEMATIC_PAGE_LIMITS,  CSS_COLOR( 32,  32,  32,  1 ) },
            { LAYER_OP_VOLTAGES,            CSS_COLOR( 251, 146, 60,  1 ) },
            { LAYER_OP_CURRENTS,            CSS_COLOR( 248, 113, 113, 1 ) },

            { LAYER_GERBVIEW_AXES,          CSS_COLOR( 0,   0,   132, 1 ) },
            { LAYER_GERBVIEW_BACKGROUND,    CSS_COLOR( 0,   0,   0,   1 ) },
            { LAYER_DCODES,                 CSS_COLOR( 255, 255, 255, 1 ) },
            { LAYER_GERBVIEW_GRID,          CSS_COLOR( 132, 132, 132, 1 ) },
            { LAYER_NEGATIVE_OBJECTS,       CSS_COLOR( 132, 132, 132, 1 ) },
            { LAYER_GERBVIEW_DRAWINGSHEET,  CSS_COLOR(   0,   0, 132, 1 ) },
            { LAYER_GERBVIEW_PAGE_LIMITS,   CSS_COLOR( 132, 132, 132, 1 ) },

            { GERBVIEW_LAYER_ID_START,      CSS_COLOR( 200, 52,  52,  1 ) },
            { GERBVIEW_LAYER_ID_START + 1,  CSS_COLOR( 127, 200, 127, 1 ) },
            { GERBVIEW_LAYER_ID_START + 2,  CSS_COLOR( 206, 125, 44,  1 ) },
            { GERBVIEW_LAYER_ID_START + 3,  CSS_COLOR( 79,  203, 203, 1 ) },
            { GERBVIEW_LAYER_ID_START + 4,  CSS_COLOR( 219, 98, 139,  1 ) },
            { GERBVIEW_LAYER_ID_START + 5,  CSS_COLOR( 167, 165, 198, 1 ) },
            { GERBVIEW_LAYER_ID_START + 6,  CSS_COLOR( 40,  204, 217, 1 ) },
            { GERBVIEW_LAYER_ID_START + 7,  CSS_COLOR( 232, 178, 167, 1 ) },
            { GERBVIEW_LAYER_ID_START + 8,  CSS_COLOR( 242, 237, 161, 1 ) },
            { GERBVIEW_LAYER_ID_START + 9,  CSS_COLOR( 141, 203, 129, 1 ) },
            { GERBVIEW_LAYER_ID_START + 10, CSS_COLOR( 237, 124, 51,  1 ) },
            { GERBVIEW_LAYER_ID_START + 11, CSS_COLOR( 91,  195, 235, 1 ) },
            { GERBVIEW_LAYER_ID_START + 12, CSS_COLOR( 247, 111, 142, 1 ) },
            { GERBVIEW_LAYER_ID_START + 13, CSS_COLOR( 77,  127, 196, 1 ) },
            { GERBVIEW_LAYER_ID_START + 14, CSS_COLOR( 200, 52,  52,  1 ) },
            { GERBVIEW_LAYER_ID_START + 15, CSS_COLOR( 127, 200, 127, 1 ) },
            { GERBVIEW_LAYER_ID_START + 16, CSS_COLOR( 206, 125, 44,  1 ) },
            { GERBVIEW_LAYER_ID_START + 17, CSS_COLOR( 79,  203, 203, 1 ) },
            { GERBVIEW_LAYER_ID_START + 18, CSS_COLOR( 219, 98, 139,  1 ) },
            { GERBVIEW_LAYER_ID_START + 19, CSS_COLOR( 167, 165, 198, 1 ) },
            { GERBVIEW_LAYER_ID_START + 20, CSS_COLOR( 40,  204, 217, 1 ) },
            { GERBVIEW_LAYER_ID_START + 21, CSS_COLOR( 232, 178, 167, 1 ) },
            { GERBVIEW_LAYER_ID_START + 22, CSS_COLOR( 242, 237, 161, 1 ) },
            { GERBVIEW_LAYER_ID_START + 23, CSS_COLOR( 141, 203, 129, 1 ) },
            { GERBVIEW_LAYER_ID_START + 24, CSS_COLOR( 237, 124, 51,  1 ) },
            { GERBVIEW_LAYER_ID_START + 25, CSS_COLOR( 91,  195, 235, 1 ) },
            { GERBVIEW_LAYER_ID_START + 26, CSS_COLOR( 247, 111, 142, 1 ) },
            { GERBVIEW_LAYER_ID_START + 27, CSS_COLOR( 77,  127, 196, 1 ) },
            { GERBVIEW_LAYER_ID_START + 28, CSS_COLOR( 200, 52,  52,  1 ) },
            { GERBVIEW_LAYER_ID_START + 29, CSS_COLOR( 127, 200, 127, 1 ) },
            { GERBVIEW_LAYER_ID_START + 30, CSS_COLOR( 206, 125, 44,  1 ) },
            { GERBVIEW_LAYER_ID_START + 31, CSS_COLOR( 79,  203, 203, 1 ) },
            { GERBVIEW_LAYER_ID_START + 32, CSS_COLOR( 219, 98, 139,  1 ) },
            { GERBVIEW_LAYER_ID_START + 33, CSS_COLOR( 167, 165, 198, 1 ) },
            { GERBVIEW_LAYER_ID_START + 34, CSS_COLOR( 40,  204, 217, 1 ) },
            { GERBVIEW_LAYER_ID_START + 35, CSS_COLOR( 232, 178, 167, 1 ) },
            { GERBVIEW_LAYER_ID_START + 36, CSS_COLOR( 242, 237, 161, 1 ) },
            { GERBVIEW_LAYER_ID_START + 37, CSS_COLOR( 141, 203, 129, 1 ) },
            { GERBVIEW_LAYER_ID_START + 38, CSS_COLOR( 237, 124, 51,  1 ) },
            { GERBVIEW_LAYER_ID_START + 39, CSS_COLOR( 91,  195, 235, 1 ) },
            { GERBVIEW_LAYER_ID_START + 40, CSS_COLOR( 247, 111, 142, 1 ) },
            { GERBVIEW_LAYER_ID_START + 41, CSS_COLOR( 77,  127, 196, 1 ) },
            { GERBVIEW_LAYER_ID_START + 42, CSS_COLOR( 200, 52,  52,  1 ) },
            { GERBVIEW_LAYER_ID_START + 43, CSS_COLOR( 127, 200, 127, 1 ) },
            { GERBVIEW_LAYER_ID_START + 44, CSS_COLOR( 206, 125, 44,  1 ) },
            { GERBVIEW_LAYER_ID_START + 45, CSS_COLOR( 79,  203, 203, 1 ) },
            { GERBVIEW_LAYER_ID_START + 46, CSS_COLOR( 219, 98, 139,  1 ) },
            { GERBVIEW_LAYER_ID_START + 47, CSS_COLOR( 167, 165, 198, 1 ) },
            { GERBVIEW_LAYER_ID_START + 48, CSS_COLOR( 40,  204, 217, 1 ) },
            { GERBVIEW_LAYER_ID_START + 49, CSS_COLOR( 232, 178, 167, 1 ) },
            { GERBVIEW_LAYER_ID_START + 50, CSS_COLOR( 242, 237, 161, 1 ) },
            { GERBVIEW_LAYER_ID_START + 51, CSS_COLOR( 141, 203, 129, 1 ) },
            { GERBVIEW_LAYER_ID_START + 52, CSS_COLOR( 237, 124, 51,  1 ) },
            { GERBVIEW_LAYER_ID_START + 53, CSS_COLOR( 91,  195, 235, 1 ) },
            { GERBVIEW_LAYER_ID_START + 54, CSS_COLOR( 247, 111, 142, 1 ) },
            { GERBVIEW_LAYER_ID_START + 55, CSS_COLOR( 77,  127, 196, 1 ) },
            { GERBVIEW_LAYER_ID_START + 56, CSS_COLOR( 200, 52,  52,  1 ) },
            { GERBVIEW_LAYER_ID_START + 57, CSS_COLOR( 127, 200, 127, 1 ) },
            { GERBVIEW_LAYER_ID_START + 58, CSS_COLOR( 206, 125, 44,  1 ) },
            { GERBVIEW_LAYER_ID_START + 59, CSS_COLOR( 79,  203, 203, 1 ) },
            { GERBVIEW_LAYER_ID_START + 60, CSS_COLOR( 219, 98, 139,  1 ) },
            { GERBVIEW_LAYER_ID_START + 61, CSS_COLOR( 167, 165, 198, 1 ) },
            { GERBVIEW_LAYER_ID_START + 62, CSS_COLOR( 40,  204, 217, 1 ) },
            { GERBVIEW_LAYER_ID_START + 63, CSS_COLOR( 232, 178, 167, 1 ) },

            { LAYER_ANCHOR,                 CSS_COLOR( 255, 38,  226, 1 ) },
            { LAYER_LOCKED_ITEM_SHADOW,     CSS_COLOR( 255, 38,  226, 0.5 ) },
            { LAYER_CONFLICTS_SHADOW,       CSS_COLOR( 255,  0,   05, 0.5 ) },
            { LAYER_AUX_ITEMS,              CSS_COLOR( 255, 255, 255, 1 ) },
            // Black Ground board canvas; the light theme's white board sheet is an override in
            // s_anvilLightOverrides, so this row is dark-only.
            { LAYER_PCB_BACKGROUND,         CSS_COLOR( 7,   7,   7,   1 ) },
            { LAYER_CURSOR,                 CSS_COLOR( 255, 255, 255, 1 ) },
            { LAYER_DRC_ERROR,              CSS_COLOR( 215, 91,  107, 0.8 ) },
            { LAYER_DRC_WARNING,            CSS_COLOR( 255, 208, 66,  0.8 ) },
            { LAYER_DRC_EXCLUSION,          CSS_COLOR( 255, 255, 255, 0.8 ) },
            { LAYER_DRC_HIGHLIGHTED,          CSS_COLOR( 255, 0, 255, 1 ) },
            { LAYER_GRID,                   CSS_COLOR( 28,  28,  28,  1 ) },
            { LAYER_GRID_AXES,              CSS_COLOR( 194, 194, 194, 1 ) },
            { LAYER_PAD_PLATEDHOLES,        CSS_COLOR( 194, 194, 0, 1 ) },
            { LAYER_NON_PLATEDHOLES,        CSS_COLOR( 26,  196, 210, 1 ) },
            { LAYER_RATSNEST,               CSS_COLOR( 0,   248, 255, 0.35 ) },
            { LAYER_SELECT_OVERLAY,         CSS_COLOR( 4,   255, 67,  1 ) },
            { LAYER_VIA_HOLES,              CSS_COLOR( 227, 183, 46, 1 ) },
            { LAYER_VIA_HOLEWALLS,          CSS_COLOR( 236, 236, 236, 1 ) },
            { LAYER_DRAWINGSHEET,           CSS_COLOR( 45,  212, 191, 1 ) },
            { LAYER_PAGE_LIMITS,            CSS_COLOR( 32,  32,  32,  1 ) },
            { LAYER_BOARD_OUTLINE_AREA,     CSS_COLOR( 100, 100, 100, 0.35 ) },
            { NETNAMES_LAYER_ID_START,      CSS_COLOR( 255, 255, 255, 0.7 ) },
            { LAYER_PAD_NETNAMES,           CSS_COLOR( 255, 255, 255, 0.9 ) },
            { LAYER_VIA_NETNAMES,           CSS_COLOR( 50, 50, 50, 0.9 ) },
            { LAYER_POINTS,                 CSS_COLOR( 255, 38,  226, 1 ) },

            { F_Cu,                         CSS_COLOR( 200, 52,  52,  1 ) },
            { In1_Cu,                       CSS_COLOR( 127, 200, 127, 1 ) },
            { In2_Cu,                       CSS_COLOR( 206, 125, 44,  1 ) },
            { In3_Cu,                       CSS_COLOR( 79,  203, 203, 1 ) },
            { In4_Cu,                       CSS_COLOR( 219, 98, 139,  1 ) },
            { In5_Cu,                       CSS_COLOR( 167, 165, 198, 1 ) },
            { In6_Cu,                       CSS_COLOR( 40,  204, 217, 1 ) },
            { In7_Cu,                       CSS_COLOR( 232, 178, 167, 1 ) },
            { In8_Cu,                       CSS_COLOR( 242, 237, 161, 1 ) },
            { In9_Cu,                       CSS_COLOR( 141, 203, 129, 1 ) },
            { In10_Cu,                      CSS_COLOR( 237, 124, 51,  1 ) },
            { In11_Cu,                      CSS_COLOR( 91,  195, 235, 1 ) },
            { In12_Cu,                      CSS_COLOR( 247, 111, 142, 1 ) },
            { In13_Cu,                      CSS_COLOR( 167, 165, 198, 1 ) },
            { In14_Cu,                      CSS_COLOR( 40,  204, 217, 1 ) },
            { In15_Cu,                      CSS_COLOR( 232, 178, 167, 1 ) },
            { In16_Cu,                      CSS_COLOR( 242, 237, 161, 1 ) },
            { In17_Cu,                      CSS_COLOR( 237, 124, 51,  1 ) },
            { In18_Cu,                      CSS_COLOR( 91,  195, 235, 1 ) },
            { In19_Cu,                      CSS_COLOR( 247, 111, 142, 1 ) },
            { In20_Cu,                      CSS_COLOR( 167, 165, 198, 1 ) },
            { In21_Cu,                      CSS_COLOR( 40,  204, 217, 1 ) },
            { In22_Cu,                      CSS_COLOR( 232, 178, 167, 1 ) },
            { In23_Cu,                      CSS_COLOR( 242, 237, 161, 1 ) },
            { In24_Cu,                      CSS_COLOR( 237, 124, 51,  1 ) },
            { In25_Cu,                      CSS_COLOR( 91,  195, 235, 1 ) },
            { In26_Cu,                      CSS_COLOR( 247, 111, 142, 1 ) },
            { In27_Cu,                      CSS_COLOR( 167, 165, 198, 1 ) },
            { In28_Cu,                      CSS_COLOR( 40,  204, 217, 1 ) },
            { In29_Cu,                      CSS_COLOR( 232, 178, 167, 1 ) },
            { In30_Cu,                      CSS_COLOR( 242, 237, 161, 1 ) },
            { B_Cu,                         CSS_COLOR( 77,  127, 196, 1 ) },

            { B_Adhes,                      CSS_COLOR( 0,   0, 132,   1 ) },
            { F_Adhes,                      CSS_COLOR( 132, 0, 132,   1 ) },
            { B_Paste,                      CSS_COLOR( 0,   194, 194, 0.9 ) },
            { F_Paste,                      CSS_COLOR( 180, 160, 154, 0.9 ) },
            { B_SilkS,                      CSS_COLOR( 232, 178, 167, 1 ) },
            { F_SilkS,                      CSS_COLOR( 242, 237, 161, 1 ) },
            { B_Mask,                       CSS_COLOR( 2,   255, 238, 0.4 ) },
            { F_Mask,                       CSS_COLOR( 216, 100, 255, 0.4 ) },
            { Dwgs_User,                    CSS_COLOR( 194, 194, 194, 1 ) },
            { Cmts_User,                    CSS_COLOR( 89,  148, 220, 1 ) },
            { Eco1_User,                    CSS_COLOR( 180, 219, 210, 1 ) },
            { Eco2_User,                    CSS_COLOR( 216, 200, 82,  1 ) },
            { Edge_Cuts,                    CSS_COLOR( 208, 210, 205, 1 ) },
            { Margin,                       CSS_COLOR( 255, 38,  226, 1 ) },
            { B_CrtYd,                      CSS_COLOR( 38,  233,  255, 1 ) },
            { F_CrtYd,                      CSS_COLOR( 255, 38,  226, 1 ) },
            { B_Fab,                        CSS_COLOR( 88,  93,  132, 1 ) },
            { F_Fab,                        CSS_COLOR( 175, 175, 175, 1 ) },
            { User_1,                       CSS_COLOR( 194, 194, 194, 1 ) },
            { User_2,                       CSS_COLOR( 89,  148, 220, 1 ) },
            { User_3,                       CSS_COLOR( 180, 219, 210, 1 ) },
            { User_4,                       CSS_COLOR( 216, 200, 82,  1 ) },
            { User_5,                       CSS_COLOR( 194, 194, 194, 1 ) },
            { User_6,                       CSS_COLOR( 89,  148, 220, 1 ) },
            { User_7,                       CSS_COLOR( 180, 219, 210, 1 ) },
            { User_8,                       CSS_COLOR( 216, 200, 82,  1 ) },
            { User_9,                       CSS_COLOR( 232, 178, 167, 1 ) },
            { User_10,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_11,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_12,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_13,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_14,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_15,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_16,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_17,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_18,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_19,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_20,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_21,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_22,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_23,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_24,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_25,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_26,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_27,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_28,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_29,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_30,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_31,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_32,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_33,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_34,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_35,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_36,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_37,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_38,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_39,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_40,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_41,                      CSS_COLOR( 194, 194, 194, 1 ) },
            { User_42,                      CSS_COLOR( 89,  148, 220, 1 ) },
            { User_43,                      CSS_COLOR( 180, 219, 210, 1 ) },
            { User_44,                      CSS_COLOR( 216, 200, 82,  1 ) },
            { User_45,                      CSS_COLOR( 194, 194, 194, 1 ) },

            { LAYER_3D_BACKGROUND_BOTTOM,   COLOR4D( 0.03, 0.03, 0.03, 1.0 ) },
            { LAYER_3D_BACKGROUND_TOP,      COLOR4D( 0.06, 0.12, 0.11, 1.0 ) },
            { LAYER_3D_BOARD,               COLOR4D( 0.2, 0.17, 0.09, 0.9 ) },
            { LAYER_3D_COPPER_TOP,          COLOR4D( 0.7, 0.61, 0.0, 1.0 ) },
            { LAYER_3D_SILKSCREEN_BOTTOM,   COLOR4D( 0.9, 0.9, 0.9, 1.0 ) },
            { LAYER_3D_SILKSCREEN_TOP,      COLOR4D( 0.9, 0.9, 0.9, 1.0 ) },
            { LAYER_3D_SOLDERMASK_BOTTOM,   COLOR4D( 0.08, 0.2, 0.14, 0.83 ) },
            { LAYER_3D_SOLDERMASK_TOP,      COLOR4D( 0.08, 0.2, 0.14, 0.83 ) },
            { LAYER_3D_SOLDERPASTE,         COLOR4D( 0.5, 0.5, 0.5, 1.0 ) }
        };

/**
 * NEMI Emerald LIGHT canvas overrides.
 *
 * The light theme is NOT a second palette: it is the NEMI Emerald Dark theme (s_defaultTheme)
 * with only its SURFACE and near-white layers re-pointed, exactly as the light mockup shows —
 * the layer swatches in the Appearance panel (F.Cu red, B.Cu blue, silkscreen yellow ...) are
 * identical in both themes, only the sheet under them turns white.
 *
 * So the rule for this table is: a layer belongs here if it is a surface (background, grid,
 * page frame), or if its dark-theme colour is white / near-white / a light pastel that would
 * vanish on a white canvas.  Everything else is deliberately absent and inherits the dark
 * theme's value.  See COLOR_SETTINGS::CreateBuiltinColorSettings(), which composes the two.
 */
static const std::map<int, COLOR4D> s_anvilLightOverrides =
        {
            // ---- Schematic: white sheet, ink-dark symbols -------------------------------
            { LAYER_SCHEMATIC_BACKGROUND,   CSS_COLOR( 255, 255, 255, 1 ) },
            { LAYER_SCHEMATIC_GRID,         CSS_COLOR( 216, 226, 222, 1 ) },
            { LAYER_SCHEMATIC_GRID_AXES,    CSS_COLOR( 158, 194, 184, 1 ) },
            { LAYER_SCHEMATIC_CURSOR,       CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_SCHEMATIC_AUX_ITEMS,    CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_SCHEMATIC_DRAWINGSHEET, CSS_COLOR( 82,  118, 110, 1 ) },
            { LAYER_SCHEMATIC_PAGE_LIMITS,  CSS_COLOR( 199, 210, 206, 1 ) },
            { LAYER_DEVICE,                 CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_DEVICE_BACKGROUND,      CSS_COLOR( 255, 252, 242, 1 ) },
            { LAYER_LOCLABEL,               CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_PINNUM,                 CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_PINNAM,                 CSS_COLOR( 46,  84,  110, 1 ) },
            { LAYER_NOTES,                  CSS_COLOR( 60,  60,  55,  1 ) },
            { LAYER_PRIVATE_NOTES,          CSS_COLOR( 46,  128, 113, 1 ) },
            { LAYER_HIDDEN,                 CSS_COLOR( 176, 181, 176, 1 ) },
            { LAYER_WIRE,                   CSS_COLOR( 11,  138, 97,  1 ) },
            { LAYER_BUS,                    CSS_COLOR( 15,  140, 130, 1 ) },
            { LAYER_BUS_JUNCTION,           CSS_COLOR( 15,  140, 130, 1 ) },
            { LAYER_JUNCTION,               CSS_COLOR( 11,  138, 97,  1 ) },
            { LAYER_PIN,                    CSS_COLOR( 15,  140, 130, 1 ) },
            { LAYER_SHEET,                  CSS_COLOR( 15,  140, 130, 1 ) },
            { LAYER_SHEETNAME,              CSS_COLOR( 15,  140, 130, 1 ) },
            { LAYER_SHEETFILENAME,          CSS_COLOR( 15,  140, 130, 1 ) },
            { LAYER_HIERLABEL,              CSS_COLOR( 166, 128, 8,   1 ) },
            { LAYER_SHEETLABEL,             CSS_COLOR( 166, 128, 8,   1 ) },
            { LAYER_GLOBLABEL,              CSS_COLOR( 199, 98,  20,  1 ) },
            { LAYER_NOCONNECT,              CSS_COLOR( 37,  106, 199, 1 ) },
            { LAYER_REFERENCEPART,          CSS_COLOR( 34,  106, 166, 1 ) },
            { LAYER_VALUEPART,              CSS_COLOR( 92,  102, 97,  1 ) },
            { LAYER_HOVERED,                CSS_COLOR( 16,  163, 126, 1 ) },
            { LAYER_SELECTION_SHADOWS,      COLOR4D( 0.06, 0.64, 0.49, 0.8 ) },

            // ---- Board: white sheet, ink-dark cursor / netnames / helpers ---------------
            { LAYER_PCB_BACKGROUND,         CSS_COLOR( 255, 255, 255, 1 ) },
            { LAYER_GRID,                   CSS_COLOR( 216, 226, 222, 1 ) },
            { LAYER_GRID_AXES,              CSS_COLOR( 150, 160, 158, 1 ) },
            { LAYER_CURSOR,                 CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_AUX_ITEMS,              CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_DRAWINGSHEET,           CSS_COLOR( 82,  118, 110, 1 ) },
            { LAYER_PAGE_LIMITS,            CSS_COLOR( 199, 210, 206, 1 ) },
            { LAYER_DRC_EXCLUSION,          CSS_COLOR( 70,  70,  64,  0.8 ) },
            { LAYER_VIA_HOLEWALLS,          CSS_COLOR( 70,  70,  64,  1 ) },
            { LAYER_RATSNEST,               CSS_COLOR( 0,   140, 160, 0.5 ) },
            { LAYER_BOARD_OUTLINE_AREA,     CSS_COLOR( 150, 150, 150, 0.25 ) },
            { NETNAMES_LAYER_ID_START,      CSS_COLOR( 30,  30,  25,  0.75 ) },
            { LAYER_PAD_NETNAMES,           CSS_COLOR( 30,  30,  25,  0.9 ) },
            { Edge_Cuts,                    CSS_COLOR( 88,  94,  91,  1 ) },
            { F_Fab,                        CSS_COLOR( 122, 122, 120, 1 ) },

            // ---- Gerbview ---------------------------------------------------------------
            { LAYER_GERBVIEW_BACKGROUND,    CSS_COLOR( 255, 255, 255, 1 ) },
            { LAYER_DCODES,                 CSS_COLOR( 20,  20,  15,  1 ) },
            { LAYER_GERBVIEW_GRID,          CSS_COLOR( 216, 226, 222, 1 ) },
            { LAYER_GERBVIEW_PAGE_LIMITS,   CSS_COLOR( 199, 210, 206, 1 ) },

            // ---- 3D viewer: light studio instead of the black room ----------------------
            { LAYER_3D_BACKGROUND_BOTTOM,   COLOR4D( 0.96, 0.96, 0.94, 1.0 ) },
            { LAYER_3D_BACKGROUND_TOP,      COLOR4D( 0.87, 0.93, 0.91, 1.0 ) }
        };


// These are looping colors used higher-order copper layers
static const std::vector<COLOR4D> s_copperColors =
{
    { CSS_COLOR( 237, 124, 51,  1 ) },
    { CSS_COLOR( 91,  195, 235, 1 ) },
    { CSS_COLOR( 247, 111, 142, 1 ) },
    { CSS_COLOR( 167, 165, 198, 1 ) },
    { CSS_COLOR( 40,  204, 217, 1 ) },
    { CSS_COLOR( 232, 178, 167, 1 ) },
    { CSS_COLOR( 242, 237, 161, 1 ) }
};

// These are looping colors used for user colors
static const std::vector<COLOR4D> s_userColors =
{
    { CSS_COLOR( 89,  148, 220, 1 ) },
    { CSS_COLOR( 180, 219, 210, 1 ) },
    { CSS_COLOR( 216, 200, 82,  1 ) },
    { CSS_COLOR( 194, 194, 194, 1 ) },
};


#endif
