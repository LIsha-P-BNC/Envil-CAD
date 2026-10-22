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

#include "passive_table_store.h"

#include <algorithm>

#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include <json_common.h>
#include <kiid.h>
#include <settings/settings_manager.h>

namespace PASSIVE_PARTS
{

namespace
{

nlohmann::json readJsonFile( const wxString& aPath )
{
    if( !wxFileExists( aPath ) )
        return nlohmann::json();

    wxFFile file( aPath, wxS( "rb" ) );

    if( !file.IsOpened() )
        return nlohmann::json();

    wxString contents;

    if( !file.ReadAll( &contents, wxMBConvUTF8() ) )
        return nlohmann::json();

    // Tolerant parse: a corrupt payload degrades to "no table", never an exception
    nlohmann::json js =
            nlohmann::json::parse( std::string( contents.utf8_str() ), nullptr, false );

    return js.is_discarded() ? nlohmann::json() : js;
}

} // namespace


PASSIVE_TABLE_STORE& PASSIVE_TABLE_STORE::Get()
{
    static PASSIVE_TABLE_STORE store;
    return store;
}


wxString PASSIVE_TABLE_STORE::StoreDirPath()
{
    wxFileName path;

    path.AssignDir( SETTINGS_MANAGER::GetUserSettingsPath() );
    path.AppendDir( wxS( "passives" ) );

    if( !path.DirExists() )
        wxMkdir( path.GetPath() );

    return path.GetPath();
}


void PASSIVE_TABLE_STORE::ensureLoaded()
{
    if( !m_loaded )
        loadLocked();
}


void PASSIVE_TABLE_STORE::loadLocked()
{
    m_tables.clear();
    m_loaded = true;

    wxDir dir( StoreDirPath() );

    if( !dir.IsOpened() )
        return;

    wxString file;
    bool     more = dir.GetFirst( &file, wxS( "*.json" ), wxDIR_FILES );

    while( more )
    {
        wxFileName fullPath( StoreDirPath(), file );

        PASSIVE_TABLE  table;
        nlohmann::json js = readJsonFile( fullPath.GetFullPath() );

        if( !js.is_null() && TableFromJson( js, table ) )
        {
            // The filename is the identity of record; heal a missing/diverged id field
            if( table.id.IsEmpty() )
                table.id = fullPath.GetName();

            m_tables.push_back( std::move( table ) );
        }

        more = dir.GetNext( &file );
    }

    std::sort( m_tables.begin(), m_tables.end(),
               []( const PASSIVE_TABLE& a, const PASSIVE_TABLE& b )
               {
                   if( a.order != b.order )
                       return a.order < b.order;

                   return a.name.CmpNoCase( b.name ) < 0;
               } );
}


std::vector<PASSIVE_TABLE> PASSIVE_TABLE_STORE::Tables()
{
    std::lock_guard<std::mutex> lock( m_mutex );

    ensureLoaded();
    return m_tables;
}


void PASSIVE_TABLE_STORE::Reload()
{
    std::lock_guard<std::mutex> lock( m_mutex );

    m_loaded = false;
    m_tables.clear();
}


bool PASSIVE_TABLE_STORE::HasEnabledTables()
{
    std::lock_guard<std::mutex> lock( m_mutex );

    ensureLoaded();

    for( const PASSIVE_TABLE& table : m_tables )
    {
        if( table.enabled && !table.records.empty() )
            return true;
    }

    return false;
}


bool PASSIVE_TABLE_STORE::Save( PASSIVE_TABLE& aTable )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    if( aTable.id.IsEmpty() )
        aTable.id = KIID().AsString();

    wxFileName fileName( StoreDirPath(), aTable.id, wxS( "json" ) );

    wxString finalPath = fileName.GetFullPath();
    wxString tmpPath = wxString::Format( wxS( "%s.tmp-%lu" ), finalPath,
                                         (unsigned long) wxGetProcessId() );

    bool ok = false;

