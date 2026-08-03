/*
 * Envil AI — board-side executor for AI tool calls. See envil_pcb_tool_exec.h.
 *
 * Mirrors the schematic tool set so the agent works the same way on both documents:
 * read (get_board), check (run_drc), and edit (move_footprint / add_track / add_via /
 * delete_track_at). Positions are in mils, matching the schematic tools, so the model never has
 * to switch conventions mid-design.
 */

#include "envil_pcb_tool_exec.h"

#include <map>
#include <set>
#include <vector>

#include <json_common.h>

#include <wx/string.h>
#include <wx/translation.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <pcb_edit_frame.h>
#include <board.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <pad.h>
#include <zone.h>
#include <pcb_track.h>
#include <pcb_marker.h>
#include <netinfo.h>
#include <pcb_io/pcb_io.h>
#include <pcb_io/pcb_io_mgr.h>
#include <io/io_mgr.h>
#include <base_units.h>
#include <ki_exception.h>
#include <lib_id.h>
#include <layer_ids.h>
#include <lset.h>
#include <rc_item.h>
#include <project.h>
#include <project_pcb.h>
#include <pgm_base.h>
#include <footprint_library_adapter.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>
#include <widgets/progress_reporter_base.h>
#include <tools/drc_tool.h>
#include <tool/tool_manager.h>
#include <drc/drc_engine.h>
#include <wildcards_and_files_ext.h>

using json = nlohmann::json;


/// DRC_TOOL::RunTests dereferences its reporter unconditionally, so a headless run needs a real
/// (but silent) one rather than nullptr.
class ENVIL_SILENT_REPORTER : public PROGRESS_REPORTER_BASE
{
public:
    ENVIL_SILENT_REPORTER() : PROGRESS_REPORTER_BASE( 1 ) {}

protected:
    bool updateUI() override { return true; }
};


static inline VECTOR2I pcbMilsPt( int aX, int aY )
{
    return VECTOR2I( pcbIUScale.mmToIU( aX * 0.0254 ), pcbIUScale.mmToIU( aY * 0.0254 ) );
}


static inline int pcbMilsToIU( int aMils )
{
    return pcbIUScale.mmToIU( aMils * 0.0254 );
}


static inline int pcbIuToMils( int aIU )
{
    return KiROUND( pcbIUScale.IUTomm( aIU ) / 0.0254 );
}


static inline std::string u8( const wxString& aStr )
{
    return std::string( aStr.utf8_str() );
}


static json ok( const std::string& aMsg )
{
    return { { "ok", true }, { "message", aMsg } };
}


static json fail( const std::string& aMsg )
{
    return { { "ok", false }, { "message", aMsg } };
}


/// Resolve a layer name ("F.Cu", "B.Cu", a user-renamed layer, ...) to its id.
static PCB_LAYER_ID layerFromName( BOARD* aBoard, const wxString& aName, PCB_LAYER_ID aDefault )
{
    if( aName.IsEmpty() )
        return aDefault;

    for( PCB_LAYER_ID layer : aBoard->GetEnabledLayers().Seq() )
    {
        if( aBoard->GetLayerName( layer ).IsSameAs( aName, false )
            || LSET::Name( layer ).IsSameAs( aName, false ) )
        {
            return layer;
        }
    }

    return aDefault;
}


static FOOTPRINT* findFootprint( BOARD* aBoard, const wxString& aRef )
{
    for( FOOTPRINT* fp : aBoard->Footprints() )
    {
        if( fp->GetReference().IsSameAs( aRef, false ) )
            return fp;
    }

    return nullptr;
}


/**
 * get_board: read-only dump so the model can see the layout -- footprints with reference,
 * value, position (mils), layer and rotation, plus board extents, copper layers and net names.
 * move_footprint / add_track work from these coordinates instead of guesses.
 */
