/*
 * Anvil AI — schematic-side executor for AI tool calls. See anvil_ai_tool_exec.h.
 *
 * The op vocabulary mirrors the one the Python (Nemi) backend used, so the model can drive
 * the schematic the same way: add_component / add_wire / add_label / add_junction /
 * add_no_connect / edit_value / move_component / delete_component.
 *
 * Units: mils throughout (Nemi used mm). Kept as mils to match add_component's existing
 * contract rather than silently mixing conventions.
 */

#include "anvil_ai_tool_exec.h"

#include <algorithm>
#include <vector>
#include <deque>
#include <set>
#include <utility>

#include <json_common.h>

#include <wx/string.h>
#include <wx/translation.h>

#include <sch_edit_frame.h>
#include <schematic.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <sch_pin.h>
#include <sch_line.h>
#include <sch_junction.h>
#include <sch_no_connect.h>
#include <sch_label.h>
#include <sch_commit.h>
#include <lib_id.h>
#include <lib_symbol.h>
#include <base_units.h>
#include <layer_ids.h>
#include <kiway.h>
#include <reporter.h>
#include <marker_base.h>
#include <sch_marker.h>
#include <sch_reference_list.h>
#include <erc/erc.h>
#include <erc/erc_item.h>
#include <erc/erc_settings.h>
#include <connection_graph.h>
#include <sch_connection.h>
#include <sch_io/sch_io.h>
#include <sch_io/sch_io_mgr.h>
#include <io/io_mgr.h>

using json = nlohmann::json;


/// mils -> internal units
static inline VECTOR2I milsPt( int aX, int aY )
{
    return VECTOR2I( schIUScale.MilsToIU( aX ), schIUScale.MilsToIU( aY ) );
}


/// internal units -> mils, for reporting positions back to the model.
static inline int iuToMils( int aIU )
{
    return schIUScale.IUToMils( aIU );
}


/// Snap a point to the nearest symbol pin within tolerance, so AI-drawn wire ends land
/// exactly on pins (Anvil only bonds a wire to a pin at an exact-coincident endpoint).
static VECTOR2I snapToPin( SCH_EDIT_FRAME* aFrame, const VECTOR2I& aPt )
{
    const int  tol = schIUScale.MilsToIU( 30 );   // < half the 50-mil grid
    long long  bestDist = (long long) tol * tol + 1;
    VECTOR2I   best = aPt;

    for( SCH_ITEM* item : aFrame->GetScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        for( SCH_PIN* pin : static_cast<SCH_SYMBOL*>( item )->GetPins( &aFrame->GetCurrentSheet() ) )
        {
            VECTOR2I  pp = pin->GetPosition();
            long long dx = pp.x - aPt.x, dy = pp.y - aPt.y;
            long long d = dx * dx + dy * dy;

            if( d < bestDist )
            {
                bestDist = d;
                best = pp;
            }
        }
    }

    return best;
}


/**
 * Every distinct screen in the hierarchy, paired with a sheet path that reaches it.
 *
 * The tools below work across the WHOLE hierarchy, not just the sheet that happens to be
 * open: ERC reports the entire design, so a reference the AI is told to fix may well live
 * on another page.  (A screen reached by several paths -- a re-used sub-sheet -- is only
 * returned once, so we never edit the same items twice.)
 */
static std::vector<std::pair<SCH_SHEET_PATH, SCH_SCREEN*>> allSheets( SCH_EDIT_FRAME* aFrame )
{
    std::vector<std::pair<SCH_SHEET_PATH, SCH_SCREEN*>> out;
    std::set<SCH_SCREEN*>                               seen;

    for( const SCH_SHEET_PATH& sheet : aFrame->Schematic().Hierarchy() )
    {
        SCH_SCREEN* screen = sheet.LastScreen();

        if( screen && seen.insert( screen ).second )
            out.emplace_back( sheet, screen );
    }

    if( out.empty() && aFrame->GetScreen() )
        out.emplace_back( aFrame->GetCurrentSheet(), aFrame->GetScreen() );

    return out;
}


