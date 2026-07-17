/*
 * Envil AI — schematic-side executor for AI tool calls. See envil_ai_tool_exec.h.
 */

#include "envil_ai_tool_exec.h"

#include <json_common.h>

#include <wx/string.h>
#include <wx/translation.h>

#include <sch_edit_frame.h>
#include <schematic.h>
#include <sch_screen.h>
#include <sch_symbol.h>
#include <sch_commit.h>
#include <lib_id.h>
#include <lib_symbol.h>
#include <base_units.h>

using json = nlohmann::json;


static json execAddComponent( SCH_EDIT_FRAME* aFrame, const json& aInput )
{
    wxString libIdStr  = wxString::FromUTF8( aInput.value( "lib_id", std::string() ) );
    wxString reference = wxString::FromUTF8( aInput.value( "reference", std::string() ) );
    wxString value     = wxString::FromUTF8( aInput.value( "value", std::string() ) );
    int      xMils     = aInput.value( "x_mils", 1000 );
    int      yMils     = aInput.value( "y_mils", 1000 );

    LIB_ID libId;

    if( libIdStr.IsEmpty() || libId.Parse( std::string( libIdStr.utf8_str() ) ) >= 0 )
        return { { "ok", false }, { "message", "Invalid or missing lib_id." } };

    LIB_SYMBOL* libSymbol = aFrame->GetLibSymbol( libId, false, false );

    if( !libSymbol )
    {
        return { { "ok", false },
                 { "message", "Symbol not found in libraries: "
                              + std::string( libIdStr.utf8_str() ) } };
    }

    VECTOR2I pos( schIUScale.MilsToIU( xMils ), schIUScale.MilsToIU( yMils ) );

    SCH_SYMBOL* symbol = new SCH_SYMBOL( *libSymbol, libId, &aFrame->GetCurrentSheet(), 1, 0, pos,
                                         &aFrame->Schematic() );

    if( !reference.IsEmpty() )
        symbol->SetRef( &aFrame->GetCurrentSheet(), reference );

    if( !value.IsEmpty() )
        symbol->SetValueFieldText( value );

    aFrame->AddToScreen( symbol, aFrame->GetScreen() );

    SCH_COMMIT commit( aFrame );
    commit.Added( symbol, aFrame->GetScreen() );
    commit.Push( _( "Envil AI: add symbol" ) );

    aFrame->GetCanvas()->Refresh();

    return { { "ok", true },
             { "message", "Placed " + std::string( reference.utf8_str() ) + " ("
                          + std::string( libIdStr.utf8_str() ) + ")." } };
}


std::string EnvilExecAiTool( SCH_EDIT_FRAME* aFrame, const std::string& aRequestJson )
{
    json result;

    try
    {
        if( !aFrame || !aFrame->Schematic().IsValid() )
        {
            result = { { "ok", false }, { "message", "No schematic is open." } };
        }
        else
        {
            json        req = json::parse( aRequestJson );
            std::string tool = req.value( "tool", std::string() );
            json        input = req.contains( "input" ) ? req["input"] : json::object();

            if( tool == "add_component" )
                result = execAddComponent( aFrame, input );
            else
                result = { { "ok", false }, { "message", "Unknown tool: " + tool } };
        }
    }
    catch( const std::exception& e )
    {
        result = { { "ok", false },
                   { "message", std::string( "Exception running tool: " ) + e.what() } };
    }

    return result.dump();
}