static json execGetBoard( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    BOARD* board = aFrame->GetBoard();
    bool   wantPads = aInput.value( "include_pads", false );

    json footprints = json::array();

    for( FOOTPRINT* fp : board->Footprints() )
    {
        json f;
        f["reference"] = u8( fp->GetReference() );
        f["value"] = u8( fp->GetValueAsString() );
        f["fpid"] = u8( fp->GetFPID().Format().wx_str() );
        f["layer"] = u8( board->GetLayerName( fp->GetLayer() ) );
        f["rotation_deg"] = fp->GetOrientation().AsDegrees();
        f["x_mils"] = pcbIuToMils( fp->GetPosition().x );
        f["y_mils"] = pcbIuToMils( fp->GetPosition().y );

        if( wantPads )
        {
            json pads = json::array();

            for( PAD* pad : fp->Pads() )
            {
                pads.push_back( { { "number", u8( pad->GetNumber() ) },
                                  { "net", u8( pad->GetNetname() ) },
                                  { "x_mils", pcbIuToMils( pad->GetPosition().x ) },
                                  { "y_mils", pcbIuToMils( pad->GetPosition().y ) } } );
            }

            f["pads"] = pads;
        }

        footprints.push_back( f );
    }

    json layers = json::array();

    for( PCB_LAYER_ID layer : board->GetEnabledLayers().CuStack() )
        layers.push_back( u8( board->GetLayerName( layer ) ) );

    json nets = json::array();

    for( const auto& [code, net] : board->GetNetInfo().NetsByNetcode() )
    {
        if( net && !net->GetNetname().IsEmpty() )
            nets.push_back( u8( net->GetNetname() ) );
    }

    // What the sheet border / title block can already resolve; anything they reference beyond
    // this comes back from run_drc as "Unresolved text variable".
    json textVars = json::object();

    for( const auto& [name, value] : aFrame->Prj().GetTextVars() )
        textVars[u8( name )] = u8( value );

    BOX2I bbox = board->GetBoardEdgesBoundingBox();

    json extents;
    extents["x_mils"] = pcbIuToMils( bbox.GetLeft() );
    extents["y_mils"] = pcbIuToMils( bbox.GetTop() );
    extents["width_mils"] = pcbIuToMils( bbox.GetWidth() );
    extents["height_mils"] = pcbIuToMils( bbox.GetHeight() );

    int trackCount = 0;
    int viaCount = 0;

    for( PCB_TRACK* t : board->Tracks() )
    {
        if( t->Type() == PCB_VIA_T )
            ++viaCount;
        else
            ++trackCount;
    }

    return { { "ok", true },
             { "footprint_count", (int) footprints.size() },
             { "track_count", trackCount },
             { "via_count", viaCount },
             { "copper_layers", layers },
             { "nets", nets },
             { "text_variables", textVars },
             { "board_extents", extents },
             { "footprints", footprints },
             { "message", "Read " + std::to_string( footprints.size() ) + " footprint(s), "
                                  + std::to_string( trackCount ) + " track(s), "
                                  + std::to_string( viaCount ) + " via(s)." } };
}


/**
 * run_drc: run the full Design Rules Check and return every violation, so the agent can close
 * the same check-fix-recheck loop on the board that run_erc gives it on the schematic.
 */
