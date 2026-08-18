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

#ifndef ANVIL_API_DEFS_H_
#define ANVIL_API_DEFS_H_

/**
 * Anvil auth backend — API STRUCTURE, in one place.
 *
 * This header is the single source of truth for the endpoints of the Anvil auth API plus
 * the header and JSON field names.  If the backend renames an endpoint or a field, edit it
 * here and nowhere else.
 *
 * The endpoint URLs are declared in full (the server host is not a secret — it is visible
 * in network traffic anyway).  A URL here may also be given as a bare path ("/api/...");
 * ANVIL_AUTH then prefixes it with the ApiBase from the `anvil_auth` config file, which
 * also lets a deployment override the host without a rebuild via ApiBase + relative paths.
 *
 * The api-key itself is still a VALUE and deliberately never appears here: it is resolved
 * at runtime by ANVIL_AUTH_CONFIG (environment variable, then the `anvil_auth` config
 * file), so `strings anvil.exe` can never reveal a key.
 */
namespace ANVIL_API
{
// ---- endpoints (full URL, or a bare path resolved against the configured ApiBase) ------

/// POST — registers/identifies the user by email and triggers the OTP mail.
inline constexpr char REGISTER_PATH[]   = "https://bnc-ai.com/api/designing-users/public/register";

/// POST — verifies the emailed OTP; the response carries the session token.
inline constexpr char VERIFY_OTP_PATH[] = "https://bnc-ai.com/api/designing-users/public/verify-otp";

/// POST — invalidates the server-side session for this email.
inline constexpr char LOGOUT_PATH[]     = "https://bnc-ai.com/api/designing-users/public/logout";

/// POST — returns the latest released application version for an os_type.
inline constexpr char LATEST_PATH[]     = "https://bnc-ai.com/api/software/latest";

// ---- headers ---------------------------------------------------------------------------

/// Deployment api-key header; its value comes from ANVIL_AUTH_CONFIG, never from code.
/// Verified against the live server: "api-key" is rejected with 401 "Invalid or missing
/// API key"; "x-api-key" is the name it actually reads.
inline constexpr char HDR_API_KEY[] = "x-api-key";

// ---- request JSON field names ----------------------------------------------------------

inline constexpr char F_EMAIL[]      = "email";
inline constexpr char F_PROJECT_ID[] = "project_id";
inline constexpr char F_OTP[]        = "otp";
inline constexpr char F_OS_TYPE[]    = "os_type";
inline constexpr char F_VERSION[]    = "version";

// ---- version response field names -------------------------------------------------------
// Verified against the live endpoint, which answers under a "data" wrapper with:
//   version, os_type, description, is_latest, download_url, update_available,
//   current_version, latest_version

inline constexpr char F_LATEST_VERSION[]   = "latest_version";
inline constexpr char F_UPDATE_AVAILABLE[] = "update_available";
inline constexpr char F_DOWNLOAD_URL[]     = "download_url";
inline constexpr char F_DESCRIPTION[]      = "description";

// ---- response parsing ------------------------------------------------------------------

/**
 * The verify-otp response is confirmed to carry a session token, but the exact field name is
 * not yet pinned down by the backend docs.  ANVIL_AUTH tries these names in order, first at
 * the top level and then inside a "data" / "result" wrapper object.  Once the real response
 * is known, shrink this list to the one true name.
 */
inline constexpr const char* TOKEN_FIELDS[]  = { "access_token", "token", "session_token" };
inline constexpr const char* EXPIRY_FIELDS[] = { "expires_in", "expires_at", "expiry" };

/// Wrapper objects some backends nest their payload under; searched after the top level.
inline constexpr const char* WRAPPER_FIELDS[] = { "data", "result" };

/// Candidate fields for a human-readable message in an error response body.
inline constexpr const char* ERROR_FIELDS[] = { "message", "error", "detail" };

// ---- constants for this build ----------------------------------------------------------

/// The os_type value the version endpoint expects for this build's platform.
#if defined( __WXMSW__ )
inline constexpr char OS_TYPE[] = "windows";
#elif defined( __WXOSX__ )
inline constexpr char OS_TYPE[] = "mac";
#endif

/// OTP length (sample OTPs from the backend are 6 digits).
inline constexpr int OTP_LENGTH = 6;

/// Seconds before the "Resend code" link re-enables on the OTP page.
inline constexpr int RESEND_COOLDOWN_SECS = 60;

} // namespace ANVIL_API

#endif // ANVIL_API_DEFS_H_