/// Find a placed symbol by reference anywhere in the hierarchy, reporting the screen and
/// sheet path it lives on (needed to commit edits against the right screen).
static SCH_SYMBOL* findSymbol( SCH_EDIT_FRAME* aFrame, const wxString& aRef,
                               SCH_SCREEN** aScreenOut = nullptr,
                               SCH_SHEET_PATH* aPathOut = nullptr )
{
    for( const auto& [path, screen] : allSheets( aFrame ) )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );

            if( sym->GetRef( &path, false ).IsSameAs( aRef, false ) )
            {
                if( aScreenOut )
                    *aScreenOut = screen;

                if( aPathOut )
                    *aPathOut = path;

                return sym;
            }
        }
    }

    return nullptr;
}


static json ok( const std::string& aMsg )
{
    return { { "ok", true }, { "message", aMsg } };
}


static json fail( const std::string& aMsg )
{
    return { { "ok", false }, { "message", aMsg } };
}


static json execAddComponent( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString libIdStr  = wxString::FromUTF8( aInput.value( "lib_id", std::string() ) );
    wxString reference = wxString::FromUTF8( aInput.value( "reference", std::string() ) );
    wxString value     = wxString::FromUTF8( aInput.value( "value", std::string() ) );
    int      xMils     = aInput.value( "x_mils", 1000 );
    int      yMils     = aInput.value( "y_mils", 1000 );

    LIB_ID libId;

    if( libIdStr.IsEmpty() || libId.Parse( std::string( libIdStr.utf8_str() ) ) >= 0 )
        return fail( "Invalid or missing lib_id." );

    LIB_SYMBOL* libSymbol = aFrame->GetLibSymbol( libId, false, false );

    if( !libSymbol )
        return fail( "Symbol not found in libraries: " + std::string( libIdStr.utf8_str() ) );

    SCH_SYMBOL* symbol = new SCH_SYMBOL( *libSymbol, libId, &aFrame->GetCurrentSheet(), 1, 0,
                                         milsPt( xMils, yMils ), &aFrame->Schematic() );

    if( !reference.IsEmpty() )
        symbol->SetRef( &aFrame->GetCurrentSheet(), reference );

    if( !value.IsEmpty() )
        symbol->SetValueFieldText( value );

    aFrame->AddToScreen( symbol, aFrame->GetScreen() );

    SCH_COMMIT commit( aFrame );
    commit.Added( symbol, aFrame->GetScreen() );
    commit.Push( _( "Envil AI: add symbol" ) );

    return ok( "Placed " + std::string( reference.utf8_str() ) + " ("
               + std::string( libIdStr.utf8_str() ) + ")." );
}


/**
 * add_wire: {"points": [[x1,y1],[x2,y2], ...]} in mils. Emits one SCH_LINE per segment so
 * a multi-point path becomes a proper connected polyline of wires.
 */