static json execRunDrc( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    BOARD*    board = aFrame->GetBoard();
    DRC_TOOL* drcTool = aFrame->GetToolManager()->GetTool<DRC_TOOL>();

    if( !drcTool )
        return fail( "The DRC tool is not available." );

    board->DeleteMARKERs( true, true );

    try
    {
        drcTool->GetDRCEngine()->InitEngine( aFrame->GetDesignRulesPath() );
    }
    catch( const PARSE_ERROR& e )
    {
        return fail( "Could not load the design rules: " + u8( e.What() ) );
    }

    ENVIL_SILENT_REPORTER reporter;

    drcTool->RunTests( &reporter, aInput.value( "refill_zones", false ),
                       aInput.value( "report_all_track_errors", true ),
                       aInput.value( "test_footprints", false ) );

    json violations = json::array();
    int  errors = 0;
    int  warnings = 0;

    for( PCB_MARKER* marker : board->Markers() )
    {
        SEVERITY sev = marker->GetSeverity();

        if( sev == RPT_SEVERITY_IGNORE || sev == RPT_SEVERITY_EXCLUSION )
            continue;

        std::string sevStr = "info";

        if( sev == RPT_SEVERITY_ERROR )
        {
            sevStr = "error";
            ++errors;
        }
        else if( sev == RPT_SEVERITY_WARNING )
        {
            sevStr = "warning";
            ++warnings;
        }

        json v;
        v["severity"] = sevStr;

        if( std::shared_ptr<RC_ITEM> rc = marker->GetRCItem() )
        {
            v["title"] = u8( rc->GetErrorText( true ) );
            v["message"] = u8( rc->GetErrorMessage( true ) );

            // Name the offending item. Without it a violation like "Unresolved text variable"
            // says what is wrong but not which item to go fix.
            if( EDA_ITEM* item = aFrame->ResolveItem( rc->GetMainItemID(), true ) )
                v["item"] = u8( item->GetItemDescription( aFrame, true ) );
        }

        v["x_mils"] = pcbIuToMils( marker->GetPos().x );
        v["y_mils"] = pcbIuToMils( marker->GetPos().y );
        violations.push_back( v );
    }

    aFrame->GetCanvas()->Refresh();

    std::string summary =
            errors == 0
                    ? ( warnings == 0
                                ? "DRC clean — no errors or warnings."
                                : "DRC clean of errors; " + std::to_string( warnings )
                                          + " warning(s)." )
                    : std::to_string( errors ) + " error(s), " + std::to_string( warnings )
                              + " warning(s).";

    return { { "ok", true },
             { "clean", errors == 0 },
             { "error_count", errors },
             { "warning_count", warnings },
             { "violations", violations },
             { "message", summary } };
}


/**
 * add_footprint: place a footprint from the configured libraries. The board's add_component --
 * for parts that come from the layout rather than from the netlist (mounting holes, fiducials,
 * test points), or for building a board from scratch.
 */
