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

#ifndef ANVIL_FRAME_THEME_H_
#define ANVIL_FRAME_THEME_H_

#include <vector>

class EDA_BASE_FRAME;
class wxWindow;

namespace KIUI
{
/**
 * Apply the Anvil dark-chrome frame theme (dock-art colours, toolbar backgrounds + highlight,
 * and a recursive recolor of every child control) to @a aFrame.
 *
 * This is the single shared implementation behind the per-frame `applyAnvilFrameTheme()` /
 * `applyAnvilPurpleFrameTheme()` methods on PCB_EDIT_FRAME and SCH_EDIT_FRAME. Those two frames
 * each hand-rolled the same dock-art + toolbar-list + recursive-recolor boilerplate, which is
 * exactly why the footprint editor, symbol editor, gerbview, pl_editor and cvpcb ended up
 * unthemed (glaring stock-dark-mode borders/backgrounds) while sharing the same window.
 *
 * Call from the end of the frame's ctor, gated the same way existing callers already are
 * (typically `ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame`).
 *
 * @param aFrame is the frame to theme. Its dock art, the four standard toolbars + active bar,
 *               and the info bar (all exposed by EDA_BASE_FRAME) are picked up automatically.
 * @param aExtraExclude are additional windows (and their subtrees) to skip during the
 *                       recursive recolor -- typically the drawing canvas and any embedded
 *                       web/AI panel, which paint their own surfaces.
 */
void ApplyAnvilFrameTheme( EDA_BASE_FRAME* aFrame, const std::vector<wxWindow*>& aExtraExclude = {} );

} // namespace KIUI

#endif // ANVIL_FRAME_THEME_H_
