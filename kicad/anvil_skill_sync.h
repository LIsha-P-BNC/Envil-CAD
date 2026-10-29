/*
 * Anvil AI — shared team design-skill sync (pull side).
 *
 * One company-wide skills.json lives on S3 (public-read, same bucket/prefix convention
 * as the library bundles).  Every launch refreshes the local cache
 * <settings>/anvil_ai_skills/skills.json from it, so the AI chat's "TEAM DESIGN SKILLS"
 * prompt block (ANVIL_AI_AGENT::buildSkillPromptBlock) always reflects what the whole
 * team has taught the AI.  The push side lives in the Python MCP server
 * (skidl_mcp_server.py `skill` tool -> skidl/anvil/anvil_skills.py).
 */

#ifndef ANVIL_SKILL_SYNC_H
#define ANVIL_SKILL_SYNC_H

namespace ANVIL_SKILL_SYNC
{
/// Fire-and-forget background refresh of the local skill cache from S3.  Never blocks,
/// never pops UI: offline / HTTP errors / corrupt payloads leave the existing cache
/// untouched (the chat simply keeps using the last pulled copy).
void PullAsync();
}

#endif // ANVIL_SKILL_SYNC_H
