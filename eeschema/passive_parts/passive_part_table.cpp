/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "passive_part_table.h"

#include <cmath>
#include <limits>

#include <wx/regex.h>

#include <json_common.h>

#include <sim/sim_value.h>

namespace PASSIVE_PARTS
{

namespace
{

/// Imperial chip package codes recognized in footprint names and imported Package columns.
const std::vector<wxString>& knownPackages()
{
    static const std::vector<wxString> packages = {
        wxS( "01005" ), wxS( "0075" ), wxS( "0100" ), wxS( "0201" ), wxS( "0402" ),
        wxS( "0603" ),  wxS( "0805" ), wxS( "1206" ), wxS( "1210" ), wxS( "1218" ),
        wxS( "1808" ),  wxS( "1812" ), wxS( "2010" ), wxS( "2220" ), wxS( "2225" ),
        wxS( "2512" ),  wxS( "2920" ),
    };

    return packages;
}


/// Case-insensitively remove every occurrence of aWord from aText.
void removeWordCI( wxString& aText, const wxString& aWord )
{
    for( ;; )
    {
        int pos = aText.Lower().Find( aWord.Lower() );

        if( pos == wxNOT_FOUND )
            break;

        aText.erase( pos, aWord.length() );
    }
}

} // namespace


int64_t ValueScaleForType( PASSIVE_TYPE aType )
{
    switch( aType )
    {
    case PASSIVE_TYPE::RESISTOR:  return INT64_C( 1000 );              // milliohms
    case PASSIVE_TYPE::CAPACITOR: return INT64_C( 1000000000000000 );  // femtofarads
    case PASSIVE_TYPE::INDUCTOR:  return INT64_C( 1000000000000 );     // picohenries
    default:                      return 0;
    }
}


bool NormalizePassiveValue( const wxString& aText, PASSIVE_TYPE aType, int64_t* aResult )
{
    if( aType == PASSIVE_TYPE::UNKNOWN || !aResult )
        return false;

    wxString s = aText;
    s.Trim( true ).Trim( false );

    if( s.IsEmpty() || s == wxS( "~" ) )
        return false;

    // Unicode micro signs (U+00B5 micro, U+03BC Greek mu) are the 'u' SI prefix
    s.Replace( wxString::FromUTF8( "\xC2\xB5" ), wxS( "u" ) );
    s.Replace( wxString::FromUTF8( "\xCE\xBC" ), wxS( "u" ) );

    // Unit words, longest first so "ohms" goes before "ohm"
    removeWordCI( s, wxS( "ohms" ) );
    removeWordCI( s, wxS( "ohm" ) );
    removeWordCI( s, wxS( "farads" ) );
    removeWordCI( s, wxS( "farad" ) );
    removeWordCI( s, wxS( "henries" ) );
    removeWordCI( s, wxS( "henrys" ) );
    removeWordCI( s, wxS( "henry" ) );

    // Unit symbols: U+03A9 Greek omega, U+2126 ohm sign
    s.Replace( wxString::FromUTF8( "\xCE\xA9" ), wxEmptyString );
    s.Replace( wxString::FromUTF8( "\xE2\x84\xA6" ), wxEmptyString );

    s.Trim( true ).Trim( false );

    // Trailing unit letter for capacitors/inductors ("100nF", "10uH")
    if( aType == PASSIVE_TYPE::CAPACITOR
            && ( s.EndsWith( wxS( "F" ) ) || s.EndsWith( wxS( "f" ) ) ) )
    {
        s.RemoveLast();
    }
    else if( aType == PASSIVE_TYPE::INDUCTOR
            && ( s.EndsWith( wxS( "H" ) ) || s.EndsWith( wxS( "h" ) ) ) )
    {
        s.RemoveLast();
    }

    // Drop whitespace and any remaining non-ASCII characters.  The latter makes the parser
    // tolerant of mojibake (a UTF-8 file read as CP1252 turns the ohm sign into two junk
    // characters) and of leftover degree-style decorations.
    wxString clean;

    for( wxUniChar c : s )
    {
        if( c < 128 && c != ' ' && c != '\t' )
            clean += c;
    }

    if( clean.IsEmpty() )
        return false;

    // Comma handling: "3,300" style thousands separators vs European decimal commas
    if( clean.Contains( wxS( "." ) ) )
    {
        clean.Replace( wxS( "," ), wxEmptyString );
    }
    else if( clean.Contains( wxS( "," ) ) )
    {
        wxRegEx thousands( wxS( "^[0-9]+(,[0-9]{3})+$" ) );
        wxString digitsOnly = clean;

        // Look only at the leading numeric part for the thousands test
        wxRegEx numericPrefix( wxS( "^([0-9,]+)" ) );

        if( numericPrefix.Matches( clean ) )
            digitsOnly = numericPrefix.GetMatch( clean, 1 );

        if( thousands.Matches( digitsOnly ) )
            clean.Replace( wxS( "," ), wxEmptyString );
        else
            clean.Replace( wxS( "," ), wxS( "." ) );
    }

    // RKM codes: "4k7" -> "4.7k", "2R2" -> "2.2", "10R" -> "10", "R47" -> "0.47"
    wxRegEx rkmMid( wxS( "^([0-9]+)([RrKkMmGgTtUuNnPp])([0-9]+)$" ) );
    wxRegEx rkmTrailingR( wxS( "^([0-9]+(\\.[0-9]+)?)[Rr]$" ) );
    wxRegEx rkmLeadingR( wxS( "^[Rr]([0-9]+)$" ) );

    if( rkmMid.Matches( clean ) )
    {
        wxString intPart = rkmMid.GetMatch( clean, 1 );
        wxString letter = rkmMid.GetMatch( clean, 2 );
        wxString fracPart = rkmMid.GetMatch( clean, 3 );

        if( letter == wxS( "R" ) || letter == wxS( "r" ) )
            letter = wxEmptyString;

        clean = intPart + wxS( "." ) + fracPart + letter;
    }
    else if( rkmTrailingR.Matches( clean ) )
    {
        clean = rkmTrailingR.GetMatch( clean, 1 );
    }
    else if( rkmLeadingR.Matches( clean ) )
    {
        clean = wxS( "0." ) + rkmLeadingR.GetMatch( clean, 1 );
    }

    // Validate with the simulator grammar first: SIM_VALUE::ToDouble() asserts on unknown
    // suffixes, so it must only ever see pre-validated strings.
    std::string candidate( clean.ToStdString() );

    if( !SIM_VALUE_GRAMMAR::IsValid( candidate, SIM_VALUE::TYPE_FLOAT,
                                     SIM_VALUE_GRAMMAR::NOTATION::SI ) )
    {
        return false;
    }

    double value = SIM_VALUE::ToDouble( candidate );

    if( std::isnan( value ) || value <= 0.0 )
        return false;

    long double scaled = (long double) value * (long double) ValueScaleForType( aType );

    if( scaled < 1.0L || scaled > (long double) std::numeric_limits<int64_t>::max() )
        return false;

    *aResult = (int64_t) llroundl( scaled );
    return *aResult > 0;
}


wxString PackageFromFootprint( const wxString& aFootprint )
{
    wxString name = aFootprint.AfterLast( ':' );

    if( name.IsEmpty() )
        return wxEmptyString;

    // Split on common separators and look for a known imperial code token, so the metric
    // half of "R_0603_1608Metric" can never be mistaken for a package.
    wxString token;
    std::vector<wxString> tokens;

    for( wxUniChar c : name )
    {
        if( c == '_' || c == '-' || c == ' ' )
        {
            if( !token.IsEmpty() )
                tokens.push_back( token );

            token.clear();
        }
        else
        {
            token += c;
        }
    }

    if( !token.IsEmpty() )
        tokens.push_back( token );

    for( const wxString& tok : tokens )
    {
        for( const wxString& pkg : knownPackages() )
        {
            if( tok == pkg )
                return pkg;
        }
    }

    return wxEmptyString;
}


PASSIVE_TYPE TypeFromFootprint( const wxString& aFootprint )
{
    wxString nickname = aFootprint.BeforeFirst( ':' ).Lower();
    wxString name = aFootprint.AfterLast( ':' );

    // Networks/arrays are not single passives
    if( name.StartsWith( wxS( "R_Network" ) ) || name.StartsWith( wxS( "R_Array" ) )
            || name.StartsWith( wxS( "C_Network" ) ) || name.StartsWith( wxS( "C_Array" ) ) )
    {
        return PASSIVE_TYPE::UNKNOWN;
    }

    if( nickname.StartsWith( wxS( "resistor" ) ) )
        return PASSIVE_TYPE::RESISTOR;

    if( nickname.StartsWith( wxS( "capacitor" ) ) )
        return PASSIVE_TYPE::CAPACITOR;

    if( nickname.StartsWith( wxS( "inductor" ) ) )
        return PASSIVE_TYPE::INDUCTOR;

    if( name.StartsWith( wxS( "R_" ) ) )
        return PASSIVE_TYPE::RESISTOR;

    if( name.StartsWith( wxS( "C_" ) ) || name.StartsWith( wxS( "CP_" ) ) )
        return PASSIVE_TYPE::CAPACITOR;

    if( name.StartsWith( wxS( "L_" ) ) )
        return PASSIVE_TYPE::INDUCTOR;

    return PASSIVE_TYPE::UNKNOWN;
}


PASSIVE_TYPE TypeFromRefdesPrefix( const wxString& aPrefix )
{
    if( aPrefix == wxS( "R" ) )
        return PASSIVE_TYPE::RESISTOR;

    if( aPrefix == wxS( "C" ) )
        return PASSIVE_TYPE::CAPACITOR;

    if( aPrefix == wxS( "L" ) )
        return PASSIVE_TYPE::INDUCTOR;

    return PASSIVE_TYPE::UNKNOWN;
}


PASSIVE_TYPE TypeFromString( const wxString& aText )
{
    wxString lower = aText.Lower();
    lower.Trim( true ).Trim( false );

    if( lower == wxS( "r" ) || lower.StartsWith( wxS( "res" ) ) )
        return PASSIVE_TYPE::RESISTOR;

    if( lower == wxS( "c" ) || lower.StartsWith( wxS( "cap" ) ) )
        return PASSIVE_TYPE::CAPACITOR;

    if( lower == wxS( "l" ) || lower.StartsWith( wxS( "ind" ) ) )
        return PASSIVE_TYPE::INDUCTOR;

    return PASSIVE_TYPE::UNKNOWN;
}


wxString TypeToString( PASSIVE_TYPE aType )
{
    switch( aType )
    {
    case PASSIVE_TYPE::RESISTOR:  return wxS( "resistor" );
    case PASSIVE_TYPE::CAPACITOR: return wxS( "capacitor" );
    case PASSIVE_TYPE::INDUCTOR:  return wxS( "inductor" );
    default:                      return wxS( "unknown" );
    }
}


wxString NormalizeTolerance( const wxString& aText )
{
    wxString out;
    bool     hasDigit = false;

    for( wxUniChar c : aText )
    {
        if( ( c >= '0' && c <= '9' ) || c == '.' || c == '%' )
        {
            out += c;
            hasDigit |= ( c >= '0' && c <= '9' );
        }
        // Everything else (plus-minus signs, spaces, mojibake, "+/-") is dropped
    }

    if( !hasDigit )
        return wxEmptyString;

    if( !out.EndsWith( wxS( "%" ) ) )
        out += wxS( "%" );

    return out;
}


nlohmann::json TableToJson( const PASSIVE_TABLE& aTable )
{
    nlohmann::json records = nlohmann::json::array();

    for( const PASSIVE_RECORD& record : aTable.records )
    {
        nlohmann::json fields = nlohmann::json::object();

        for( const auto& [name, value] : record.fields )
            fields[std::string( name.utf8_str() )] = std::string( value.utf8_str() );

        records.push_back( { { "value", record.valueScaled },
                             { "package", std::string( record.package.utf8_str() ) },
                             { "tolerance", std::string( record.tolerance.utf8_str() ) },
                             { "fields", std::move( fields ) } } );
    }

    nlohmann::json shown = nlohmann::json::array();

    for( const wxString& field : aTable.shownBomFields )
        shown.push_back( std::string( field.utf8_str() ) );

    return { { "version", 1 },
             { "id", std::string( aTable.id.utf8_str() ) },
             { "name", std::string( aTable.name.utf8_str() ) },
             { "source_file", std::string( aTable.sourceFile.utf8_str() ) },
             { "imported_at", std::string( aTable.importedAt.utf8_str() ) },
             { "component_type", std::string( TypeToString( aTable.type ).utf8_str() ) },
             { "enabled", aTable.enabled },
             { "order", aTable.order },
             { "default_tolerance", std::string( aTable.defaultTolerance.utf8_str() ) },
             { "shown_fields", std::move( shown ) },
             { "records", std::move( records ) } };
}


bool TableFromJson( const nlohmann::json& aJson, PASSIVE_TABLE& aTable )
{
    aTable = PASSIVE_TABLE();

    if( !aJson.is_object() )
        return false;

    // Total/tolerant reader: every field has a default, malformed records are skipped,
    // so one bad entry can never take the rest of the table (or the app) down.
    auto getString = []( const nlohmann::json& j, const char* key ) -> wxString
    {
        if( j.contains( key ) && j.at( key ).is_string() )
            return wxString::FromUTF8( j.at( key ).get<std::string>() );

        return wxEmptyString;
    };

    if( aJson.contains( "version" ) && aJson.at( "version" ).is_number_integer()
            && aJson.at( "version" ).get<int>() > 1 )
    {
        return false;
    }

    aTable.id = getString( aJson, "id" );
    aTable.name = getString( aJson, "name" );
    aTable.sourceFile = getString( aJson, "source_file" );
    aTable.importedAt = getString( aJson, "imported_at" );
    aTable.type = TypeFromString( getString( aJson, "component_type" ) );
    aTable.defaultTolerance = NormalizeTolerance( getString( aJson, "default_tolerance" ) );

    if( aTable.type == PASSIVE_TYPE::UNKNOWN )
        aTable.type = PASSIVE_TYPE::RESISTOR;

    if( aJson.contains( "enabled" ) && aJson.at( "enabled" ).is_boolean() )
        aTable.enabled = aJson.at( "enabled" ).get<bool>();

    if( aJson.contains( "order" ) && aJson.at( "order" ).is_number_integer() )
        aTable.order = aJson.at( "order" ).get<int>();

    if( aJson.contains( "shown_fields" ) && aJson.at( "shown_fields" ).is_array() )
    {
        for( const nlohmann::json& field : aJson.at( "shown_fields" ) )
        {
            if( field.is_string() )
                aTable.shownBomFields.emplace_back( wxString::FromUTF8( field.get<std::string>() ) );
        }
    }

    if( aJson.contains( "records" ) && aJson.at( "records" ).is_array() )
    {
        for( const nlohmann::json& entry : aJson.at( "records" ) )
        {
            if( !entry.is_object() )
                continue;

            PASSIVE_RECORD record;

            if( entry.contains( "value" ) && entry.at( "value" ).is_number_integer() )
                record.valueScaled = entry.at( "value" ).get<int64_t>();

            if( record.valueScaled <= 0 )
                continue;

            record.package = getString( entry, "package" ).Upper();
            record.tolerance = NormalizeTolerance( getString( entry, "tolerance" ) );

            if( entry.contains( "fields" ) && entry.at( "fields" ).is_object() )
            {
                for( const auto& [name, value] : entry.at( "fields" ).items() )
                {
                    if( value.is_string() )
                    {
                        record.fields[wxString::FromUTF8( name )] =
                                wxString::FromUTF8( value.get<std::string>() );
                    }
                }
            }

            if( !record.fields.empty() )
                aTable.records.push_back( std::move( record ) );
        }
    }

    return true;
}

} // namespace PASSIVE_PARTS