static json execAddWire( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    if( !aInput.contains( "points" ) || !aInput["points"].is_array()
        || aInput["points"].size() < 2 )
    {
        return fail( "add_wire needs a 'points' array of at least two [x_mils, y_mils] pairs." );
    }

    std::vector<VECTOR2I> pts;

    for( const json& p : aInput["points"] )
    {
        if( !p.is_array() || p.size() < 2 )
            return fail( "Each point must be [x_mils, y_mils]." );

        // Snap each vertex to a nearby pin so wire ends bond exactly (Anvil needs the wire
        // endpoint coincident with the pin). Harmless when no pin is near.
        pts.push_back( snapToPin( aFrame, milsPt( p[0].get<int>(), p[1].get<int>() ) ) );
    }

    SCH_SCREEN*            screen = aFrame->GetScreen();
    SCH_COMMIT             commit( aFrame );
    std::deque<EDA_ITEM*>  newWires;
    int                    segCount = 0;

    for( size_t i = 0; i + 1 < pts.size(); ++i )
    {
        if( pts[i] == pts[i + 1] )
            continue;

        SCH_LINE* wire = new SCH_LINE( pts[i], LAYER_WIRE );
        wire->SetEndPoint( pts[i + 1] );

        aFrame->AddToScreen( wire, screen );
        commit.Added( wire, screen );
        newWires.push_back( wire );
        ++segCount;
    }

    // Auto-add junctions ONLY at the two terminal endpoints of the requested polyline.
    // A wire the AI deliberately ENDS on a rail (a T-tap) still gets its dot, so the ERC
    // loop converges; but a bend vertex or a segment merely PASSING THROUGH another net's
    // connection point never invents a junction. Crossings stay unconnected unless the
    // model explicitly calls add_junction there — a crossing in the source drawing (e.g.
    // a jump-over in an imported PDF schematic) is not a connection.
    int jctCount = 0;

    if( !newWires.empty() )
    {
        const VECTOR2I terminals[2] = {
                static_cast<SCH_LINE*>( newWires.front() )->GetStartPoint(),
                static_cast<SCH_LINE*>( newWires.back() )->GetEndPoint() };

        for( const VECTOR2I& pt : terminals )
        {
            if( !screen->IsExplicitJunctionNeeded( pt ) )
                continue;

            bool exists = false;

            for( SCH_ITEM* it : screen->Items().OfType( SCH_JUNCTION_T ) )
            {
                if( it->GetPosition() == pt )
                {
                    exists = true;
                    break;
                }
            }

            if( exists )
                continue;

            SCH_JUNCTION* jct = new SCH_JUNCTION( pt );
            aFrame->AddToScreen( jct, screen );
            commit.Added( jct, screen );
            ++jctCount;
        }
    }

    commit.Push( _( "Envil AI: add wire" ) );

    std::string msg = "Added " + std::to_string( segCount ) + " wire segment(s)";

    if( jctCount )
        msg += " and " + std::to_string( jctCount ) + " junction(s)";

    return ok( msg + "." );
}


/**
 * add_label: {"name","x_mils","y_mils","kind"} where kind is label | global | hier.
 */
static json execAddLabel( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString name = wxString::FromUTF8( aInput.value( "name", std::string() ) );

    if( name.IsEmpty() )
        return fail( "add_label needs a 'name'." );

    VECTOR2I    pos = milsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );
    std::string kind = aInput.value( "kind", std::string( "label" ) );

    SCH_LABEL_BASE* label = nullptr;

    if( kind == "global" || kind == "global_label" )
        label = new SCH_GLOBALLABEL( pos, name );
    else if( kind == "hier" || kind == "hier_label" || kind == "hierarchical" )
        label = new SCH_HIERLABEL( pos, name );
    else
        label = new SCH_LABEL( pos, name );

    aFrame->AddToScreen( label, aFrame->GetScreen() );

    SCH_COMMIT commit( aFrame );
    commit.Added( label, aFrame->GetScreen() );
    commit.Push( _( "Envil AI: add label" ) );

    return ok( "Added " + kind + " '" + std::string( name.utf8_str() ) + "'." );
}


static json execAddJunction( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    VECTOR2I      pos = milsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );
    SCH_JUNCTION* jct = new SCH_JUNCTION( pos );

    aFrame->AddToScreen( jct, aFrame->GetScreen() );

    SCH_COMMIT commit( aFrame );
    commit.Added( jct, aFrame->GetScreen() );
    commit.Push( _( "Envil AI: add junction" ) );

    return ok( "Added junction." );
}


static json execAddNoConnect( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    VECTOR2I        pos = milsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );
    SCH_NO_CONNECT* nc = new SCH_NO_CONNECT( pos );

    aFrame->AddToScreen( nc, aFrame->GetScreen() );

    SCH_COMMIT commit( aFrame );
    commit.Added( nc, aFrame->GetScreen() );
    commit.Push( _( "Envil AI: add no-connect" ) );

    return ok( "Added no-connect." );
}