static json execAddFootprint( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    std::string fpidStr = aInput.value( "fpid", std::string() );

    if( fpidStr.empty() )
        return fail( "add_footprint needs an 'fpid' like 'Resistor_SMD:R_0603_1608Metric'." );

    LIB_ID fpid;

    if( fpid.Parse( wxString::FromUTF8( fpidStr ) ) >= 0 )
    {
        return fail( "'" + fpidStr + "' is not a valid library id; expected "
                     "'Library:Footprint'." );
    }

    FOOTPRINT* fp = nullptr;

    try
    {
        fp = aFrame->LoadFootprint( fpid );
    }
    catch( const IO_ERROR& e )
    {
        return fail( "Could not load " + fpidStr + ": " + u8( e.What() ) );
    }

    if( !fp )
        return fail( "No footprint " + fpidStr + " in the configured libraries." );

    // A library built from a board can store a footprint flipped and rotated; normalize to
    // front / 0 degrees first, the way AddFootprintToBoard does, so the requested transform
    // means what it says.
    fp->SetPosition( VECTOR2I( 0, 0 ) );

    if( fp->IsFlipped() )
        fp->Flip( fp->GetPosition(), FLIP_DIRECTION::TOP_BOTTOM );

    fp->SetOrientation( ANGLE_0 );

    if( aInput.contains( "reference" ) )
        fp->SetReference( wxString::FromUTF8( aInput["reference"].get<std::string>() ) );

    if( aInput.contains( "value" ) )
        fp->SetValue( wxString::FromUTF8( aInput["value"].get<std::string>() ) );

    if( aInput.contains( "rotation_deg" ) )
        fp->SetOrientation( EDA_ANGLE( aInput["rotation_deg"].get<double>(), DEGREES_T ) );

    wxString layerName = wxString::FromUTF8( aInput.value( "layer", std::string() ) );

    if( layerFromName( aFrame->GetBoard(), layerName, F_Cu ) == B_Cu )
        fp->Flip( fp->GetPosition(), FLIP_DIRECTION::TOP_BOTTOM );

    fp->SetPosition( pcbMilsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) ) );

    BOARD_COMMIT commit( aFrame );
    commit.Add( fp );
    commit.Push( _( "Envil AI: add footprint" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Placed " + fpidStr + " as " + u8( fp->GetReference() ) + "." );
}


static json execMoveFootprint( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString ref = wxString::FromUTF8( aInput.value( "reference", std::string() ) );

    if( ref.IsEmpty() )
        return fail( "move_footprint needs a 'reference'." );

    FOOTPRINT* fp = findFootprint( aFrame->GetBoard(), ref );

    if( !fp )
        return fail( "No footprint with reference " + u8( ref ) + " on this board." );

    BOARD_COMMIT commit( aFrame );
    commit.Modify( fp );

    if( aInput.contains( "x_mils" ) && aInput.contains( "y_mils" ) )
        fp->SetPosition( pcbMilsPt( aInput["x_mils"].get<int>(), aInput["y_mils"].get<int>() ) );

    if( aInput.contains( "rotation_deg" ) )
        fp->SetOrientation( EDA_ANGLE( aInput["rotation_deg"].get<double>(), DEGREES_T ) );

    if( aInput.value( "flip", false ) )
        fp->Flip( fp->GetPosition(), FLIP_DIRECTION::TOP_BOTTOM );

    commit.Push( _( "Envil AI: move footprint" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Moved " + u8( ref ) + "." );
}


/**
 * add_track: route copper along a point path -- one segment per consecutive pair, on the named
 * layer and net. The board equivalent of add_wire.
 */
static json execAddTrack( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    if( !aInput.contains( "points" ) || !aInput["points"].is_array()
        || aInput["points"].size() < 2 )
    {
        return fail( "add_track needs a 'points' array of at least two [x_mils, y_mils] pairs." );
    }

    BOARD*                board = aFrame->GetBoard();
    std::vector<VECTOR2I> pts;

    for( const json& p : aInput["points"] )
    {
        if( !p.is_array() || p.size() < 2 )
            return fail( "Each entry of 'points' must be [x_mils, y_mils]." );

        pts.push_back( pcbMilsPt( p[0].get<int>(), p[1].get<int>() ) );
    }

    PCB_LAYER_ID layer = layerFromName( board,
                                        wxString::FromUTF8( aInput.value( "layer",
                                                                          std::string() ) ),
                                        F_Cu );

    if( !IsCopperLayer( layer ) )
        return fail( "Tracks must be on a copper layer (e.g. F.Cu or B.Cu)." );

    int width = aInput.contains( "width_mils" )
                        ? pcbMilsToIU( aInput["width_mils"].get<int>() )
                        : board->GetDesignSettings().GetCurrentTrackWidth();

    int netCode = 0;

    if( aInput.contains( "net" ) )
    {
        wxString      netName = wxString::FromUTF8( aInput["net"].get<std::string>() );
        NETINFO_ITEM* net = board->FindNet( netName );

        if( !net )
            return fail( "No net named " + u8( netName ) + " on this board." );

        netCode = net->GetNetCode();
    }

    BOARD_COMMIT commit( aFrame );
    int          segCount = 0;

    for( size_t i = 0; i + 1 < pts.size(); ++i )
    {
        if( pts[i] == pts[i + 1] )
            continue;

        PCB_TRACK* track = new PCB_TRACK( board );
        track->SetStart( pts[i] );
        track->SetEnd( pts[i + 1] );
        track->SetWidth( width );
        track->SetLayer( layer );
        track->SetNetCode( netCode );

        commit.Add( track );
        ++segCount;
    }

    if( segCount == 0 )
        return fail( "No usable track segments (all the points were coincident)." );

    commit.Push( _( "Envil AI: add track" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Added " + std::to_string( segCount ) + " track segment(s) on "
               + u8( board->GetLayerName( layer ) ) + "." );
}


static json execAddVia( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    BOARD*   board = aFrame->GetBoard();
    VECTOR2I pos = pcbMilsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );

    int netCode = 0;

    if( aInput.contains( "net" ) )
    {
        wxString      netName = wxString::FromUTF8( aInput["net"].get<std::string>() );
        NETINFO_ITEM* net = board->FindNet( netName );

        if( !net )
            return fail( "No net named " + u8( netName ) + " on this board." );

        netCode = net->GetNetCode();
    }

    PCB_VIA* via = new PCB_VIA( board );
    via->SetPosition( pos );
    via->SetWidth( aInput.contains( "diameter_mils" )
                           ? pcbMilsToIU( aInput["diameter_mils"].get<int>() )
                           : board->GetDesignSettings().GetCurrentViaSize() );
    via->SetDrill( aInput.contains( "drill_mils" )
                           ? pcbMilsToIU( aInput["drill_mils"].get<int>() )
                           : board->GetDesignSettings().GetCurrentViaDrill() );
    via->SetLayerPair( F_Cu, B_Cu );
    via->SetNetCode( netCode );

    BOARD_COMMIT commit( aFrame );
    commit.Add( via );
    commit.Push( _( "Envil AI: add via" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Added a via." );
}


/**
 * set_text_variable: define a project text variable, e.g. REVISION or ISSUE_DATE.
 *
 * An imported board's sheet border and title block reference ${VARIABLES} the project never
 * defines, which DRC reports as "Unresolved text variable" errors. Deleting that text would be
 * the wrong fix; defining the variable is the right one.
 */
static json execSetTextVariable( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString name = wxString::FromUTF8( aInput.value( "name", std::string() ) );

    if( name.IsEmpty() )
        return fail( "set_text_variable needs a 'name'." );

    // Accept either NAME or ${NAME} -- the model reads the latter off the sheet.
    if( name.StartsWith( wxS( "${" ) ) && name.EndsWith( wxS( "}" ) ) )
        name = name.Mid( 2, name.Length() - 3 );

    PROJECT& prj = aFrame->Prj();
    prj.GetTextVars()[name] = wxString::FromUTF8( aInput.value( "value", std::string() ) );
    prj.IncrementTextVarsTicker();

    aFrame->GetBoard()->SynchronizeProperties();
    aFrame->OnModify();
    aFrame->GetCanvas()->Refresh();

    return ok( "Set ${" + u8( name ) + "}." );
}


/**
 * capture_footprints: harvest the board's footprints into a project-local library.
 *
 * Runs the same capture the importer does, so a board imported before that existed -- or one
 * whose footprints name libraries this installation doesn't have -- can be fixed in place
 * instead of re-imported. Clears "footprint not found in libraries" warnings.
 */
static json execCaptureFootprints( PCB_EDIT_FRAME* aFrame )
{
    std::string result = EnvilCaptureBoardFootprints( aFrame );

    if( result.rfind( "OK ", 0 ) == 0 )
    {
        return ok( "Captured the board's footprints into the project library and registered it: "
                   + result.substr( 3 ) + "." );
    }

    return fail( result.rfind( "ERROR ", 0 ) == 0 ? result.substr( 6 ) : result );
}


/// delete_track_at: remove the tracks/vias that end near a point -- the board's delete_at.
static json execDeleteTrackAt( PCB_EDIT_FRAME* aFrame, const json& aInput )
{
    BOARD*          board = aFrame->GetBoard();
    VECTOR2I        pt = pcbMilsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );
    const int       tol = pcbMilsToIU( aInput.value( "radius_mils", 20 ) );
    const long long tol2 = (long long) tol * tol;

    auto atPt = [&]( const VECTOR2I& p )
    {
        long long dx = p.x - pt.x, dy = p.y - pt.y;
        return dx * dx + dy * dy <= tol2;
    };

    std::vector<PCB_TRACK*> toDelete;

    for( PCB_TRACK* t : board->Tracks() )
    {
        if( atPt( t->GetStart() ) || atPt( t->GetEnd() ) )
            toDelete.push_back( t );
    }

    if( toDelete.empty() )
        return fail( "No track or via found at that point." );

    BOARD_COMMIT commit( aFrame );

    for( PCB_TRACK* t : toDelete )
        commit.Remove( t );

    commit.Push( _( "Envil AI: delete track" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Deleted " + std::to_string( toDelete.size() ) + " track/via item(s)." );
}


std::string EnvilCaptureBoardFootprints( PCB_EDIT_FRAME* aFrame )
{
    try
    {
        BOARD* board = aFrame->GetBoard();

        if( board->Footprints().empty() )
            return "ERROR this board has no footprints to capture";

        // ---- 1. create the project-local <project>.pretty ----
        // The project name doubles as the library nickname, so run it through LIB_ID's own
        // sanitizer rather than trusting that a filename is always a legal nickname.
        wxString projectNick =
                LIB_ID::FixIllegalChars( aFrame->Prj().GetProjectName(), true ).wx_str();

        if( projectNick.IsEmpty() )
            projectNick = wxS( "Anvil" );

        wxFileName libFn( aFrame->Prj().GetProjectPath(), wxEmptyString );
        libFn.AppendDir( projectNick + wxS( "." ) + FILEEXT::KiCadFootprintLibPathExtension );

        const wxString libDirName = libFn.GetDirs().Last();
        const wxString libPath = libFn.GetPath();

        IO_RELEASER<PCB_IO> plugin( PCB_IO_MGR::FindPlugin( PCB_IO_MGR::KICAD_SEXP ) );

        if( !plugin )
            return "ERROR no footprint library plugin available";

        if( !wxFileName::DirExists( libPath ) )
            plugin->CreateLibrary( libPath );

        // ---- 2. write every footprint, and note which nicknames need a home ----
        LIBRARY_MANAGER&        manager = Pgm().GetLibraryManager();
        std::set<wxString>      unresolved;   // referenced, but in no library table
        std::vector<FOOTPRINT*> untagged;     // no nickname at all -- retag to the project lib
        int                     count = 0;

        for( FOOTPRINT* fp : board->Footprints() )
        {
            if( fp->GetFPID().GetUniStringLibItemName().IsEmpty() )
                continue;   // nothing to name the library file after

            // Save a detached copy, the way "Export footprints to library" does, so board-only
            // state (designator, group, zone offset) doesn't leak into the library.
            FOOTPRINT* fpCopy = static_cast<FOOTPRINT*>( fp->Duplicate( IGNORE_PARENT_GROUP ) );
            fpCopy->SetReference( wxS( "REF**" ) );
            fpCopy->SetParentGroup( nullptr );

            for( ZONE* zone : fpCopy->Zones() )
                zone->Move( -fpCopy->GetPosition() );

            plugin->FootprintSave( libPath, fpCopy );
            delete fpCopy;
            ++count;

            wxString nick = fp->GetFPID().GetLibNickname();

            if( nick.IsEmpty() )
                untagged.push_back( fp );
            else if( !manager.GetFullURI( LIBRARY_TABLE_TYPE::FOOTPRINT, nick ).has_value() )
                unresolved.insert( nick );
        }

        if( count == 0 )
            return "ERROR none of this board's footprints have a library name to save under";

        // ---- 3. point every homeless nickname at that library ----
        std::optional<LIBRARY_TABLE*> optTable =
                manager.Table( LIBRARY_TABLE_TYPE::FOOTPRINT, LIBRARY_TABLE_SCOPE::PROJECT );

        if( !optTable.has_value() )
            return "ERROR could not open the project footprint library table";

        LIBRARY_TABLE* table = optTable.value();
        const wxString uri = wxS( "${KIPRJMOD}/" ) + libDirName;

        std::vector<wxString> wanted( unresolved.begin(), unresolved.end() );
        std::vector<wxString> toLoad;

        if( !untagged.empty() )
            wanted.push_back( projectNick );

        for( const wxString& nick : wanted )
        {
            if( table->HasRow( nick ) )
                continue;

            LIBRARY_TABLE_ROW& row = table->InsertRow();
            row.SetNickname( nick );
            row.SetURI( uri );
            row.SetType( PCB_IO_MGR::ShowType( PCB_IO_MGR::KICAD_SEXP ) );
            toLoad.emplace_back( nick );
        }

        if( !toLoad.empty() )
        {
            bool saved = true;

            table->Save().map_error(
                    [&]( const LIBRARY_ERROR& aError )
                    {
                        wxLogError( wxT( "Error saving project footprint library table:\n\n" )
                                    + aError.message );
                        saved = false;
                    } );

            if( !saved )
                return "ERROR could not save the project footprint library table";

            manager.AbortAsyncLoads();
            manager.LoadProjectTables( { LIBRARY_TABLE_TYPE::FOOTPRINT } );

            FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &aFrame->Prj() );

            if( adapter )
            {
                for( const wxString& nick : toLoad )
                    adapter->LoadOne( nick );
            }
        }

        // ---- 4. give the nickname-less footprints one, so the board resolves them ----
        if( !untagged.empty() )
        {
            BOARD_COMMIT commit( aFrame );

            for( FOOTPRINT* fp : untagged )
            {
                commit.Modify( fp );

                LIB_ID id = fp->GetFPID();
                id.SetLibNickname( projectNick );
                fp->SetFPID( id );
            }

            commit.Push( _( "Anvil: link footprints to the project library" ) );
        }

        return "OK " + std::to_string( count ) + " footprints";
    }
    catch( const IO_ERROR& e )
    {
        return "ERROR " + u8( e.What() );
    }
    catch( const std::exception& e )
    {
        return std::string( "ERROR " ) + e.what();
    }
    catch( ... )
    {
        return "ERROR unknown failure capturing footprints";
    }
}


std::string EnvilExecPcbTool( PCB_EDIT_FRAME* aFrame, const std::string& aRequestJson )
{
    json result;

    try
    {
        if( !aFrame || !aFrame->GetBoard() )
        {
            result = fail( "No board is open." );
        }
        else
        {
            json        req = json::parse( aRequestJson );
            std::string tool = req.value( "tool", std::string() );
            json        input = req.contains( "input" ) ? req["input"] : json::object();

            if( tool == "get_board" )
                result = execGetBoard( aFrame, input );
            else if( tool == "run_drc" )
                result = execRunDrc( aFrame, input );
            else if( tool == "add_footprint" )
                result = execAddFootprint( aFrame, input );
            else if( tool == "move_footprint" )
                result = execMoveFootprint( aFrame, input );
            else if( tool == "add_track" )
                result = execAddTrack( aFrame, input );
            else if( tool == "add_via" )
                result = execAddVia( aFrame, input );
            else if( tool == "delete_track_at" )
                result = execDeleteTrackAt( aFrame, input );
            else if( tool == "set_text_variable" )
                result = execSetTextVariable( aFrame, input );
            else if( tool == "capture_footprints" )
                result = execCaptureFootprints( aFrame );
            else
                result = fail( "Unknown board tool: " + tool );
        }
    }
    catch( const IO_ERROR& e )
    {
        result = fail( "Board error: " + u8( e.What() ) );
    }
    catch( const std::exception& e )
    {
        result = fail( std::string( "Exception running board tool: " ) + e.what() );
    }

    return result.dump();
}
