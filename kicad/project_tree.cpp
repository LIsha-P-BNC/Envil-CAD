/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2004-2012 Jean-Pierre Charras
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


#include <advanced_config.h>
#include <bitmaps.h>
#include <git/kicad_git_common.h>
#include <kiplatform/anvil_theme.h>
#include <kiplatform/ui.h>
#include <widgets/ui_common.h>
#include <wx/settings.h>
#include <wx/dcmemory.h>

#include "project_tree_item.h"
#include "project_tree_pane.h"
#include "project_tree.h"
#include "kicad_id.h"


IMPLEMENT_ABSTRACT_CLASS( PROJECT_TREE, wxTreeCtrl )


#ifdef __WXMSW__
#define PLATFORM_STYLE wxTR_LINES_AT_ROOT
#else
#define PLATFORM_STYLE wxTR_NO_LINES
#endif

PROJECT_TREE::PROJECT_TREE( PROJECT_TREE_PANE* parent ) :
        wxTreeCtrl( parent, ID_PROJECT_TREE, wxDefaultPosition, wxDefaultSize,
                    PLATFORM_STYLE | wxTR_HAS_BUTTONS | wxTR_MULTIPLE, wxDefaultValidator,
                    wxT( "EDATreeCtrl" ) ),
        m_statusImageList( nullptr )
{
    m_projectTreePane = parent;
    m_gitCommon = std::make_unique<KIGIT_COMMON>( nullptr );

    // Make sure the GUI font scales properly on GTK
    SetFont( KIUI::GetControlFont( this ) );

    // Anvil mockup look: file names are DATA, so the tree reads in the professional mono face
    // (IBM Plex Mono / Cascadia fallback) like the layer lists and status read-outs — one point
    // under the UI size, since the mono face runs wide and reads bigger at equal points.
    if( ADVANCED_CFG::GetCfg().m_AnvilPurpleFrame )
    {
        wxFont mono = KIUI::GetMonospacedUIFont();
        mono.SetFractionalPointSize( mono.GetFractionalPointSize() - 1.0 );
        SetFont( mono );
    }

    LoadIcons();

    // Anvil mono chrome icons: track the row under the cursor so its icon can flip to the
    // Signal-Emerald twin appended by LoadIcons() (see setHoverIconItem).
    Bind( wxEVT_MOTION, &PROJECT_TREE::onMouseMove, this );
    Bind( wxEVT_LEAVE_WINDOW, &PROJECT_TREE::onMouseLeave, this );
    Bind( wxEVT_TREE_DELETE_ITEM, &PROJECT_TREE::onItemDelete, this );
}


PROJECT_TREE::~PROJECT_TREE()
{
    // We pass ownership of these to wxWidgets in SetImageList() and SetStateImageList()
    //delete m_statusImageList;
}


