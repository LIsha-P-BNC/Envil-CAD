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

#include <anvil_auth/anvil_auth.h>
#include <anvil_auth/anvil_api_defs.h>
#include <anvil_auth/anvil_auth_config.h>

#include <kicad_curl/kicad_curl.h>
#include <kicad_curl/kicad_curl_easy.h>
#include <kiplatform/secrets.h>

#include <build_version.h>
#include <json_common.h>

#include <cstdlib>
#include <optional>

#include <wx/datetime.h>
#include <wx/translation.h>

/*
 * SECURITY NOTES
 *  - Never log the api-key, the OTP, or the session token — not even at debug level.
 *  - TLS certificate verification stays at the curl default (ON); do not disable it.
 *  - The session blob lives only in the platform secret store (DPAPI on Windows).
 */
namespace
{
// Secret-store coordinates for the session blob.
const wxChar* SECRET_SERVICE = wxT( "org.anvil.envil-cad" );
const wxChar* SECRET_KEY     = wxT( "session" );


struct SESSION
{
    wxString  email;
    wxString  token;
    long long expires_at = 0;   // epoch seconds; 0 = no local expiry recorded
};


std::optional<SESSION> loadSession()
{
    wxString blob;

    if( !KIPLATFORM::SECRETS::GetSecret( SECRET_SERVICE, SECRET_KEY, blob ) || blob.IsEmpty() )
        return std::nullopt;

    try
    {
        nlohmann::json js = nlohmann::json::parse( blob.ToStdString() );

        SESSION session;
        session.email      = wxString::FromUTF8( js.value( "email", "" ) );
        session.token      = wxString::FromUTF8( js.value( "token", "" ) );
        session.expires_at = js.value( "expires_at", 0LL );

        if( session.token.IsEmpty() )
            return std::nullopt;

        return session;
    }
    catch( ... )
    {
        return std::nullopt;
    }
}


bool storeSession( const SESSION& aSession )
{
    nlohmann::json js = { { "email", std::string( aSession.email.utf8_str() ) },
                          { "token", std::string( aSession.token.utf8_str() ) },
                          { "expires_at", aSession.expires_at } };

    return KIPLATFORM::SECRETS::StoreSecret( SECRET_SERVICE, SECRET_KEY,
                                             wxString::FromUTF8( js.dump().c_str() ) );
}


/// Fish a string out of @a aJson by trying @a aFields at the top level, then inside the
/// conventional wrapper objects ("data" / "result").
template <size_t N>
std::string findField( const nlohmann::json& aJson, const char* const ( &aFields )[N] )
{
    auto tryLevel = [&]( const nlohmann::json& aLevel ) -> std::string
    {
        if( !aLevel.is_object() )
            return {};

        for( const char* field : aFields )
        {
            auto it = aLevel.find( field );

            if( it == aLevel.end() )
                continue;

            if( it->is_string() )
                return it->get<std::string>();

            if( it->is_number() )
                return it->dump();
        }

        return {};
    };

    if( std::string found = tryLevel( aJson ); !found.empty() )
        return found;

    for( const char* wrapper : ANVIL_API::WRAPPER_FIELDS )
    {
        auto it = aJson.find( wrapper );

        if( it != aJson.end() )
        {
            if( std::string found = tryLevel( *it ); !found.empty() )
                return found;
        }
    }

    return {};
}


/// Like findField(), but for a boolean flag; empty when the field is absent or not a bool.
std::optional<bool> findBool( const nlohmann::json& aJson, const char* aField )
{
    auto tryLevel = [&]( const nlohmann::json& aLevel ) -> std::optional<bool>
    {
        if( !aLevel.is_object() )
            return std::nullopt;

        auto it = aLevel.find( aField );

        if( it != aLevel.end() && it->is_boolean() )
            return it->get<bool>();

        return std::nullopt;
    };

    if( std::optional<bool> found = tryLevel( aJson ) )
        return found;

    for( const char* wrapper : ANVIL_API::WRAPPER_FIELDS )
    {
        auto it = aJson.find( wrapper );

        if( it != aJson.end() )
        {
            if( std::optional<bool> found = tryLevel( *it ) )
                return found;
        }
    }

    return std::nullopt;
}


/// Best-effort human-readable message from an error response body.
wxString errorFromBody( const std::string& aBody, int aStatus )
{
    try
    {
        nlohmann::json js = nlohmann::json::parse( aBody );
        std::string    msg = findField( js, ANVIL_API::ERROR_FIELDS );

        if( !msg.empty() )
            return wxString::FromUTF8( msg );
    }
    catch( ... )
    {
    }

    if( aStatus > 0 )
        return wxString::Format( _( "Server error (HTTP %d)." ), aStatus );

    return _( "Could not reach the server. Check your network connection." );
}


/**
 * POST @a aBody as JSON to @a aPath (a full URL, or a path resolved against the configured
 * ApiBase) with the configured api-key header.
 *
 * @param aResponse receives the raw response body (also on HTTP errors).
 * @param aError receives a displayable message when false is returned.
 * @return true on HTTP 2xx.
 */
bool postJson( const char* aPath, const nlohmann::json& aBody, std::string& aResponse,
               wxString& aError )
{
    if( !ANVIL_AUTH_CONFIG::IsConfigured() )
    {
        aError = _( "Server is not configured. Open Server Settings and enter the "
                    "connection details." );
        return false;
    }

    // anvil_api_defs.h may declare an endpoint as a full URL (used as-is) or as a bare
    // path (prefixed with the configured ApiBase).
    wxString url = wxString::FromUTF8( aPath );

    if( !url.StartsWith( wxS( "http" ) ) )
        url = ANVIL_AUTH_CONFIG::ApiBase() + url;

    if( !url.StartsWith( wxS( "http" ) ) )
    {
        aError = _( "Server is not configured. Open Server Settings and enter the "
                    "connection details." );
        return false;
    }

    KICAD_CURL_EASY curl;

    curl.SetHeader( "Accept", "application/json" );
    curl.SetHeader( "Content-Type", "application/json" );
    curl.SetHeader( ANVIL_API::HDR_API_KEY, ANVIL_AUTH_CONFIG::ApiKey().ToStdString() );
    curl.SetURL( url.ToUTF8().data() );
    curl.SetPostFields( aBody.dump() );
    curl.SetFollowRedirects( true );
    curl.SetConnectTimeout( 10 );

    int result = curl.Perform();

    aResponse = curl.GetBuffer();

    if( result != CURLE_OK )
    {
        aError = _( "Could not reach the server. Check your network connection." );
        return false;
    }

    int status = curl.GetResponseStatusCode();

    if( status < 200 || status >= 300 )
    {
        aError = errorFromBody( aResponse, status );
        return false;
    }

    return true;
}

} // namespace


