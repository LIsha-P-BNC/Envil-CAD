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

#include <qa_utils/wx_utils/unit_test_utils.h>

#include <wx/arrstr.h>

#include <json_common.h>

#include <passive_parts/passive_part_table.h>

using namespace PASSIVE_PARTS;


BOOST_AUTO_TEST_SUITE( PassivePartTable )


struct VALUE_CASE
{
    std::string  input;
    PASSIVE_TYPE type;
    bool         valid;
    int64_t      expected; // scaled units (milliohm / femtofarad / picohenry)
};


BOOST_AUTO_TEST_CASE( ValueParsing )
{
    // clang-format off
    const std::vector<VALUE_CASE> cases = {
        // Plain SI notation
        { "10k",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 10000000 ) },
        { "10K",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 10000000 ) },
        { "10000",    PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 10000000 ) },
        { "4.7k",     PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 4700000 ) },
        { "1M",       PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 1000000000 ) },
        { "22M",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 22000000000 ) },
        { "1",        PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 1000 ) },

        // Unit words and symbols
        { "10 kOhms", PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 10000000 ) },
        { "1 Ohms",   PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 1000 ) },
        { "10 ohm",   PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 10000 ) },

        // RKM codes
        { "4k7",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 4700000 ) },
        { "2R2",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 2200 ) },
        { "10R",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 10000 ) },
        { "R47",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 470 ) },
        { "1M2",      PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 1200000000 ) },

        // Thousands separators
        { "3,300",    PASSIVE_TYPE::RESISTOR,  true,  INT64_C( 3300000 ) },

        // Capacitors (femtofarads)
        { "100nF",    PASSIVE_TYPE::CAPACITOR, true,  INT64_C( 100000000 ) },
        { "4.7uF",    PASSIVE_TYPE::CAPACITOR, true,  INT64_C( 4700000000 ) },
        { "10pF",     PASSIVE_TYPE::CAPACITOR, true,  INT64_C( 10000 ) },
        { "4u7",      PASSIVE_TYPE::CAPACITOR, true,  INT64_C( 4700000000 ) },

        // Inductors (picohenries)
        { "10uH",     PASSIVE_TYPE::INDUCTOR,  true,  INT64_C( 10000000 ) },

        // Invalid input
        { "",         PASSIVE_TYPE::RESISTOR,  false, 0 },
        { "~",        PASSIVE_TYPE::RESISTOR,  false, 0 },
        { "abc",      PASSIVE_TYPE::RESISTOR,  false, 0 },
        { "1 - 1.0 R", PASSIVE_TYPE::RESISTOR, false, 0 },
        { "316 mV",   PASSIVE_TYPE::RESISTOR,  false, 0 },
        { "DNP",      PASSIVE_TYPE::RESISTOR,  false, 0 },
    };
    // clang-format on

    for( const VALUE_CASE& testCase : cases )
    {
        BOOST_TEST_INFO_SCOPE( testCase.input );

        int64_t result = 0;
        bool    ok = NormalizePassiveValue( wxString::FromUTF8( testCase.input ),
                                            testCase.type, &result );

        BOOST_CHECK_EQUAL( ok, testCase.valid );

        if( testCase.valid && ok )
            BOOST_CHECK_EQUAL( result, testCase.expected );
    }

    // Unicode forms built from explicit UTF-8 bytes: "10k<omega>", micro sign capacitance
    int64_t result = 0;

    BOOST_CHECK( NormalizePassiveValue( wxString::FromUTF8( "10k\xCE\xA9" ),
                                        PASSIVE_TYPE::RESISTOR, &result ) );
    BOOST_CHECK_EQUAL( result, INT64_C( 10000000 ) );

    // Mojibake (UTF-8 omega read as CP1252 and re-encoded) must still parse
    BOOST_CHECK( NormalizePassiveValue( wxString::FromUTF8( "10k\xC3\x8E\xC2\xA9" ),
                                        PASSIVE_TYPE::RESISTOR, &result ) );
    BOOST_CHECK_EQUAL( result, INT64_C( 10000000 ) );

    // U+00B5 micro sign as the SI prefix
    BOOST_CHECK( NormalizePassiveValue( wxString::FromUTF8( "4.7\xC2\xB5" "F" ),
                                        PASSIVE_TYPE::CAPACITOR, &result ) );
    BOOST_CHECK_EQUAL( result, INT64_C( 4700000000 ) );
}


BOOST_AUTO_TEST_CASE( PackageParsing )
{
    BOOST_CHECK_EQUAL( PackageFromFootprint( "Resistor_SMD:R_0603_1608Metric" ), "0603" );
    BOOST_CHECK_EQUAL( PackageFromFootprint( "Capacitor_SMD:C_0402_1005Metric" ), "0402" );
    BOOST_CHECK_EQUAL( PackageFromFootprint( "Resistor_SMD:R_2512_6332Metric" ), "2512" );
    BOOST_CHECK_EQUAL( PackageFromFootprint( "R_0805_2012Metric" ), "0805" );
    BOOST_CHECK_EQUAL( PackageFromFootprint( "" ), "" );
    BOOST_CHECK_EQUAL( PackageFromFootprint( "Package_TO_SOT_SMD:SOT-23" ), "" );

    // The metric half must never be mistaken for a package
    BOOST_CHECK_EQUAL( PackageFromFootprint( "Resistor_SMD:R_0603_1608Metric" ), "0603" );
}