static json execEditValue( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString ref = wxString::FromUTF8( aInput.value( "reference", std::string() ) );
    wxString val = wxString::FromUTF8( aInput.value( "new_value", std::string() ) );

    if( ref.IsEmpty() )
        return fail( "edit_value needs a 'reference'." );

    SCH_SCREEN* screen = nullptr;
    SCH_SYMBOL* sym = findSymbol( aFrame, ref, &screen );

    if( !sym )
        return fail( "No symbol with reference " + std::string( ref.utf8_str() ) + "." );

    SCH_COMMIT commit( aFrame );
    commit.Modify( sym, screen );
    sym->SetValueFieldText( val );
    commit.Push( _( "Envil AI: edit value" ) );

    aFrame->GetCanvas()->Refresh();

    return ok( "Set " + std::string( ref.utf8_str() ) + " value to '"
               + std::string( val.utf8_str() ) + "'." );
}


static json execMoveComponent( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString ref = wxString::FromUTF8( aInput.value( "reference", std::string() ) );

    if( ref.IsEmpty() )
        return fail( "move_component needs a 'reference'." );

    SCH_SCREEN* screen = nullptr;
    SCH_SYMBOL* sym = findSymbol( aFrame, ref, &screen );

    if( !sym )
        return fail( "No symbol with reference " + std::string( ref.utf8_str() ) + "." );

    VECTOR2I pos = milsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );

    SCH_COMMIT commit( aFrame );
    commit.Modify( sym, screen );
    sym->SetPosition( pos );
    commit.Push( _( "Envil AI: move symbol" ) );

    aFrame->GetCanvas()->Refresh();

    return ok( "Moved " + std::string( ref.utf8_str() ) + "." );
}


static json execDeleteComponent( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString ref = wxString::FromUTF8( aInput.value( "reference", std::string() ) );

    if( ref.IsEmpty() )
        return fail( "delete_component needs a 'reference'." );

    SCH_SCREEN* screen = nullptr;
    SCH_SYMBOL* sym = findSymbol( aFrame, ref, &screen );

    if( !sym )
        return fail( "No symbol with reference " + std::string( ref.utf8_str() ) + "." );

    SCH_COMMIT commit( aFrame );
    commit.Removed( sym, screen );
    aFrame->RemoveFromScreen( sym, screen );
    commit.Push( _( "Envil AI: delete symbol" ) );

    return ok( "Deleted " + std::string( ref.utf8_str() ) + "." );
}


/**
 * delete_at: remove stray non-symbol items (wires, labels, junctions, no-connects) at a
 * point. Symbols have delete_component; this cleans up the connectivity artifacts the AI
 * can otherwise never remove (orphan labels, dangling wire ends).
 */