void PROJECT_TREE::LoadIcons()
{
#ifdef __WXMAC__
    const int c_fileDefSize = 16;
    const int c_gitDefSize = 16;
#else
    // Anvil: match the 16px toolbar / title-bar / chrome icons (was 18 -> read too big next to
    // the compact mono tree font).
    const int c_fileDefSize = 16;
    const int c_gitDefSize = 16;
#endif

    // Anvil mono chrome icons: the file icons are flat Bone-white glyphs, and a Signal-Emerald
    // copy of the whole set is appended right after the base set — onMouseMove() swaps the row
    // under the cursor to its emerald twin, matching the toolbar / title-bar hover feedback.
    const bool mono = ADVANCED_CFG::GetCfg().m_AnvilMonoIcons;

    // Reloading (theme change) rebuilds the image list, so a raised hover index must be put
    // back first — the old indices are only valid against the old images.
    setHoverIconItem( wxTreeItemId() );

    auto getBundle = [&]( BITMAPS aBmp, int aDefSize, const wxColour* aFlat )
    {
#ifdef __WXMSW__
        // Add padding to bitmaps because MSW is the only platform that doesn't get it automatically
        const int          c_padding = 1;
        wxVector<wxBitmap> bmps;

        for( double scale : { 1.0, 1.25, 1.5, 1.75, 2.0, 3.0 } )
        {
            int            size = aDefSize * scale;
            int            paddedSize = size + c_padding * 2 * scale;
            wxBitmapBundle scaled = KiBitmapBundleDef( aBmp, size );
            wxBitmap       bmp = scaled.GetBitmap( wxDefaultSize );

            if( aFlat )
                bmp = KIUI::RecolorFlat( bmp, *aFlat );

            wxBitmap padded( paddedSize, paddedSize, 32 );

            {
                padded.UseAlpha();
                wxMemoryDC dc( padded );
                dc.DrawBitmap( bmp, c_padding * scale, c_padding * scale );
            }

            bmps.push_back( padded );
        }

        return wxBitmapBundle::FromBitmaps( bmps );
#else
        if( aFlat )
        {
            wxBitmapBundle     bundle = KiBitmapBundleDef( aBmp, aDefSize );
            wxVector<wxBitmap> bmps;

            for( double scale : { 1.0, 1.25, 1.5, 1.75, 2.0, 3.0 } )
            {
                int size = aDefSize * scale;
                bmps.push_back( KIUI::RecolorFlat( bundle.GetBitmap( wxSize( size, size ) ),
                                                   *aFlat ) );
            }

            return wxBitmapBundle::FromBitmaps( bmps );
        }

        return KiBitmapBundleDef( aBmp, aDefSize );
#endif
    };

    // One entry per TREE_FILE_TYPE, in enum order.
    static const BITMAPS c_fileIcons[] = {
        BITMAPS::project,                       // TREE_LEGACY_PROJECT
        BITMAPS::project_kicad,                 // TREE_JSON_PROJECT
        BITMAPS::icon_eeschema_24,              // TREE_LEGACY_SCHEMATIC
        BITMAPS::icon_eeschema_24,              // TREE_SEXPR_SCHEMATIC
        BITMAPS::icon_pcbnew_24,                // TREE_LEGACY_PCB
        BITMAPS::icon_pcbnew_24,                // TREE_SEXPR_PCB
        BITMAPS::icon_gerbview_24,              // TREE_GERBER
        BITMAPS::file_gerber_job,               // TREE_GERBER_JOB_FILE (.gbrjob)
        BITMAPS::file_html,                     // TREE_HTML
        BITMAPS::file_pdf,                      // TREE_PDF
        BITMAPS::editor,                        // TREE_TXT
        BITMAPS::editor,                        // TREE_MD
        BITMAPS::netlist,                       // TREE_NET
        BITMAPS::file_cir,                      // TREE_NET_SPICE
        BITMAPS::unknown,                       // TREE_UNKNOWN
        BITMAPS::directory,                     // TREE_DIRECTORY
        BITMAPS::icon_cvpcb_24,                 // TREE_CMP_LINK
        BITMAPS::tools,                         // TREE_REPORT
        BITMAPS::file_pos,                      // TREE_POS
        BITMAPS::file_drl,                      // TREE_DRILL
        BITMAPS::file_drl,                      // TREE_DRILL_NC (similar TREE_DRILL)
        BITMAPS::file_drl,                      // TREE_DRILL_XNC (similar TREE_DRILL)
        BITMAPS::file_svg,                      // TREE_SVG
        BITMAPS::file_csv,                      // TREE_CSV
        BITMAPS::icon_pagelayout_editor_24,     // TREE_PAGE_LAYOUT_DESCR
        BITMAPS::module,                        // TREE_FOOTPRINT_FILE
        BITMAPS::library,                       // TREE_SCHEMATIC_LIBFILE
        BITMAPS::library,                       // TREE_SEXPR_SYMBOL_LIB_FILE
        BITMAPS::editor,                        // DESIGN_RULES
        BITMAPS::zip,                           // ZIP_ARCHIVE
        BITMAPS::editor,                        // JOBSET_FILE
    };

    wxVector<wxBitmapBundle> images;

    // Filled-silhouette art (the folder, the project root) flattens to a solid near-black blob
    // in the light theme's ink tier, so those icons go through the Soft-Oat cream FILL tier
    // instead — see FILL_ICON_IDLE in the palette.
    auto flatFor = [&]( BITMAPS aIcon ) -> const wxColour*
    {
        if( !mono )
            return nullptr;

        if( aIcon == BITMAPS::directory || aIcon == BITMAPS::project
                || aIcon == BITMAPS::project_kicad )
            return &ANVIL::FILL_ICON_IDLE;

        return &ANVIL::INK_ICON_IDLE;
    };

    for( BITMAPS icon : c_fileIcons )
        images.push_back( getBundle( icon, c_fileDefSize, flatFor( icon ) ) );

    m_baseIconCount = (int) images.size();
    m_hasHoverIcons = mono;

    if( mono )
    {
        for( BITMAPS icon : c_fileIcons )
            images.push_back( getBundle( icon, c_fileDefSize, &ANVIL::INK_ICON_HOVER ) );
    }

    SetImages( images );

    // Anvil for macOS currently has backported SetStateImages for this control
    // that is otherwise available since wxWidgets 3.3 on other platforms.
#if wxCHECK_VERSION( 3, 3, 0 ) || defined( __WXMAC__ )
    wxVector<wxBitmapBundle> stateImages;
    stateImages.push_back( wxBitmapBundle( wxBitmap( c_gitDefSize, c_gitDefSize ) ) );      // GIT_STATUS_UNTRACKED
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_good_check, c_gitDefSize ) );    // GIT_STATUS_CURRENT
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_modified, c_gitDefSize ) );      // GIT_STATUS_MODIFIED
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_add, c_gitDefSize ) );           // GIT_STATUS_ADDED
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_delete, c_gitDefSize ) );        // GIT_STATUS_DELETED
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_out_of_date, c_gitDefSize ) );   // GIT_STATUS_BEHIND
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_changed_ahead, c_gitDefSize ) ); // GIT_STATUS_AHEAD
    stateImages.push_back( KiBitmapBundleDef( BITMAPS::git_conflict, c_gitDefSize ) );      // GIT_STATUS_CONFLICTED
    stateImages.push_back( wxBitmapBundle( wxBitmap( c_gitDefSize, c_gitDefSize ) ) );      // GIT_STATUS_IGNORED

    SetStateImages( stateImages );