bool ANVIL_AUTH::RequestOtp( const wxString& aEmail, wxString& aError )
{
    nlohmann::json body = { { ANVIL_API::F_EMAIL, std::string( aEmail.utf8_str() ) },
                            { ANVIL_API::F_PROJECT_ID,
                              std::string( ANVIL_AUTH_CONFIG::ProjectId().utf8_str() ) } };

    std::string response;
    return postJson( ANVIL_API::REGISTER_PATH, body, response, aError );
}


bool ANVIL_AUTH::VerifyOtp( const wxString& aEmail, const wxString& aOtp, wxString& aError )
{
    nlohmann::json body = { { ANVIL_API::F_EMAIL, std::string( aEmail.utf8_str() ) },
                            { ANVIL_API::F_OTP, std::string( aOtp.utf8_str() ) } };

    std::string response;

    if( !postJson( ANVIL_API::VERIFY_OTP_PATH, body, response, aError ) )
        return false;

    // Pull the session token out of the response.  The exact field name isn't pinned down
    // yet, so anvil_api_defs.h carries a candidate list; a success WITHOUT a recognisable
    // token is reported as an error rather than silently letting the user through.
    std::string token;
    long long   expiresAt = 0;

    try
    {
        nlohmann::json js = nlohmann::json::parse( response );

        token = findField( js, ANVIL_API::TOKEN_FIELDS );

        if( std::string expiry = findField( js, ANVIL_API::EXPIRY_FIELDS ); !expiry.empty() )
        {
            long long value = std::atoll( expiry.c_str() );
            long long now = (long long) wxDateTime::Now().GetTicks();

            // Heuristic: a small number is a relative "expires_in" (seconds from now); a
            // large one is an absolute "expires_at" epoch timestamp.
            if( value > 0 )
                expiresAt = ( value < 1000000000LL ) ? now + value : value;
        }
    }
    catch( ... )
    {
        // fall through with empty token
    }

    if( token.empty() )
    {
        aError = _( "Login succeeded but the server response carried no session token. "
                    "Please contact support." );
        return false;
    }

    if( expiresAt == 0 )
    {
        expiresAt = (long long) wxDateTime::Now().GetTicks()
                    + (long long) ANVIL_AUTH_CONFIG::SessionDays() * 24 * 3600;
    }

    SESSION session;
    session.email = aEmail;
    session.token = wxString::FromUTF8( token );
    session.expires_at = expiresAt;

    if( !storeSession( session ) )
    {
        aError = _( "Could not store the session in the system credential store." );
        return false;
    }

    return true;
}


