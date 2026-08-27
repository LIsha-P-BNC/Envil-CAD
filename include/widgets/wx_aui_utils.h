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


#ifndef KICAD_WX_AUI_UTILS_H
#define KICAD_WX_AUI_UTILS_H

#include <wx/aui/framemanager.h>
#include <wx/log.h>


/**
 * Trace mask for AUI pane bookkeeping; enable with WXTRACE=KICAD_AUI.
 */
#define traceAuiPanes wxT( "KICAD_AUI" )


/**
 * A wxAuiManager that ignores null windows instead of asserting on them.
 *
 * Panes here are routinely conditional: toolbars that only exist in one layout preset (the
 * left/right drawing rails are null under the modern Altium-style layout), panels that may fail
 * to construct (the AI chat webview), optional side panes.  Handing the resulting nullptr to
 * wxAuiManager::AddPane() trips a wxWidgets assert -- and because vcpkg builds wxWidgets with
 * wxDEBUG_LEVEL=1, that assert pops a modal "Debug Alert" dialog even in a release build.  The
 * pane is not added either way, so the assert bought nothing but a popup to click through.
 *
 * Doing the check once here makes every call site -- present and future -- safe without anyone
 * having to remember an `if( m_tbFoo )` guard.  The skipped pane is still diagnosable via the
 * traceAuiPanes log trace.
 */
class EDA_AUI_MANAGER : public wxAuiManager
{
public:
    EDA_AUI_MANAGER( wxWindow* aManagedWnd = nullptr, unsigned int aFlags = wxAUI_MGR_DEFAULT ) :
            wxAuiManager( aManagedWnd, aFlags )
    { }

    bool AddPane( wxWindow* aWindow, const wxAuiPaneInfo& aPaneInfo )
    {
        if( !aWindow )
            return skipNullWindow( wxT( "AddPane" ), aPaneInfo.name );

        return wxAuiManager::AddPane( aWindow, aPaneInfo );
    }

    bool AddPane( wxWindow* aWindow, const wxAuiPaneInfo& aPaneInfo, const wxPoint& aDropPos )
    {
        if( !aWindow )
            return skipNullWindow( wxT( "AddPane" ), aPaneInfo.name );

        return wxAuiManager::AddPane( aWindow, aPaneInfo, aDropPos );
    }

    bool AddPane( wxWindow* aWindow, int aDirection = wxLEFT,
                  const wxString& aCaption = wxEmptyString )
    {
        if( !aWindow )
            return skipNullWindow( wxT( "AddPane" ), aCaption );

        return wxAuiManager::AddPane( aWindow, aDirection, aCaption );
    }

    bool InsertPane( wxWindow* aWindow, const wxAuiPaneInfo& aInsertLocation,
                     int aInsertLevel = wxAUI_INSERT_PANE )
    {
        if( !aWindow )
            return skipNullWindow( wxT( "InsertPane" ), aInsertLocation.name );

        return wxAuiManager::InsertPane( aWindow, aInsertLocation, aInsertLevel );
    }

    bool DetachPane( wxWindow* aWindow )
    {
        if( !aWindow )
            return skipNullWindow( wxT( "DetachPane" ), wxEmptyString );

        return wxAuiManager::DetachPane( aWindow );
    }

private:
    static bool skipNullWindow( const wxString& aCall, const wxString& aName )
    {
        wxLogTrace( traceAuiPanes, wxT( "%s: ignoring null window (pane \"%s\")" ), aCall,
                    aName.IsEmpty() ? wxString( wxT( "<unnamed>" ) ) : aName );
        return false;
    }
};


/**
 * Sets the size of an AUI pane, working around http://trac.wxwidgets.org/ticket/13180
 * @param aManager is an AUI manager
 * @param aPane is a wxAuiPaneInfo containing pane info managed by aManager
 * @param aWidth is the width to set (-1 for automatic)
 * @param aHeight is the height to set (-1 for automatic)
 */
void SetAuiPaneSize( wxAuiManager& aManager, wxAuiPaneInfo& aPane, int aWidth, int aHeight );

#endif // KICAD_WX_AUI_UTILS_H