static json execDeleteAt( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    VECTOR2I        pt = milsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );
    const int       tol = schIUScale.MilsToIU( aInput.value( "radius_mils", 30 ) );
    const long long tol2 = (long long) tol * tol;

    auto atPt = [&]( const VECTOR2I& p )
    {
        long long dx = p.x - pt.x, dy = p.y - pt.y;
        return dx * dx + dy * dy <= tol2;
    };

    std::vector<SCH_ITEM*> toDelete;

    for( SCH_ITEM* item : aFrame->GetScreen()->Items() )
    {
        bool hit = false;

        switch( item->Type() )
        {
        case SCH_LINE_T:
        {
            SCH_LINE* w = static_cast<SCH_LINE*>( item );

            if( w->GetLayer() == LAYER_WIRE
                && ( atPt( w->GetStartPoint() ) || atPt( w->GetEndPoint() ) ) )
                hit = true;

            break;
        }
        case SCH_JUNCTION_T:
        case SCH_NO_CONNECT_T:
        case SCH_LABEL_T:
        case SCH_GLOBAL_LABEL_T:
        case SCH_HIER_LABEL_T:
            hit = atPt( item->GetPosition() );
            break;
        default:
            break;
        }

        if( hit )
            toDelete.push_back( item );
    }

    if( toDelete.empty() )
        return fail( "No wire/label/junction/no-connect found at that point." );

    SCH_COMMIT commit( aFrame );

    for( SCH_ITEM* it : toDelete )
    {
        commit.Removed( it, aFrame->GetScreen() );
        aFrame->RemoveFromScreen( it, aFrame->GetScreen() );
    }

    commit.Push( _( "Envil AI: delete items" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Deleted " + std::to_string( toDelete.size() ) + " item(s)." );
}



/**
 * snap_to_grid: bring off-grid symbol pins and wire ends onto the connection grid.
 *
 * Imported designs (Altium in particular) land on a foreign grid, so pins can sit off the
 * 50-mil connection grid and silently fail to bond to on-grid wires -- a defect that looks
 * fine on screen. Snapping is done PER SYMBOL and carries attached wiring with it:
 *
 *   - the delta is computed from the symbol's own pins (not its origin), because a symbol
 *     can sit on-grid while its pin offsets are off-grid;
 *   - the symbol is moved by that delta, and every wire endpoint coincident with one of its
 *     pins is moved by the same delta, so existing connections are preserved rather than
 *     broken;
 *   - free wire ends (not on any pin) are then snapped on their own.
 */
static json execSnapToGrid( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    const int grid = schIUScale.MilsToIU( aInput.value( "grid_mils", 50 ) );

    if( grid <= 0 )
        return fail( "grid_mils must be positive." );

    auto snap = [grid]( int v ) -> int
    {
        return KiROUND( (double) v / grid ) * grid;
    };

    SCH_COMMIT commit( aFrame );
    int        movedSymbols = 0;
    int        movedWireEnds = 0;

    // Snap every sheet, not just the open one -- ERC's off-grid reports span the hierarchy.
    for( const auto& [path, screen] : allSheets( aFrame ) )
    {
    // --- symbols: snap by pin position, dragging coincident wire ends along ---
    std::vector<SCH_SYMBOL*> symbols;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        symbols.push_back( static_cast<SCH_SYMBOL*>( item ) );

    for( SCH_SYMBOL* sym : symbols )
    {
        std::vector<SCH_PIN*> pins = sym->GetPins( &path );

        if( pins.empty() )
            continue;

        VECTOR2I p0 = pins[0]->GetPosition();
        VECTOR2I delta( snap( p0.x ) - p0.x, snap( p0.y ) - p0.y );

        if( delta.x == 0 && delta.y == 0 )
            continue;

        // Record where the pins are BEFORE the move so we can find the wires on them.
        std::vector<VECTOR2I> pinPts;

        for( SCH_PIN* pin : pins )
            pinPts.push_back( pin->GetPosition() );

        commit.Modify( sym, screen );
        sym->Move( delta );
        ++movedSymbols;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_LINE_T ) )
        {
            SCH_LINE* wire = static_cast<SCH_LINE*>( item );

            if( wire->GetLayer() != LAYER_WIRE )
                continue;

            bool startHit = false;
            bool endHit = false;

            for( const VECTOR2I& pp : pinPts )
            {
                startHit = startHit || ( wire->GetStartPoint() == pp );
                endHit = endHit || ( wire->GetEndPoint() == pp );
            }

            if( !startHit && !endHit )
                continue;

            commit.Modify( wire, screen );

            if( startHit )
            {
                wire->SetStartPoint( wire->GetStartPoint() + delta );
                ++movedWireEnds;
            }

            if( endHit )
            {
                wire->SetEndPoint( wire->GetEndPoint() + delta );
                ++movedWireEnds;
            }
        }
    }

    // --- free wire ends: snap any endpoint that isn't sitting on a pin ---
    std::set<std::pair<int, int>> pinPositions;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        for( SCH_PIN* pin : static_cast<SCH_SYMBOL*>( item )->GetPins( &path ) )
        {
            VECTOR2I pp = pin->GetPosition();
            pinPositions.insert( { pp.x, pp.y } );
        }
    }

    for( SCH_ITEM* item : screen->Items().OfType( SCH_LINE_T ) )
    {
        SCH_LINE* wire = static_cast<SCH_LINE*>( item );

        if( wire->GetLayer() != LAYER_WIRE )
            continue;

        VECTOR2I s = wire->GetStartPoint();
        VECTOR2I e = wire->GetEndPoint();
        VECTOR2I ns( snap( s.x ), snap( s.y ) );
        VECTOR2I ne( snap( e.x ), snap( e.y ) );

        bool fixS = ( ns != s ) && !pinPositions.count( { s.x, s.y } );
        bool fixE = ( ne != e ) && !pinPositions.count( { e.x, e.y } );

        if( !fixS && !fixE )
            continue;

        commit.Modify( wire, screen );

        if( fixS )
        {
            wire->SetStartPoint( ns );
            ++movedWireEnds;
        }

        if( fixE )
        {
            wire->SetEndPoint( ne );
            ++movedWireEnds;
        }
    }
    }   // end hierarchy loop

    if( movedSymbols == 0 && movedWireEnds == 0 )
        return ok( "Everything is already on the connection grid." );

    commit.Push( _( "Anvil: snap to grid" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Snapped " + std::to_string( movedSymbols ) + " symbol(s) and "
               + std::to_string( movedWireEnds ) + " wire end(s) to the grid." );
}


