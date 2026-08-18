/*
 * Envil AI - native Claude client for Envil-CAD.
 *
 * Talks to the Anthropic Messages API (POST /v1/messages) directly over HTTPS
 * using Anvil's bundled libcurl wrapper. No external process, no Python.
 *
 * Phase 2: tool-use support. The caller drives the agent loop; this class does
 * one Messages-API round trip per call. Messages/tools are passed as JSON
 * strings (Anthropic wire shape) so the public header stays light.
 *
 * The API key is read from the CLAUDE_API_KEY (or ANTHROPIC_API_KEY) env var.
 */

#ifndef ENVIL_AI_CLIENT_H
#define ENVIL_AI_CLIENT_H

#include <string>
#include <wx/string.h>

#include <kicommon.h>

class KICOMMON_API ENVIL_AI_CLIENT
{
public:
    ENVIL_AI_CLIENT();

    /// True when an API key is available in the environment.
    bool HasApiKey() const;

    /**
     * One Messages-API round trip.
     *
     * @param aMessagesJson  the "messages" array as a JSON string (Anthropic shape).
     * @param aToolsJson     the "tools" array as a JSON string (may be empty for none).
     * @param aAssistantContentJson [out] the assistant reply's "content" array (JSON string).
     * @param aStopReason    [out] e.g. "end_turn" or "tool_use".
     * @param aError         [out] a human-readable error on failure.
     * @return true on success.
     *
     * Blocking call - run it off the UI thread.
     */
    bool SendTurn( const std::string& aMessagesJson, const std::string& aToolsJson,
                   std::string& aAssistantContentJson, std::string& aStopReason,
                   wxString& aError );

    void SetModel( const wxString& aModel ) { m_model = aModel; }

    /**
     * Extra context appended to the user-editable system prompt on every turn, e.g. the
     * open project folder and schematic. Kept separate from the prompt file so refreshing
     * context never rewrites what the user has authored there.
     */
    void SetContextSuffix( const wxString& aSuffix ) { m_contextSuffix = aSuffix; }

    /// The effective system prompt (user-editable file + live context). Used by the CLI
    /// backend, which passes it via --append-system-prompt-file.
    wxString GetSystemPrompt() const;

    /// True when a Claude API key is present in the environment (API-key backend usable).
    /// (HasApiKey above.) This mirror keeps naming parallel with the CLI check in the agent.

private:
    wxString getApiKey() const;

    wxString m_model;
    int      m_maxTokens;
    wxString m_endpoint;
    wxString m_contextSuffix;
};

#endif // ENVIL_AI_CLIENT_H
