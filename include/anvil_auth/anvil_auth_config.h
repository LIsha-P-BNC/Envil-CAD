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

#ifndef ANVIL_AUTH_CONFIG_H_
#define ANVIL_AUTH_CONFIG_H_

#include <kicommon.h>
#include <wx/string.h>

/**
 * Deployment VALUES for the Anvil auth backend — one standard `.env` file, nothing else.
 *
 * The file lives NEXT TO THE EXECUTABLE and is plain .env syntax (Key=Value lines,
 * '#' comments):
 *
 *     ApiKey=dt_1...          # REQUIRED - deployment api-key ("api-key" request header)
 *     ProjectId=2             # REQUIRED - sent when requesting an OTP
 *     ApiBase=https://...     # optional - only for bare-path endpoints in anvil_api_defs.h
 *     SessionDays=30          # optional - local session lifetime when server sends no expiry
 *
 * Ship a filled-in `.env` inside the installer/zip and a new machine needs no setup.
 * `.env.example` in the repository is the committed template; the real file is
 * .gitignore'd and must never be committed — that is the whole point of keeping values
 * out of the code: `strings anvil.exe` can never reveal a key, and rotating the key is a
 * one-file edit with no rebuild.
 *
 * (Being beside the exe, the file is readable by anyone holding the exe.  That removes
 * setup friction, not exposure — the endpoint is protected by server-side rate limiting,
 * not by hiding this file.)
 *
 * Values are re-read on every accessor call (the file is tiny), so editing the file takes
 * effect on the next sign-in attempt without restarting.
 */
class KICOMMON_API ANVIL_AUTH_CONFIG
{
public:
    /// Absolute path of the `.env` file beside the executable (it may not exist yet).
    static wxString ConfigFilePath();

    /// Deployment api-key sent in the "api-key" header, or empty.
    static wxString ApiKey();

    /// project_id value for the register call, or empty.
    static wxString ProjectId();

    /// Optional server base URL override (no trailing slash), or empty.
    static wxString ApiBase();

    /// Optional shared Anthropic API key for the AI engine (ANTHROPIC_API_KEY), or empty.
    /// The simplest way to make a shared install's AI work with no per-machine Claude login:
    /// one deployment key billed centrally. Set a spend cap on it in the Anthropic console.
    static wxString ClaudeApiKey();

    /// Optional base URL of the Anthropic-compatible Claude proxy (no trailing slash), or
    /// empty. When set (and no shared key), the AI engine is pointed here (ANTHROPIC_BASE_URL)
    /// and authenticated with the signed-in user's JWT, for per-user metering instead.
    static wxString ClaudeBaseUrl();

    /// Local session lifetime (days) used when the server reports no expiry.  Default 30.
    static int SessionDays();

    /// True when an api-key is present — the one value the app cannot run without.
    static bool IsConfigured();

};

#endif // ANVIL_AUTH_CONFIG_H_