/**
 * get_schematic: read-only dump of the open sheet so the model can see what is placed and
 * wire pin-to-pin. Each symbol reports its reference, value, lib_id, body position, and
 * every pin with number, name, ABSOLUTE position in mils (transform already applied by
 * SCH_PIN::GetPosition — exactly what add_wire needs to land on a pin) AND the NET it is
 * connected to. A top-level "nets" map (net -> ["R1.1", ...]) gives the model the real
 * connectivity it needs to analyse and correctly FIX ERC errors, not just pin geometry.
 */
static json execGetSchematic( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    bool pins = aInput.value( "include_pins", true );

    SCHEMATIC& sch = aFrame->Schematic();

    // Recompute connectivity so every pin can report its net. Rebuild the graph only
    // (no cleanup) — this is a read, it must not mutate the user's wires/geometry.
    SCH_SHEET_LIST sheets = sch.Hierarchy();
    sch.ConnectionGraph()->Recalculate( sheets, true );

    json symbols = json::array();
    json nets = json::object();   // net name -> ["R1.1", "U2.7", ...]

    // Report the WHOLE hierarchy: ERC covers every sheet, so a symbol the model is asked to
    // fix may live on a page other than the one currently open.
    for( const auto& [path, screen] : allSheets( aFrame ) )
    {
        const wxString sheetName = path.Last() ? path.Last()->GetName() : wxString( wxS( "/" ) );

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );

            std::string ref = std::string( sym->GetRef( &path, false ).utf8_str() );

            json s;
            s["reference"] = ref;
            s["value"] = std::string( sym->GetValue( true, &path, false ).utf8_str() );
            s["lib_id"] = sym->GetLibId().Format().wx_str().utf8_string();
            s["sheet"] = std::string( sheetName.utf8_str() );
            s["x_mils"] = iuToMils( sym->GetPosition().x );
            s["y_mils"] = iuToMils( sym->GetPosition().y );

            if( pins )
            {
                json pinArr = json::array();

                for( SCH_PIN* pin : sym->GetPins( &path ) )
                {
                    VECTOR2I    pp = pin->GetPosition();
                    std::string pinNum = std::string( pin->GetNumber().utf8_str() );

                    std::string netName;

                    if( SCH_CONNECTION* conn = pin->Connection( &path ) )
                        netName = std::string( conn->Name().utf8_str() );

                    json pinObj = { { "number", pinNum },
                                    { "name", std::string( pin->GetName().utf8_str() ) },
                                    { "x_mils", iuToMils( pp.x ) },
                                    { "y_mils", iuToMils( pp.y ) } };

                    if( !netName.empty() )
                    {
                        pinObj["net"] = netName;
                        nets[netName].push_back( ref + "." + pinNum );
                    }

                    pinArr.push_back( pinObj );
                }

                s["pins"] = pinArr;
            }

            symbols.push_back( s );
        }
    }

    return { { "ok", true },
             { "count", (int) symbols.size() },
             { "sch_file", std::string( sch.GetFileName().utf8_str() ) },
             { "symbols", symbols },
             { "nets", nets },
             { "message", "Read " + std::to_string( symbols.size() ) + " symbol(s), "
                                  + std::to_string( nets.size() ) + " net(s)." } };
}


