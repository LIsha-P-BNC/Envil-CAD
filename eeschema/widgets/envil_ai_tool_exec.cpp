/*
 * Envil AI — schematic-side executor for AI tool calls. See envil_ai_tool_exec.h.
 *
 * The op vocabulary mirrors the one the Python (Nemi) backend used, so the model can drive
 * the schematic the same way: add_component / add_wire / add_label / add_junction /
 * add_no_connect / edit_value / move_component / delete_component.
 *
 * Units: mils throughout (Nemi used mm). Kept as mils to match add_component's existing
 * contract rather than silently mixing conventions.
 */

#include "envil_ai_tool_exec.h"

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


/// Find a placed symbol by its reference designator on the current sheet.
static SCH_SYMBOL* findSymbol( SCH_EDIT_FRAME* aFrame, const wxString& aRef )
{
    for( SCH_ITEM* item : aFrame->GetScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );

        if( sym->GetRef( &aFrame->GetCurrentSheet(), false ).IsSameAs( aRef, false ) )
            return sym;
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

        pts.push_back( milsPt( p[0].get<int>(), p[1].get<int>() ) );
    }

    SCH_COMMIT commit( aFrame );

    for( size_t i = 0; i + 1 < pts.size(); ++i )
    {
        SCH_LINE* wire = new SCH_LINE( pts[i], LAYER_WIRE );
        wire->SetEndPoint( pts[i + 1] );

        aFrame->AddToScreen( wire, aFrame->GetScreen() );
        commit.Added( wire, aFrame->GetScreen() );
    }

    commit.Push( _( "Envil AI: add wire" ) );

    return ok( "Added " + std::to_string( pts.size() - 1 ) + " wire segment(s)." );
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

    SCH_SYMBOL* sym = findSymbol( aFrame, ref );

    if( !sym )
        return fail( "No symbol with reference " + std::string( ref.utf8_str() ) + "." );

    SCH_COMMIT commit( aFrame );
    commit.Modify( sym, aFrame->GetScreen() );
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

    SCH_SYMBOL* sym = findSymbol( aFrame, ref );

    if( !sym )
        return fail( "No symbol with reference " + std::string( ref.utf8_str() ) + "." );

    VECTOR2I pos = milsPt( aInput.value( "x_mils", 0 ), aInput.value( "y_mils", 0 ) );

    SCH_COMMIT commit( aFrame );
    commit.Modify( sym, aFrame->GetScreen() );
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

    SCH_SYMBOL* sym = findSymbol( aFrame, ref );

    if( !sym )
        return fail( "No symbol with reference " + std::string( ref.utf8_str() ) + "." );

    SCH_COMMIT commit( aFrame );
    commit.Removed( sym, aFrame->GetScreen() );
    aFrame->RemoveFromScreen( sym, aFrame->GetScreen() );
    commit.Push( _( "Envil AI: delete symbol" ) );

    return ok( "Deleted " + std::string( ref.utf8_str() ) + "." );
}


/**
 * get_schematic: read-only dump of the open sheet so the model can see what is placed and
 * wire pin-to-pin. Each symbol reports its reference, value, lib_id, body position, and
 * every pin with number, name, and ABSOLUTE position in mils (transform already applied by
 * SCH_PIN::GetPosition) — which is exactly what add_wire needs to land on a pin.
 */
static json execGetSchematic( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    bool pins = aInput.value( "include_pins", true );

    json symbols = json::array();

    for( SCH_ITEM* item : aFrame->GetScreen()->Items().OfType( SCH_SYMBOL_T ) )
    {
        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );

        json s;
        s["reference"] = std::string( sym->GetRef( &aFrame->GetCurrentSheet(), false ).utf8_str() );
        s["value"] = std::string( sym->GetValue( true, &aFrame->GetCurrentSheet(), false ).utf8_str() );
        s["lib_id"] = sym->GetLibId().Format().wx_str().utf8_string();
        s["x_mils"] = iuToMils( sym->GetPosition().x );
        s["y_mils"] = iuToMils( sym->GetPosition().y );

        if( pins )
        {
            json pinArr = json::array();

            for( SCH_PIN* pin : sym->GetPins( &aFrame->GetCurrentSheet() ) )
            {
                VECTOR2I pp = pin->GetPosition();
                pinArr.push_back( { { "number", std::string( pin->GetNumber().utf8_str() ) },
                                    { "name", std::string( pin->GetName().utf8_str() ) },
                                    { "x_mils", iuToMils( pp.x ) },
                                    { "y_mils", iuToMils( pp.y ) } } );
            }

            s["pins"] = pinArr;
        }

        symbols.push_back( s );
    }

    return { { "ok", true },
             { "count", (int) symbols.size() },
             { "symbols", symbols },
             { "message", "Read " + std::to_string( symbols.size() ) + " symbol(s)." } };
}


std::string EnvilExecAiTool( SCH_EDIT_FRAME* aFrame, const std::string& aRequestJson )
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
