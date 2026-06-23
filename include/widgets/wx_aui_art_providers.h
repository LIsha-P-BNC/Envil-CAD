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

#pragma once

#include <wx/aui/auibar.h>
#include <wx/aui/dockart.h>


class WX_AUI_TOOLBAR_ART : public wxAuiDefaultToolBarArt
{
public:
    WX_AUI_TOOLBAR_ART() :
            wxAuiDefaultToolBarArt()
    {
        saturateHighlightColor();
    }

    virtual ~WX_AUI_TOOLBAR_ART() = default;

#if wxCHECK_VERSION( 3, 3, 0 )
    wxSize GetToolSize( wxReadOnlyDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem ) override;
#else
    wxSize GetToolSize( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem ) override;
#endif

    /**
     * Unfortunately we need to re-implement this to actually be able to control the size
     */
    void DrawButton( wxDC& aDc, wxWindow* aWindow, const wxAuiToolBarItem& aItem,
                     const wxRect& aRect ) override;

    void DrawBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect ) override;

    void DrawPlainBackground( wxDC& aDc, wxWindow* aWindow, const wxRect& aRect ) override;

    void UpdateColoursFromSystem() override;

    int ShowDropDown( wxWindow* wnd, const wxAuiToolBarItemArray& items ) override;

    /**
     * Envil "Vibrant Purple" frame theme: paint this tool-bar's background solid @p aBg and use
     * @p aHighlight for the hover/pressed/checked feedback.  Enabled per art-provider instance
     * (so only the schematic editor's tool-bars are recoloured), and re-pinned in
     * UpdateColoursFromSystem() so a Windows theme/colour-change event can't reset it back to the
     * system grey.  Used only when the EnvilPurpleFrame advanced-config flag is set.
     */
    void EnableEnvilTheme( const wxColour& aBg, const wxColour& aHighlight )
    {
        m_envilTheme = true;
        m_envilBg = aBg;
        m_envilHighlight = aHighlight;
        m_baseColour = aBg;
        m_highlightColour = aHighlight;
    }

private:
    void saturateHighlightColor();

    bool     m_envilTheme = false;
    wxColour m_envilBg;
    wxColour m_envilHighlight;
};


class WX_AUI_DOCK_ART : public wxAuiDefaultDockArt
{
public:
    WX_AUI_DOCK_ART();
};


class WX_AUI_TAB_ART : public wxAuiGenericTabArt
{
public:
    WX_AUI_TAB_ART() :
            wxAuiGenericTabArt()
    {}

    wxAuiTabArt* Clone() override
    {
        return new WX_AUI_TAB_ART();
    }

    void DrawTab( wxDC& dc, wxWindow* wnd, const wxAuiNotebookPage& page, const wxRect& in_rect,
                  int close_button_state, wxRect* out_tab_rect, wxRect* out_button_rect, int* x_extent ) override;
};