    {
        wxFFile file( tmpPath, wxS( "wb" ) );

        if( file.IsOpened() )
        {
            std::string payload = TableToJson( aTable ).dump( 2 );
            ok = file.Write( payload.data(), payload.size() ) == payload.size();
        }
    }

    if( ok )
        ok = wxRenameFile( tmpPath, finalPath, /* overwrite */ true );

    if( !ok && wxFileExists( tmpPath ) )
        wxRemoveFile( tmpPath );

    m_loaded = false;
    m_tables.clear();

    return ok;
}


bool PASSIVE_TABLE_STORE::Remove( const wxString& aId )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    if( aId.IsEmpty() )
        return false;

    wxFileName fileName( StoreDirPath(), aId, wxS( "json" ) );

    bool ok = !wxFileExists( fileName.GetFullPath() ) || wxRemoveFile( fileName.GetFullPath() );

    m_loaded = false;
    m_tables.clear();

    return ok;
}


const PASSIVE_RECORD* PASSIVE_TABLE_STORE::Find( PASSIVE_TYPE aType, const wxString& aPackage,
                                                 int64_t aValueScaled,
                                                 const wxString& aSymbolTolerance,
                                                 const wxString& aFallbackTolerance )
{
    std::lock_guard<std::mutex> lock( m_mutex );

    ensureLoaded();

    if( aType == PASSIVE_TYPE::UNKNOWN || aPackage.IsEmpty() || aValueScaled <= 0 )
        return nullptr;

    wxString package = aPackage.Upper();
    wxString symbolTolerance = NormalizeTolerance( aSymbolTolerance );

    struct MATCH
    {
        const PASSIVE_RECORD* record;
        const PASSIVE_TABLE*  table;
    };

    std::vector<MATCH> candidates;

    for( const PASSIVE_TABLE& table : m_tables )
    {
        if( !table.enabled || table.type != aType )
            continue;

        for( const PASSIVE_RECORD& record : table.records )
        {
            if( record.valueScaled == aValueScaled && record.package == package )
                candidates.push_back( { &record, &table } );
        }
    }

    if( candidates.empty() )
        return nullptr;

    // Symbol states its tolerance: only an exact match may win
    if( !symbolTolerance.IsEmpty() )
    {
        for( const MATCH& candidate : candidates )
        {
            if( candidate.record->tolerance == symbolTolerance )
                return candidate.record;
        }

        return nullptr;
    }

    // No tolerance on the symbol: a single candidate wins outright
    if( candidates.size() == 1 )
        return candidates.front().record;

    // Several candidates: the default tolerance class decides; otherwise nothing does.
    // A wrong MPN in a manufacturing BOM is worse than an empty cell.
    wxString fallback = NormalizeTolerance( aFallbackTolerance );

    for( const MATCH& candidate : candidates )
    {
        wxString preferred = candidate.table->defaultTolerance.IsEmpty()
                                     ? fallback
                                     : candidate.table->defaultTolerance;

        if( !preferred.IsEmpty() && candidate.record->tolerance == preferred )
            return candidate.record;
    }

    return nullptr;
}


std::vector<wxString> PASSIVE_TABLE_STORE::ShownBomFields()
{
    std::lock_guard<std::mutex> lock( m_mutex );

    ensureLoaded();

    std::vector<wxString> fields;

    auto normalized = []( const wxString& aName )
    {
        wxString out;

        for( wxUniChar c : aName.Lower() )
        {
            if( c != ' ' && c != '.' && c != '_' && c != '-' && c != '#' )
                out += c;
        }

        return out;
    };

    for( const PASSIVE_TABLE& table : m_tables )
    {
        if( !table.enabled )
            continue;

        for( const wxString& field : table.shownBomFields )
        {
            bool duplicate = false;

            for( const wxString& existing : fields )
                duplicate |= ( normalized( existing ) == normalized( field ) );

            if( !duplicate && !field.IsEmpty() )
                fields.push_back( field );
        }
    }

    return fields;
}

} // namespace PASSIVE_PARTS
