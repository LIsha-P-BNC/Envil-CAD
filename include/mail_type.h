/*
 * This program source code file is part of Anvil, a free EDA CAD application.
 *
 * Copyright (C) 2014 CERN
 * Copyright The KiCad Developers, see AUTHORS.TXT for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */


#ifndef MAIL_TYPE_H_
#define MAIL_TYPE_H_

/**
 * The set of mail types sendable via #KIWAY::ExpressMail() and supplied as
 * the @a aCommand parameter to that function.
 *
 * Such mail will be received in KIWAY_PLAYER::KiwayMailIn( KIWAY_MAIL_EVENT& aEvent ) and
 * aEvent.Command() will match aCommand to KIWAY::ExpressMail().
 */
enum MAIL_T
{
    MAIL_CROSS_PROBE,              // PCB<->SCH, CVPCB->SCH cross-probing.
    MAIL_SELECTION,                // SCH<->PCB selection synchronization.
    MAIL_SELECTION_FORCE,          // Explicit selection of SCH->PCB selection synchronization.
    MAIL_ASSIGN_FOOTPRINTS,        // CVPCB->SCH footprint stuffing
    MAIL_SCH_SAVE,                 // CVPCB->SCH save the schematic
    MAIL_EESCHEMA_NETLIST,         // SCH->CVPCB netlist immediately after launching CVPCB
    MAIL_SYMBOL_NETLIST,           // SCH->FP_CHOOSER symbol pin & fp_filter information
    MAIL_PCB_UPDATE,               // SCH->PCB forward update
    MAIL_SCH_UPDATE,               // PCB->SCH forward update
    MAIL_IMPORT_FILE,              // Import a different format file
    MAIL_SCH_GET_NETLIST,          // Fetch a netlist from schematics
    MAIL_SCH_GET_ITEM,             // Fetch item from KIID
    MAIL_PCB_GET_NETLIST,          // Fetch a netlist from PCB layout
    MAIL_PCB_UPDATE_LINKS,         // Update the schematic symbol paths in the PCB's footprints
    MAIL_SCH_REFRESH,              // Tell the schematic editor to refresh the display.
    MAIL_ADD_LOCAL_LIB,            // Add a local library to the project library table
    MAIL_LIB_EDIT,
    MAIL_FP_EDIT,
    MAIL_RELOAD_LIB,               // Reload Library List if one was added
    MAIL_RELOAD_PLUGINS,           // Reload python plugins
    MAIL_REFRESH_SYMBOL,           // Refresh symbol in symbol viewer
    MAIL_SCH_NAVIGATE_TO_SHEET,    // Navigate to sheet by filename if in hierarchy

    /**
     * Envil AI: run one AI tool call against the schematic (SHELL/AI->SCH).
     *
     * Unlike the other mails this one is a request/response: the payload carries the tool
     * request as JSON in, and the handler overwrites it with the result JSON out. This works
     * because KIWAY::ExpressMail/ProcessEvent dispatch synchronously and hand the *same*
     * KIWAY_MAIL_EVENT to the recipient, so writes to mail.GetPayload() are visible to the
     * sender once dispatch returns. It is what lets the shell-owned AI panel (CommonAiPanel),
     * which lives in kicad.exe and cannot see SCH_EDIT_FRAME across the KIFACE boundary,
     * still place parts in the schematic.
     *
     * In:  {"tool":"add_component","input":{ ... }}
     * Out: {"ok":true|false,"message":"..."}
     */
    MAIL_ENVIL_AI_TOOL,

    /**
     * Anvil: convert a symbol library to the native .anvil_sym format (SHELL->SCH).
     * Request/response like MAIL_ENVIL_AI_TOOL: payload in is "srcPath\ndestPath",
     * the handler overwrites it with "OK <n> symbols" or "ERROR <message>".
     */
    MAIL_ENVIL_CONVERT_SYMLIB,

    /**
     * Anvil: convert a footprint library to the native format (SHELL->PCB).
     * Request/response: payload in is "srcPath\ndestDir", replaced with
     * "OK <n> footprints" or "ERROR <message>".
     */
    MAIL_ENVIL_CONVERT_FPLIB,

    /**
     * Anvil: capture the symbols embedded in the open schematic into a project-local
     * .anvil_sym library and register every referenced library nickname against it
     * (SHELL->SCH). Imported designs carry embedded symbols whose lib_id nicknames point
     * at the foreign tool's libraries, which exist in no table -- this makes them real,
     * editable Anvil libraries instead. Request/response: payload replaced with
     * "OK <n> symbols" or "ERROR <message>".
     */
    MAIL_ENVIL_CAPTURE_SYMBOLS,

    /**
     * Envil AI: run one AI tool call against the board (SHELL/AI->PCB).
     * The board-side twin of MAIL_ENVIL_AI_TOOL, with the same request/response payload
     * contract, so layout work reaches PCB_EDIT_FRAME by the same route schematic work
     * reaches SCH_EDIT_FRAME.
     *
     * In:  {"tool":"add_track","input":{ ... }}
     * Out: {"ok":true|false,"message":"..."}
     */
    MAIL_ENVIL_PCB_TOOL,

    /**
     * Anvil: capture the open board's footprints into a project-local .pretty library and
     * register every referenced library nickname against it (SHELL->PCB). The footprint
     * counterpart of MAIL_ENVIL_CAPTURE_SYMBOLS: imported boards carry footprints whose
     * fpid nicknames name the foreign tool's libraries (or nothing at all), which resolve
     * nowhere -- this gives them a real, editable Anvil library. Request/response: payload
     * replaced with "OK <n> footprints" or "ERROR <message>".
     */
    MAIL_ENVIL_CAPTURE_FOOTPRINTS
};

#endif  // MAIL_TYPE_H_