BOOST_AUTO_TEST_CASE( TypeDetection )
{
    BOOST_CHECK( TypeFromFootprint( "Resistor_SMD:R_0603_1608Metric" )
                 == PASSIVE_TYPE::RESISTOR );
    BOOST_CHECK( TypeFromFootprint( "Capacitor_SMD:C_0603_1608Metric" )
                 == PASSIVE_TYPE::CAPACITOR );
    BOOST_CHECK( TypeFromFootprint( "Inductor_SMD:L_0603_1608Metric" )
                 == PASSIVE_TYPE::INDUCTOR );

    // Networks are not single passives
    BOOST_CHECK( TypeFromFootprint( "Resistor_SMD:R_Network03_0603" ) == PASSIVE_TYPE::UNKNOWN );
    BOOST_CHECK( TypeFromFootprint( "Resistor_SMD:R_Array_Convex_4x0603" )
                 == PASSIVE_TYPE::UNKNOWN );

    BOOST_CHECK( TypeFromRefdesPrefix( "R" ) == PASSIVE_TYPE::RESISTOR );
    BOOST_CHECK( TypeFromRefdesPrefix( "C" ) == PASSIVE_TYPE::CAPACITOR );
    BOOST_CHECK( TypeFromRefdesPrefix( "L" ) == PASSIVE_TYPE::INDUCTOR );
    BOOST_CHECK( TypeFromRefdesPrefix( "RN" ) == PASSIVE_TYPE::UNKNOWN );
    BOOST_CHECK( TypeFromRefdesPrefix( "RV" ) == PASSIVE_TYPE::UNKNOWN );
    BOOST_CHECK( TypeFromRefdesPrefix( "U" ) == PASSIVE_TYPE::UNKNOWN );
}


BOOST_AUTO_TEST_CASE( ToleranceNormalization )
{
    // Plus-minus sign as UTF-8 bytes
    BOOST_CHECK_EQUAL( NormalizeTolerance( wxString::FromUTF8( "\xC2\xB1" "5%" ) ), "5%" );
    BOOST_CHECK_EQUAL( NormalizeTolerance( "5%" ), "5%" );
    BOOST_CHECK_EQUAL( NormalizeTolerance( "5" ), "5%" );
    BOOST_CHECK_EQUAL( NormalizeTolerance( "+/- 0.1 %" ), "0.1%" );
    BOOST_CHECK_EQUAL( NormalizeTolerance( "J" ), "" );
    BOOST_CHECK_EQUAL( NormalizeTolerance( "" ), "" );
}


BOOST_AUTO_TEST_CASE( JsonRoundTrip )
{
    PASSIVE_TABLE table;
    table.id = "test-id";
    table.name = "Test 0603 Resistors";
    table.sourceFile = "C:/somewhere/list.csv";
    table.importedAt = "2026-10-16T10:00:00";
    table.type = PASSIVE_TYPE::RESISTOR;
    table.enabled = true;
    table.order = 3;
    table.defaultTolerance = "5%";
    table.shownBomFields = { "Manufacturer", "MPN", "Description" };

    PASSIVE_RECORD record;
    record.valueScaled = INT64_C( 10000000 );
    record.package = "0603";
    record.tolerance = "5%";
    record.fields["Manufacturer"] = "Yageo";
    record.fields["MPN"] = "RC0603JR-0710KL";
    record.fields["Description"] = "RES SMD 10K OHM 5% 1/10W 0603";
    table.records.push_back( record );

    record.valueScaled = INT64_C( 4700000 );
    record.fields["MPN"] = "RC0603JR-074K7L";
    table.records.push_back( record );

    nlohmann::json js = TableToJson( table );

    PASSIVE_TABLE readBack;
    BOOST_REQUIRE( TableFromJson( js, readBack ) );

    BOOST_CHECK_EQUAL( readBack.id, table.id );
    BOOST_CHECK_EQUAL( readBack.name, table.name );
    BOOST_CHECK( readBack.type == PASSIVE_TYPE::RESISTOR );
    BOOST_CHECK_EQUAL( readBack.enabled, true );
    BOOST_CHECK_EQUAL( readBack.order, 3 );
    BOOST_CHECK_EQUAL( readBack.defaultTolerance, "5%" );
    BOOST_REQUIRE_EQUAL( readBack.shownBomFields.size(), 3 );
    BOOST_REQUIRE_EQUAL( readBack.records.size(), 2 );
    BOOST_CHECK_EQUAL( readBack.records[0].valueScaled, INT64_C( 10000000 ) );
    BOOST_CHECK_EQUAL( readBack.records[0].package, "0603" );
    BOOST_CHECK_EQUAL( readBack.records[0].fields.at( "MPN" ), "RC0603JR-0710KL" );
    BOOST_CHECK_EQUAL( readBack.records[1].fields.at( "MPN" ), "RC0603JR-074K7L" );
}


BOOST_AUTO_TEST_CASE( JsonTolerantReader )
{
    // Not an object
    PASSIVE_TABLE table;
    BOOST_CHECK( !TableFromJson( nlohmann::json::array(), table ) );

    // Future version is rejected
    nlohmann::json future = { { "version", 2 } };
    BOOST_CHECK( !TableFromJson( future, table ) );

    // Malformed records are skipped, good ones survive
    nlohmann::json mixed = {
        { "version", 1 },
        { "name", "mixed" },
        { "component_type", "resistor" },
        { "records",
          { { { "value", 1000 }, { "package", "0603" },
              { "fields", { { "MPN", "GOOD" } } } },
            { { "value", "not-a-number" }, { "package", "0603" },
              { "fields", { { "MPN", "BAD" } } } },
            { { "value", 2000 }, { "package", "0603" }, { "fields", nlohmann::json::object() } },
            "not-an-object" } }
    };

    BOOST_REQUIRE( TableFromJson( mixed, table ) );
    BOOST_REQUIRE_EQUAL( table.records.size(), 1 );
    BOOST_CHECK_EQUAL( table.records[0].fields.at( "MPN" ), "GOOD" );
}


BOOST_AUTO_TEST_SUITE_END()