/**
 * annotate: assign reference designators to un-annotated symbols (power flags, new parts).
 * ERC needs this — otherwise it flags "Item not annotated".
 */
static json execAnnotate( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    SCH_COMMIT commit( aFrame );
    NULL_REPORTER reporter;

    aFrame->AnnotateSymbols( &commit, ANNOTATE_ALL, SORT_BY_X_POSITION, INCREMENTAL_BY_REF,
                             false /*recursive*/, 0 /*startNum*/, false /*resetAnnotation*/,
                             true /*regroupUnits*/, false /*repairTimestamps*/, reporter,
                             SYMBOL_FILTER_NON_POWER );

    commit.Push( _( "Envil AI: annotate" ) );
    aFrame->GetCanvas()->Refresh();

    return ok( "Annotated the schematic." );
}


/**
 * run_erc: run the full native ERC suite on the live schematic and return every violation
 * (severity, rule title, detail message, position) so the AI can see its own errors and fix
 * them. This is what turns the panel into a self-correcting loop: wire -> run_erc -> fix ->
 * run_erc -> clean.
 */
static json execRunErc( SCH_EDIT_FRAME* aFrame, const json& )
{
    SCHEMATIC* sch = &aFrame->Schematic();

    sch->RecordERCExclusions();

    // Clear previous ERC markers so we only report the current state.
    SCH_SCREENS screens( sch->Root() );
    screens.DeleteAllMarkers( MARKER_BASE::MARKER_ERC, false );

    json violations = json::array();
    int  errors = 0;
    int  warnings = 0;

    // Annotation check first (mirrors the ERC dialog) — surface un-annotated symbols.
    int notAnnotated = aFrame->CheckAnnotate(
            [&]( ERCE_T, const wxString& aMsg, SCH_REFERENCE*, SCH_REFERENCE* )
            {
                violations.push_back( { { "severity", "error" },
                                        { "title", "Not annotated" },
                                        { "message", std::string( aMsg.utf8_str() ) } } );
                ++errors;
            },
            ANNOTATE_ALL, true, SYMBOL_FILTER_NON_POWER );

    (void) notAnnotated;

    // Run the full ERC suite. RunTests recalculates connectivity and drops SCH_MARKERs.
    ERC_TESTER tester( sch, false );
    tester.RunTests( aFrame->GetCanvas()->GetView()->GetDrawingSheet(), aFrame,
                     aFrame->Kiway().KiFACE( KIWAY::FACE_CVPCB, false ), &aFrame->Prj(), nullptr );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_MARKER_T ) )
        {
            SCH_MARKER* marker = static_cast<SCH_MARKER*>( item );

            if( marker->GetMarkerType() != MARKER_BASE::MARKER_ERC )
                continue;

            SEVERITY sev = marker->GetSeverity();

            if( sev == RPT_SEVERITY_IGNORE || sev == RPT_SEVERITY_EXCLUSION )
                continue;

            std::shared_ptr<RC_ITEM> rc = marker->GetRCItem();
            std::string              sevStr = "info";

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

            if( rc )
            {
                v["title"] = std::string( rc->GetErrorText( true ).utf8_str() );
                v["message"] = std::string( rc->GetErrorMessage( true ).utf8_str() );
            }

            v["x_mils"] = iuToMils( marker->GetPosition().x );
            v["y_mils"] = iuToMils( marker->GetPosition().y );
            violations.push_back( v );
        }
    }

    aFrame->GetCanvas()->Refresh();

    std::string summary = errors == 0
            ? ( warnings == 0 ? "ERC clean — no errors or warnings."
                              : "ERC clean of errors; " + std::to_string( warnings )
                                        + " warning(s)." )
            : std::to_string( errors ) + " error(s), " + std::to_string( warnings )
                      + " warning(s).";

    return { { "ok", true },
             { "clean", errors == 0 },
             { "error_count", errors },
             { "warning_count", warnings },
             { "sch_file", std::string( sch->GetFileName().utf8_str() ) },
             { "violations", violations },
             { "message", summary } };
}