#else
    // Make an image list containing small icons
    wxBitmap blank_bitmap( c_gitDefSize, c_gitDefSize );

    delete m_statusImageList;
    m_statusImageList = new wxImageList( c_gitDefSize, c_gitDefSize, true,
                                         static_cast<int>( KIGIT_COMMON::GIT_STATUS::GIT_STATUS_LAST ) );

    m_statusImageList->Add( blank_bitmap );                                         // GIT_STATUS_UNTRACKED
    m_statusImageList->Add( KiBitmap( BITMAPS::git_good_check, c_gitDefSize ) );    // GIT_STATUS_CURRENT
    m_statusImageList->Add( KiBitmap( BITMAPS::git_modified, c_gitDefSize ) );      // GIT_STATUS_MODIFIED
    m_statusImageList->Add( KiBitmap( BITMAPS::git_add, c_gitDefSize ) );           // GIT_STATUS_ADDED
    m_statusImageList->Add( KiBitmap( BITMAPS::git_delete, c_gitDefSize ) );        // GIT_STATUS_DELETED
    m_statusImageList->Add( KiBitmap( BITMAPS::git_out_of_date, c_gitDefSize ) );   // GIT_STATUS_BEHIND
    m_statusImageList->Add( KiBitmap( BITMAPS::git_changed_ahead, c_gitDefSize ) ); // GIT_STATUS_AHEAD
    m_statusImageList->Add( KiBitmap( BITMAPS::git_conflict, c_gitDefSize ) );      // GIT_STATUS_CONFLICTED
    m_statusImageList->Add( blank_bitmap );                                         // GIT_STATUS_IGNORED

    SetStateImageList( m_statusImageList );
#endif

}


