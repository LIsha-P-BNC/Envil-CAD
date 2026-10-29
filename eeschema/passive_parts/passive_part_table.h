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

#ifndef PASSIVE_PART_TABLE_H
#define PASSIVE_PART_TABLE_H

#include <cstdint>
#include <map>
#include <vector>

#include <wx/string.h>

#include <nlohmann/json_fwd.hpp>

/**
 * Customer-imported passive component parts lists for BOM auto-fill.
 *
 * A customer imports their company's approved passive parts list (CSV) through
 * Preferences > Schematic Editor > Passive Parts.  Each list maps a lookup key of
 * { component type, package size, value, tolerance } to a set of BOM field values
 * (Manufacturer, MPN, Description, ...).  The BOM data model consults the lists to
 * fill cells that the schematic symbols leave empty — display/export only, never
 * persisted into the schematic.
 */
namespace PASSIVE_PARTS
{

enum class PASSIVE_TYPE
{
    UNKNOWN = 0,
    RESISTOR,
    CAPACITOR,
    INDUCTOR
};

/// One normalized row of an imported parts list.
struct PASSIVE_RECORD
{
    /// Component value in integer base units scaled per type (see ValueScaleForType):
    /// milliohms for resistors, femtofarads for capacitors, picohenries for inductors.
    /// Integer so exact-match lookup never depends on floating point equality.
    int64_t  valueScaled = 0;

    wxString package;       ///< Imperial package code, e.g. "0603"; normalized uppercase
    wxString tolerance;     ///< Normalized tolerance, e.g. "5%"; empty if the list has none

    /// BOM field name -> value, e.g. { "Manufacturer": "Yageo", "MPN": "RC0603JR-0710KL" }.
    /// Field names are whatever the import mapping chose; matching against BOM columns is
    /// done with normalized names plus the Manufacturer/MPN alias sets.
    std::map<wxString, wxString> fields;
};

/// An imported parts list plus its metadata.  Serialized one-per-file under the user
/// settings dir (see PASSIVE_TABLE_STORE); deliberately NOT in eeschema.json so a bad
/// record can never take application settings down with it and "reset settings" can
/// never destroy a customer's parts library.
struct PASSIVE_TABLE
{
    wxString     id;                ///< Stable identity; also the payload file basename
    wxString     name;              ///< Display name
    wxString     sourceFile;        ///< Original CSV path (informational)
    wxString     importedAt;        ///< ISO timestamp (informational)
    PASSIVE_TYPE type = PASSIVE_TYPE::RESISTOR;
    bool         enabled = true;
    int          order = 0;         ///< Lookup precedence across tables (lower first)
    wxString     defaultTolerance;  ///< Used when a symbol has no Tolerance field

    /// BOM fields this list wants force-created as columns (the "Show in BOM" ticks).
    std::vector<wxString> shownBomFields;

    std::vector<PASSIVE_RECORD> records;
};


/// Integer scale factor used for PASSIVE_RECORD::valueScaled, per component type.
int64_t ValueScaleForType( PASSIVE_TYPE aType );

/**
 * Parse a passive value string into scaled integer base units.
 *
 * Handles SI suffixes ("10k", "4.7M"), RKM codes ("4k7", "2R2", "R47"), unit words and
 * symbols ("10 kOhms", "100nF", "10k<ohm sign>"), unicode micro signs, and tolerates
 * mojibake by discarding non-ASCII characters after unit stripping.
 *
 * @return true and set aResult on success; false for empty/unparseable input.
 */
bool NormalizePassiveValue( const wxString& aText, PASSIVE_TYPE aType, int64_t* aResult );

/**
 * Extract the imperial package code from a footprint field value
 * ("Resistor_SMD:R_0603_1608Metric" -> "0603").  Empty string if none recognized.
 */
wxString PackageFromFootprint( const wxString& aFootprint );

/**
 * Infer the passive type from a footprint field value via the library nickname
 * ("Resistor_SMD:..." -> RESISTOR) or the footprint name prefix (R_/C_/L_).
 * Resistor networks/arrays return UNKNOWN.
 */
PASSIVE_TYPE TypeFromFootprint( const wxString& aFootprint );

/**
 * Infer the passive type from a reference designator prefix: exactly "R", "C" or "L".
 * "RN", "RV", etc. return UNKNOWN — networks and variable parts are not single passives.
 */
PASSIVE_TYPE TypeFromRefdesPrefix( const wxString& aPrefix );

/// Parse a type name ("resistor", "R", "cap", ...) case-insensitively.
PASSIVE_TYPE TypeFromString( const wxString& aText );

/// Canonical serialized name for a type ("resistor", "capacitor", "inductor").
wxString TypeToString( PASSIVE_TYPE aType );

/// Normalize a tolerance string: strip plus-minus signs/spaces/non-ASCII, ensure a
/// trailing '%' when numeric.  "+/-5 %" -> "5%".  Empty if no digits present.
wxString NormalizeTolerance( const wxString& aText );

/// Serialize a table (records included) to JSON.
nlohmann::json TableToJson( const PASSIVE_TABLE& aTable );

/**
 * Deserialize a table from JSON.  Total/tolerant: missing keys default, malformed
 * records are skipped.  Returns false only if the JSON is not an object or carries
 * an incompatible version.
 */
bool TableFromJson( const nlohmann::json& aJson, PASSIVE_TABLE& aTable );

} // namespace PASSIVE_PARTS

#endif // PASSIVE_PART_TABLE_H