bool ANVIL_AUTH::Logout( wxString* aError )
{
    wxString email = GetEmail();

    wxString    error;
    std::string response;
    bool        serverOk = true;

    if( !email.IsEmpty() )
    {
        nlohmann::json body = { { ANVIL_API::F_EMAIL, std::string( email.utf8_str() ) } };
        serverOk = postJson( ANVIL_API::LOGOUT_PATH, body, response, error );
    }

    // The local session goes away regardless of what the server said: the user asked to
    // sign out, and a dead server must not pin them logged in.
    WipeSession();

    if( !serverOk && aError )
        *aError = error;

    return serverOk;
}


bool ANVIL_AUTH::CheckVersion( ANVIL_VERSION_INFO& aInfo, wxString& aError )
{
    // The endpoint requires BOTH the platform and the version we are on: it answers with a
    // comparison ("update_available"), not just the newest build. Omitting version is a 400.
    wxString current = GetSemanticVersion();
    current.Replace( wxS( "~" ), wxS( "-" ) );      // make the string valid semver

    nlohmann::json body = { { ANVIL_API::F_OS_TYPE, ANVIL_API::OS_TYPE },
                            { ANVIL_API::F_VERSION, std::string( current.utf8_str() ) } };

    std::string response;

    if( !postJson( ANVIL_API::LATEST_PATH, body, response, aError ) )
        return false;

    try
    {
        nlohmann::json js = nlohmann::json::parse( response );

        // Payload sits under "data"; findField() looks there after the top level.
        const char* const latestFields[] = { ANVIL_API::F_LATEST_VERSION,
                                             ANVIL_API::F_VERSION };

        std::string latest = findField( js, latestFields );

        if( !latest.empty() )
        {
            aInfo.latest_version = wxString::FromUTF8( latest );
            aInfo.current_version = current;

            const char* const urlFields[]  = { ANVIL_API::F_DOWNLOAD_URL };
            const char* const descFields[] = { ANVIL_API::F_DESCRIPTION };

            aInfo.download_url = wxString::FromUTF8( findField( js, urlFields ) );
            aInfo.description = wxString::FromUTF8( findField( js, descFields ) );

            // Trust the server's own verdict when it gives one; fall back to a string
            // comparison only when the field is absent.
            if( std::optional<bool> flag = findBool( js, ANVIL_API::F_UPDATE_AVAILABLE ) )
                aInfo.update_available = *flag;
            else
                aInfo.update_available = ( aInfo.latest_version != current );

            return true;
        }
    }
    catch( ... )
    {
    }

    aError = _( "Unexpected version response from the server." );
    return false;
}


bool ANVIL_AUTH::IsLoggedIn()
{
    std::optional<SESSION> session = loadSession();

    if( !session )
        return false;

    if( session->expires_at > 0
        && session->expires_at <= (long long) wxDateTime::Now().GetTicks() )
    {
        return false;
    }

    return true;
}


wxString ANVIL_AUTH::GetEmail()
{
    if( std::optional<SESSION> session = loadSession() )
        return session->email;

    return wxEmptyString;
}


void ANVIL_AUTH::WipeSession()
{
    KIPLATFORM::SECRETS::DeleteSecret( SECRET_SERVICE, SECRET_KEY );
}
