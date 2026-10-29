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

#ifndef ANVIL_AUTH_H_
#define ANVIL_AUTH_H_

#include <kicommon.h>
#include <wx/string.h>

/// The signed-in account, as shown in the app's account UI.
struct KICOMMON_API ANVIL_USER
{
    wxString email;     ///< always present while signed in
    wxString name;      ///< display name, may be empty
    wxString role;      ///< e.g. "designer", may be empty

    /// Best available label for a one-line display: name when known, else the email.
    wxString Label() const { return name.IsEmpty() ? email : name; }
};


/// What the /software/latest endpoint reports about this build.
struct KICOMMON_API ANVIL_VERSION_INFO
{
    wxString current_version;    ///< the version this build reported to the server
    wxString latest_version;     ///< newest release for this platform
    wxString download_url;       ///< installer link for the newest release
    wxString description;        ///< release notes (HTML fragment)
    bool     update_available = false;
};


/**
 * Email-OTP authentication against the Anvil backend.
 *
 * API structure comes from anvil_api_defs.h; deployment values (base URL, api-key,
 * project id) from ANVIL_AUTH_CONFIG.  The session token returned by verify-otp is kept in
 * the platform secret store (Windows Credential Manager / libsecret / Keychain) — never in
 * a settings JSON, never in a log.
 *
 * All network calls are synchronous and BLOCK; call them from a worker thread (e.g.
 * GetKiCadThreadPool()) and marshal results back to the UI with CallAfter().  The
 * session-state queries (IsLoggedIn / GetEmail) are cheap and thread-safe.
 */
class KICOMMON_API ANVIL_AUTH
{
public:
    /**
     * Request an OTP mail for @a aEmail (POST register).
     * @return true on success; on failure @a aError carries a user-displayable message.
     */
    static bool RequestOtp( const wxString& aEmail, wxString& aError );

    /**
     * Verify the emailed @a aOtp (POST verify-otp).  On success the session token from the
     * response is persisted in the secret store, and IsLoggedIn() becomes true.
     */
    static bool VerifyOtp( const wxString& aEmail, const wxString& aOtp, wxString& aError );

    /**
     * Sign in with a handoff token minted by another NEMI product (POST sso-exchange).
     *
     * This is the OTP flow's twin: the server does the identifying, and on success the
     * session it returns is persisted exactly as VerifyOtp() persists one, so everything
     * downstream — IsLoggedIn(), GetSessionToken(), the account UI, Logout() — behaves as
     * if the user had typed a code.  The difference is only in who vouched for them.
     *
     * @a aHandoffToken is short-lived (about two minutes) and single-use: a failure here is
     * final for that token, and the caller must send the user back to the normal sign-in
     * rather than retrying.  The token is a credential — never log it.
     *
     * @return true on success; on failure @a aError carries a user-displayable message and
     *         any previous session is left untouched.
     */
    static bool SignInWithHandoffToken( const wxString& aHandoffToken, wxString& aError );

    /// Outcome of looking for a handoff token on the command line.
    enum class HANDOFF
    {
        NONE,       ///< No token was offered — an ordinary launch.
        SIGNED_IN,  ///< A token was offered and accepted; a session now exists.
        FAILED      ///< A token was offered and refused; @a aError says why.
    };

    /**
     * Sign in from a handoff token passed on the command line, if one is there.
     *
     * Reads the running application's own arguments, so every entry point can
     * call it the same way with nothing to thread through. Two forms are
     * accepted:
     *
     *   anvilcad://sso?token=<url-encoded>   what the operating system hands us
     *                                        when a browser follows the link
     *   --handoff-token=<token>              the same thing by hand, which is
     *                                        how this can be exercised before
     *                                        the installer registers the
     *                                        protocol on a machine
     *
     * NONE is the ordinary case and says nothing is wrong. FAILED means someone
     * really did arrive from a sign-in link and it did not work — worth telling
     * them, because otherwise the app silently asks for a code they have just
     * been spared.
     *
     * Any existing session is left alone when no token is offered.
     */
    static HANDOFF TrySignInFromCommandLine( wxString& aError );

    /**
     * End the session: best-effort POST logout to the server, then wipe the local session
     * unconditionally.  @return true when the server acknowledged the logout (the local
     * session is gone either way).
     */
    static bool Logout( wxString* aError = nullptr );

    /**
     * End the session without ever blocking the caller: the local session is wiped before
     * this returns (so IsLoggedIn() is already false), and the server-side logout is posted
     * on the shared thread pool with the credentials captured beforehand.
     *
     * This is what interactive sign-out should call — Logout() blocks on the network for as
     * long as the connect timeout allows, which freezes the window it was called from.
     */
    static void LogoutDetached();

    /**
     * Ask the server for the latest released version for this platform (POST latest).
     * Wired but intentionally not called at startup yet.
     */
    static bool CheckVersion( ANVIL_VERSION_INFO& aInfo, wxString& aError );

    /// True when a session token is stored and not expired.
    static bool IsLoggedIn();

    /// Email of the stored session, or empty when logged out.
    static wxString GetEmail();

    /// The signed-in user's JWT (bearer credential), or empty when logged out / expired.
    /// Handed to the Claude engine as the proxy auth token so a shared install needs no
    /// per-machine API key.
    static wxString GetSessionToken();

    /// The signed-in account's details; all fields empty when logged out.
    static ANVIL_USER GetUser();

    /// Remove the local session without contacting the server (e.g. after a 401).
    static void WipeSession();
};

#endif // ANVIL_AUTH_H_
