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

#include <activity_logger.h>

#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/log.h>

#include <paths.h>


ACTIVITY_LOGGER& ACTIVITY_LOGGER::Get()
{
    static ACTIVITY_LOGGER logger;
    return logger;
}


bool ACTIVITY_LOGGER::appendLine( const wxString& aPath, const wxString& aLine )
{
    // wxFFile reports open/write errors through wxLog; keep those quiet — a failed log
    // write must never surface to the user
    wxLogNull quiet;
    wxFFile   file;

    if( !file.Open( aPath, wxS( "a" ) ) )
        return false;

    return file.Write( aLine );
}


wxString ACTIVITY_LOGGER::jsonEscape( const wxString& aStr )
{
    wxString out;
    out.reserve( aStr.length() + 8 );

    for( wxUniChar c : aStr )
    {
        switch( static_cast<unsigned int>( c ) )
        {
        case '"':  out << wxS( "\\\"" ); break;
        case '\\': out << wxS( "\\\\" ); break;
        case '\n': out << wxS( "\\n" );  break;
        case '\r': out << wxS( "\\r" );  break;
        case '\t': out << wxS( "\\t" );  break;
        default:
            if( static_cast<unsigned int>( c ) < 0x20 )
                out << wxString::Format( wxS( "\\u%04x" ), static_cast<unsigned int>( c ) );
            else
                out << c;

            break;
        }
    }

    return out;
}


void ACTIVITY_LOGGER::LogCommit( const wxString& aProjectDir, const wxString& aProjectName,
                                 const wxString& aEditor, const wxString& aMessage, bool aFromAPI,
                                 const std::vector<wxString>& aItems )
{
    wxString line = wxString::Format(
            wxS( "{\"ts\":\"%s\",\"project\":\"%s\",\"editor\":\"%s\",\"action\":\"%s\","
                 "\"origin\":\"%s\",\"items\":[" ),
            wxDateTime::Now().Format( wxS( "%Y-%m-%dT%H:%M:%S" ) ),
            jsonEscape( aProjectName ),
            jsonEscape( aEditor ),
            jsonEscape( aMessage ),
            aFromAPI ? wxS( "api" ) : wxS( "manual" ) );

    for( size_t ii = 0; ii < aItems.size(); ++ii )
    {
        if( ii > 0 )
            line << wxS( "," );

        line << wxS( "\"" ) << jsonEscape( aItems[ii] ) << wxS( "\"" );
    }

    line << wxS( "]}\n" );

    std::lock_guard<std::mutex> lock( m_mutex );

    // Per-project log next to the project file; global fallback for project-less edits or
    // unwritable project directories (e.g. demos opened from a read-only location).
    if( !aProjectDir.IsEmpty() && !aProjectName.IsEmpty() )
    {
        wxFileName projectLog( aProjectDir, aProjectName + wxS( "-activity.log" ) );

        if( appendLine( projectLog.GetFullPath(), line ) )
            return;
    }

    wxFileName fallbackLog( PATHS::GetUserSettingsPath(), wxS( "activity.log" ) );

    if( PATHS::EnsurePathExists( fallbackLog.GetPath() ) )
        appendLine( fallbackLog.GetFullPath(), line );
}
