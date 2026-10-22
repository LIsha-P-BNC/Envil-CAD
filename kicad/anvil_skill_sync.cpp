/*
 * Anvil AI — shared team design-skill sync (pull side).  See anvil_skill_sync.h.
 */

// curl headers must precede wxWidgets headers (see kicad_curl_easy.h header comment).
#include <kicad_curl/kicad_curl.h>
#include <kicad_curl/kicad_curl_easy.h>

#include "anvil_skill_sync.h"

#include <string>
#include <thread>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/utils.h>           // wxGetEnv

#include <json_common.h>
#include <settings/settings_manager.h>


// The shared team skill file.  Anonymous public-read GET, exactly like the installer's
// library-bundle downloads from the same bucket (dist/installer/AnvilCAD.iss) — the pull
// must work before any AWS credentials or backend session exist on the machine.  The
// push side (conditional PUT + outbox) is Python: skidl/anvil/anvil_skills.py, which
// honors the matching ANVIL_SKILLS_* overrides.
static const char* SKILLS_URL_DEFAULT =
        "https://dms-booma.s3.ap-south-1.amazonaws.com/anvilcad_electronics/anvil_ai_skills/"
        "skills.json";


static void pullOnce()
{
    std::string url = SKILLS_URL_DEFAULT;

    {
        wxString override;

        if( wxGetEnv( wxS( "ANVIL_SKILLS_URL" ), &override ) && !override.IsEmpty() )
            url = std::string( override.utf8_str() );
    }

    KICAD_CURL_EASY curl;

    curl.SetURL( url );
    curl.SetFollowRedirects( true );
    curl.SetUserAgent( "Anvil-SkillSync/1.0" );
    curl.SetConnectTimeout( 10 );
    curl.SetTimeout( 20 );          // small fixed-size JSON; longer means stalled

    if( curl.Perform() != CURLE_OK || curl.GetResponseStatusCode() != 200 )
        return;                     // offline / not yet created: keep the cached copy

    const std::string& body = curl.GetBuffer();

    // Never replace a good cache with garbage: the payload must at least be the skill
    // schema before it lands on disk.
    try
    {
        nlohmann::json doc = nlohmann::json::parse( body );

        if( !doc.is_object() || !doc.contains( "schema_version" )
                || !doc["schema_version"].is_number_integer() || !doc.contains( "skills" )
                || !doc["skills"].is_array() )
        {
            return;
        }
    }
    catch( const std::exception& )
    {
        return;
    }

    wxLogNull noLog;                // a failed cache write must never pop UI

    wxFileName dir( SETTINGS_MANAGER::GetUserSettingsPath(), wxEmptyString );
    dir.AppendDir( wxS( "anvil_ai_skills" ) );

    if( !dir.DirExists() && !dir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
        return;

    // Atomic replace so a concurrently running chat turn never reads a torn file.
    wxFileName fn( dir.GetPath(), wxS( "skills.json" ) );
    wxString   tmpPath = fn.GetFullPath() + wxS( ".pull.tmp" );

    {
        wxFFile tmp( tmpPath, wxS( "wb" ) );

        if( !tmp.IsOpened() )
            return;

        tmp.Write( body.data(), body.size() );
    }

    wxRenameFile( tmpPath, fn.GetFullPath(), true );
}


void ANVIL_SKILL_SYNC::PullAsync()
{
    // Detached: the pull is best-effort and must never delay startup or shutdown — worst
    // case this launch keeps last launch's cache and the next launch catches up.
    std::thread( []() { pullOnce(); } ).detach();
}