std::string EnvilConvertSymbolLib( const std::string& aRequest )
{
    try
    {
        wxString req = wxString::FromUTF8( aRequest );
        wxString src = req.BeforeFirst( '\n' );
        wxString dest = req.AfterFirst( '\n' );

        if( src.IsEmpty() || dest.IsEmpty() )
            return "ERROR missing source or destination path";

        wxString ext = wxFileName( src ).GetExt().Lower();

        SCH_IO_MGR::SCH_FILE_T srcType;

        if( ext == wxT( "schlib" ) || ext == wxT( "intlib" ) )
            srcType = SCH_IO_MGR::SCH_ALTIUM;
        else if( ext == wxT( "lib" ) )
            srcType = SCH_IO_MGR::SCH_LEGACY;
        else
            srcType = SCH_IO_MGR::SCH_KICAD;

        IO_RELEASER<SCH_IO> srcPlugin( SCH_IO_MGR::FindPlugin( srcType ) );
        IO_RELEASER<SCH_IO> dstPlugin( SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_KICAD ) );

        if( !srcPlugin || !dstPlugin )
            return "ERROR no plugin for this library format";

        std::vector<LIB_SYMBOL*> symbols;
        srcPlugin->EnumerateSymbolLib( symbols, src );

        if( symbols.empty() )
            return "ERROR no symbols found in the source library";

        if( !wxFileName::FileExists( dest ) )
            dstPlugin->CreateLibrary( dest );

        int count = 0;

        for( LIB_SYMBOL* sym : symbols )
        {
            // SaveSymbol takes ownership of a flattened copy.
            dstPlugin->SaveSymbol( dest, new LIB_SYMBOL( *sym ) );
            ++count;
        }

        return "OK " + std::to_string( count ) + " symbols";
    }
    catch( const std::exception& e )
    {
        return std::string( "ERROR " ) + e.what();
    }
    catch( ... )
    {
        return "ERROR unknown failure reading the library";
    }
}


std::string AnvilExecAiTool( SCH_EDIT_FRAME* aFrame, const std::string& aRequestJson )
{
    json result;

    try
    {
        if( !aFrame || !aFrame->Schematic().IsValid() )
        {
            result = fail( "No schematic is open." );
        }
        else
        {
            json        req = json::parse( aRequestJson );
            std::string tool = req.value( "tool", std::string() );
            json        input = req.contains( "input" ) ? req["input"] : json::object();

            if( tool == "get_schematic" )
                result = execGetSchematic( aFrame, input );
            else if( tool == "run_erc" )
                result = execRunErc( aFrame, input );
            else if( tool == "snap_to_grid" )
                result = execSnapToGrid( aFrame, input );
            else if( tool == "annotate" )
                result = execAnnotate( aFrame, input );
            else if( tool == "add_component" )
                result = execAddComponent( aFrame, input );
            else if( tool == "add_wire" )
                result = execAddWire( aFrame, input );
            else if( tool == "add_label" )
                result = execAddLabel( aFrame, input );
            else if( tool == "add_junction" )
                result = execAddJunction( aFrame, input );
            else if( tool == "add_no_connect" )
                result = execAddNoConnect( aFrame, input );
            else if( tool == "edit_value" )
                result = execEditValue( aFrame, input );
            else if( tool == "move_component" )
                result = execMoveComponent( aFrame, input );
            else if( tool == "delete_component" )
                result = execDeleteComponent( aFrame, input );
            else if( tool == "delete_at" )
                result = execDeleteAt( aFrame, input );
            else
                result = fail( "Unknown tool: " + tool );

            aFrame->GetCanvas()->Refresh();
        }
    }
    catch( const std::exception& e )
    {
        result = fail( std::string( "Exception running tool: " ) + e.what() );
    }

    return result.dump();
}
