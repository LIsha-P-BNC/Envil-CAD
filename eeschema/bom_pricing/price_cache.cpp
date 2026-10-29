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

#include "price_cache.h"
#include "bom_pricing_defs.h"

#include <json_common.h>
#include <settings/settings_manager.h>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include <ctime>

namespace BOM_PRICING
{

namespace
{

int64_t now()
{
    return (int64_t) std::time( nullptr );
}


bool isFresh( int64_t aFetchedAt, int aTtlHours )
{
    return aFetchedAt > 0 && ( now() - aFetchedAt ) < (int64_t) aTtlHours * 3600;
}


nlohmann::json entryToJson( const CACHED_PRICE& aEntry )
{
    nlohmann::json breaks = nlohmann::json::array();

    for( const auto& [qty, price] : aEntry.qtyBreaksInr )
        breaks.push_back( { { "qty", qty }, { "price_inr", price } } );

    return { { "price_inr", aEntry.priceInr },
             { "source", std::string( aEntry.source.utf8_str() ) },
             { "url", std::string( aEntry.url.utf8_str() ) },
             { "qty_breaks", breaks },
             { "not_found", aEntry.notFound },
             { "fetched_at", aEntry.fetchedAt } };
}


CACHED_PRICE entryFromJson( const nlohmann::json& aJson )
{
    CACHED_PRICE entry;

    entry.priceInr = aJson.value( "price_inr", 0.0 );
    entry.source = wxString::FromUTF8( aJson.value( "source", std::string() ) );
    entry.url = wxString::FromUTF8( aJson.value( "url", std::string() ) );
    entry.notFound = aJson.value( "not_found", false );
    entry.fetchedAt = aJson.value( "fetched_at", (int64_t) 0 );

    auto it = aJson.find( "qty_breaks" );

    if( it != aJson.end() && it->is_array() )
    {
        for( const nlohmann::json& b : *it )
        {
            if( b.is_object() )
                entry.qtyBreaksInr.emplace_back( b.value( "qty", 1 ), b.value( "price_inr", 0.0 ) );
        }
    }

    return entry;
}


/// Best-effort parse of the cache file; any corruption just means starting empty.
nlohmann::json readCacheFile()
{
    if( !wxFileExists( PRICE_CACHE::CacheFilePath() ) )
        return nlohmann::json::object();

    wxFFile file( PRICE_CACHE::CacheFilePath(), wxS( "rb" ) );

    if( !file.IsOpened() )
        return nlohmann::json::object();

    wxString contents;

    if( !file.ReadAll( &contents ) )
        return nlohmann::json::object();

    nlohmann::json js = nlohmann::json::parse( std::string( contents.utf8_str() ), nullptr,
                                               /* throw */ false );

    return js.is_object() ? js : nlohmann::json::object();
}

} // namespace


wxString PRICE_CACHE::CacheFilePath()
{
    return SETTINGS_MANAGER::GetUserSettingsPath() + wxFileName::GetPathSeparator()
           + wxS( "bom_price_cache.json" );
}


PRICE_CACHE::PRICE_CACHE()
{
    load();
}


void PRICE_CACHE::load()
{
    try
    {
        nlohmann::json js = readCacheFile();

        auto rateIt = js.find( "usd_inr" );

        if( rateIt != js.end() && rateIt->is_object() )
        {
            m_usdInr = rateIt->value( "rate", 0.0 );
            m_usdInrFetchedAt = rateIt->value( "fetched_at", (int64_t) 0 );
        }

        auto partsIt = js.find( "parts" );

        if( partsIt != js.end() && partsIt->is_object() )
        {
            for( const auto& [mpn, entry] : partsIt->items() )
            {
                if( entry.is_object() )
                    m_parts[wxString::FromUTF8( mpn )] = entryFromJson( entry );
            }
        }
    }
    catch( ... )
    {
        m_parts.clear();
    }
}


std::optional<CACHED_PRICE> PRICE_CACHE::Get( const wxString& aNormalizedMpn, int aTtlHours ) const
{
    auto it = m_parts.find( aNormalizedMpn );

    if( it == m_parts.end() )
        return std::nullopt;

    int ttl = it->second.notFound ? NEGATIVE_CACHE_TTL_HOURS : aTtlHours;

    if( !isFresh( it->second.fetchedAt, ttl ) )
        return std::nullopt;

    return it->second;
}


void PRICE_CACHE::Put( const wxString& aNormalizedMpn, const CACHED_PRICE& aEntry )
{
    CACHED_PRICE entry = aEntry;

    if( entry.fetchedAt == 0 )
        entry.fetchedAt = now();

    m_parts[aNormalizedMpn] = entry;
    m_dirty[aNormalizedMpn] = entry;
}


std::optional<double> PRICE_CACHE::GetFreshUsdInr() const
{
    if( m_usdInr > 0.0 && isFresh( m_usdInrFetchedAt, FX_CACHE_TTL_HOURS ) )
        return m_usdInr;

    return std::nullopt;
}


void PRICE_CACHE::PutUsdInr( double aRate )
{
    m_usdInr = aRate;
    m_usdInrFetchedAt = now();
    m_usdInrDirty = true;
}


void PRICE_CACHE::Flush()
{
    if( m_dirty.empty() && !m_usdInrDirty )
        return;

    try
    {
        // Re-read and merge: a concurrent writer (dialog vs. CLI) may have added entries since
        // we loaded.  Overlaying only this session's new entries onto a fresh base means the
        // last rename wins over an identical merged base — nothing is lost either way.
        nlohmann::json js = readCacheFile();

        js["version"] = 1;

        if( !js.contains( "parts" ) || !js["parts"].is_object() )
            js["parts"] = nlohmann::json::object();

        for( const auto& [mpn, entry] : m_dirty )
            js["parts"][std::string( mpn.utf8_str() )] = entryToJson( entry );

        if( m_usdInrDirty )
            js["usd_inr"] = { { "rate", m_usdInr }, { "fetched_at", m_usdInrFetchedAt } };

        wxString finalPath = CacheFilePath();
        wxString tmpPath = wxString::Format( wxS( "%s.tmp-%lu" ), finalPath,
                                             (unsigned long) wxGetProcessId() );

        {
            wxFFile file( tmpPath, wxS( "wb" ) );

            if( !file.IsOpened() )
                return;

            std::string dump = js.dump( 2 );
            file.Write( dump.data(), dump.size() );
        }

        if( !wxRenameFile( tmpPath, finalPath, /* overwrite */ true ) )
            wxRemoveFile( tmpPath );

        m_dirty.clear();
        m_usdInrDirty = false;
    }
    catch( ... )
    {
        // A failed flush only costs a re-fetch next session.
    }
}

} // namespace BOM_PRICING