void PROJECT_TREE::GetItemsRecursively( const wxTreeItemId& aParentId, std::vector<wxTreeItemId>& aItems )
{
    wxTreeItemIdValue cookie;
    wxTreeItemId      child = GetFirstChild( aParentId, cookie );

    while( child.IsOk() )
    {
        aItems.push_back( child );
        GetItemsRecursively( child, aItems );
        child = GetNextChild( aParentId, cookie );
    }
}


int PROJECT_TREE::OnCompareItems( const wxTreeItemId& item1, const wxTreeItemId& item2 )
{
    PROJECT_TREE_ITEM* myitem1 = (PROJECT_TREE_ITEM*) GetItemData( item1 );
    PROJECT_TREE_ITEM* myitem2 = (PROJECT_TREE_ITEM*) GetItemData( item2 );

    if( !myitem1 || !myitem2 )
        return 0;

    if( myitem1->GetType() == TREE_FILE_TYPE::DIRECTORY
            && myitem2->GetType() != TREE_FILE_TYPE::DIRECTORY )
        return -1;

    if( myitem2->GetType() == TREE_FILE_TYPE::DIRECTORY
            && myitem1->GetType() != TREE_FILE_TYPE::DIRECTORY )
        return 1;

    if( myitem1->IsRootFile() && !myitem2->IsRootFile() )
        return -1;

    if( myitem2->IsRootFile() && !myitem1->IsRootFile() )
        return 1;

    return myitem1->GetFileName().CmpNoCase( myitem2->GetFileName() );
}


void PROJECT_TREE::onMouseMove( wxMouseEvent& aEvent )
{
    aEvent.Skip();

    if( !m_hasHoverIcons )
        return;

    int          flags = 0;
    wxTreeItemId item  = HitTest( aEvent.GetPosition(), flags );

    // Light the icon while the cursor is anywhere on the row's content (icon, label, the
    // indent / expander area or the blank space right of the label).
    const int onRow = wxTREE_HITTEST_ONITEM | wxTREE_HITTEST_ONITEMINDENT
                      | wxTREE_HITTEST_ONITEMRIGHT | wxTREE_HITTEST_ONITEMSTATEICON;

    if( item.IsOk() && !( flags & onRow ) )
        item = wxTreeItemId();

    setHoverIconItem( item );
}


void PROJECT_TREE::onMouseLeave( wxMouseEvent& aEvent )
{
    setHoverIconItem( wxTreeItemId() );
    aEvent.Skip();
}


void PROJECT_TREE::onItemDelete( wxTreeEvent& aEvent )
{
    // The hovered row can be deleted out from under the cursor (project switch, file removed,
    // tree rebuild); the stored id would dangle, so forget it without touching the dying item.
    if( m_hoverIconItem == aEvent.GetItem() )
        m_hoverIconItem = wxTreeItemId();

    aEvent.Skip();
}


void PROJECT_TREE::setHoverIconItem( const wxTreeItemId& aItem )
{
    if( m_hoverIconItem == aItem )
        return;

    // Swap a row between its Bone-white icon (base index) and its Signal-Emerald twin (base
    // index + m_baseIconCount).  Both the normal and the selected slot move, because
    // PROJECT_TREE_ITEM keeps those two slots in lockstep.
    auto shift = [&]( const wxTreeItemId& aRow, int aDelta )
    {
        const int img = GetItemImage( aRow );
        const int lo  = aDelta > 0 ? 0 : m_baseIconCount;

        // Only move an index that sits in the expected half of the image list, so a reload or
        // a retype under the cursor can never walk an index out of range.
        if( img < lo || img >= lo + m_baseIconCount )
            return;

        SetItemImage( aRow, img + aDelta );
        SetItemImage( aRow, img + aDelta, wxTreeItemIcon_Selected );
    };

    if( m_hoverIconItem.IsOk() )
        shift( m_hoverIconItem, -m_baseIconCount );

    m_hoverIconItem = aItem;

    if( m_hoverIconItem.IsOk() )
        shift( m_hoverIconItem, m_baseIconCount );
}
