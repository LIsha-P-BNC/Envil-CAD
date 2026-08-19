/*
 * This program source code file is part of KiCad, a free EDA CAD application.
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

#include <anvil_auth/anvil_auth_config.h>

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

/*
 * One standard `.env` file beside the executable — see the header for the format.  The
 * .env Key=Value / '#'-comment syntax is exactly what wxFileConfig parses, so no custom
 * parser is needed.
 */
namespace
{
const wxChar* KEY_API_BASE     = wxT( "ApiBase" );
const wxChar* KEY_API_KEY      = wxT( "ApiKey" );
const wxChar* KEY_PROJECT_ID   = wxT( "ProjectId" );
const wxChar* KEY_SESSION_DAYS = wxT( "SessionDays" );


wxString readValue( const wxChar* aKey )
{
    const wxString path = ANVIL_AUTH_CONFIG::ConfigFilePath();

    if( path.IsEmpty() || !wxFileName( path ).FileExists() )
        return wxEmptyString;

    wxFileConfig cfg( wxS( "" ), wxS( "" ), path );

    wxString value;
    cfg.Read( aKey, &value, wxEmptyString );

    return value.Trim().Trim( false );
}

} // namespace


wxString ANVIL_AUTH_CONFIG::ConfigFilePath()
{
    const wxString exeDir = wxFileName( wxStandardPaths::Get().GetExecutablePath() ).GetPath();

    if( exeDir.IsEmpty() )
        return wxEmptyString;

    return wxFileName( exeDir, wxS( ".env" ) ).GetFullPath();
}


wxString ANVIL_AUTH_CONFIG::ApiKey()
{
    return readValue( KEY_API_KEY );
}


wxString ANVIL_AUTH_CONFIG::ProjectId()
{
    return readValue( KEY_PROJECT_ID );
}


wxString ANVIL_AUTH_CONFIG::ApiBase()
{
    wxString base = readValue( KEY_API_BASE );

    // Normalise: bare-path endpoints in anvil_api_defs.h all start with '/'.
    while( base.EndsWith( wxS( "/" ) ) )
        base.RemoveLast();

    return base;
}


int ANVIL_AUTH_CONFIG::SessionDays()
{
    long days = 0;

    if( readValue( KEY_SESSION_DAYS ).ToLong( &days ) && days > 0 )
        return static_cast<int>( days );

    return 30;
}


bool ANVIL_AUTH_CONFIG::IsConfigured()
{
    return !ApiKey().IsEmpty();
}
