#!/usr/bin/env node
/*
 * envil-mcp — MCP server that bridges an external Claude client (Claude Desktop, Claude
 * Code, Cursor, ...) to a running Envil-CAD instance.
 *
 * The user's own client provides the model (their subscription); this process just exposes
 * Envil-CAD's schematic and board tools over MCP and forwards each call to the app's loopback
 * tool socket (ENVIL_AI_TOOL_SERVER, default 127.0.0.1:5571). Envil-CAD routes each call to
 * the schematic or the board editor by tool name, through exactly the same path as its
 * in-app AI panel.
 *
 * No API key involved. Envil-CAD must be running with the relevant document open.
 */

import net from "node:net";
import { Server } from "@modelcontextprotocol/sdk/server/index.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import {
  ListToolsRequestSchema,
  CallToolRequestSchema,
} from "@modelcontextprotocol/sdk/types.js";

const HOST = process.env.ENVIL_MCP_HOST || "127.0.0.1";
const PORT = parseInt(process.env.ENVIL_MCP_PORT || "5571", 10);

/**
 * Send one tool request to Envil-CAD and await its single-line JSON reply.
 * One short-lived connection per call keeps this stateless and robust.
 */
function callEnvil(request) {
  return new Promise((resolve, reject) => {
    const sock = net.createConnection({ host: HOST, port: PORT });
    let buf = "";
    const timer = setTimeout(() => {
      sock.destroy();
      reject(new Error(`Envil-CAD did not respond within 60s on ${HOST}:${PORT}`));
    }, 60000);

    sock.on("connect", () => sock.write(JSON.stringify(request) + "\n"));
    sock.on("data", (d) => {
      buf += d.toString("utf8");
      const nl = buf.indexOf("\n");
      if (nl !== -1) {
        clearTimeout(timer);
        const line = buf.slice(0, nl);
        sock.end();
        try {
          resolve(JSON.parse(line));
        } catch (e) {
          reject(new Error("Bad reply from Envil-CAD: " + line));
        }
      }
    });
    sock.on("error", (e) =>
      reject(
        new Error(
          `Cannot reach Envil-CAD on ${HOST}:${PORT} (${e.code}). ` +
            "Is Envil-CAD running with a schematic open?"
        )
      )
    );
  });
}

// All coordinates are in MILS and should be multiples of 50 (the default schematic grid),
// so pins and wire ends actually land on grid and connect.
const XY = {
  x_mils: { type: "integer", description: "X position in mils (multiple of 50)." },
  y_mils: { type: "integer", description: "Y position in mils (multiple of 50)." },
};

const TOOLS = [
  {
    name: "get_schematic",
    description:
      "Read the open schematic: every placed symbol with its reference, value, lib_id, " +
      "body position, and each pin's number, name, and ABSOLUTE [x_mils, y_mils]. Call " +
      "this before wiring so add_wire endpoints land exactly on pins.",
    inputSchema: {
      type: "object",
      properties: {
        include_pins: {
          type: "boolean",
          description: "Include pin lists (default true). Set false for a quick inventory.",
        },
      },
    },
  },
  {
    name: "run_erc",
    description:
      "Run the full native Electrical Rules Check on the open schematic and return every " +
      "violation (severity, rule title, detail message, x/y position) plus error/warning " +
      "counts and a 'clean' flag. Use this to check your work and loop until clean.",
    inputSchema: { type: "object", properties: {} },
  },
  {
    name: "snap_to_grid",
    description:
      "Bring off-grid symbol pins and wire ends onto the connection grid (default 50 mils). " +
      "Symbols are moved by the delta their own pins need, and wire ends attached to those " +
      "pins move with them, so existing connections are preserved. Use when ERC reports " +
      "'symbol pin or wire end off connection grid' (common after importing from Altium).",
    inputSchema: {
      type: "object",
      properties: {
        grid_mils: { type: "integer", description: "Connection grid in mils (default 50)." },
      },
    },
  },
  {
    name: "annotate",
    description:
      "Assign reference designators to un-annotated symbols (power flags, newly added parts). " +
      "Run before run_erc to clear 'not annotated' errors.",
    inputSchema: { type: "object", properties: {} },
  },
  {
    name: "add_component",
    description: "Place a component symbol into the schematic open in Envil-CAD.",
    inputSchema: {
      type: "object",
      properties: {
        lib_id: {
          type: "string",
          description:
            "KiCad library id 'Library:Symbol', e.g. 'Regulator_Linear:AP2112K-3.3', " +
            "'Device:R', 'Device:C', 'power:GND', 'power:+3V3'.",
        },
        reference: { type: "string", description: "Reference designator, e.g. U1, R1, C1." },
        value: { type: "string", description: "Optional value, e.g. '10k'." },
        ...XY,
      },
      required: ["lib_id", "reference"],
    },
  },
  {
    name: "add_wire",
    description:
      "Draw a wire path. Each consecutive pair of points becomes one wire segment, so a " +
      "multi-point path draws a connected polyline. Use right-angle paths.",
    inputSchema: {
      type: "object",
      properties: {
        points: {
          type: "array",
          description: "Ordered [x_mils, y_mils] pairs, at least two.",
          items: {
            type: "array",
            items: { type: "integer" },
            minItems: 2,
            maxItems: 2,
          },
          minItems: 2,
        },
      },
      required: ["points"],
    },
  },
  {
    name: "add_label",
    description: "Add a net label at a point (net naming / connect-by-name).",
    inputSchema: {
      type: "object",
      properties: {
        name: { type: "string", description: "Net name, e.g. 'VOUT', 'SW'." },
        kind: {
          type: "string",
          enum: ["label", "global", "hier"],
          description: "label = local (default), global = global label, hier = hierarchical.",
        },
        ...XY,
      },
      required: ["name"],
    },
  },
  {
    name: "add_junction",
    description: "Add a junction dot where wires cross and must connect.",
    inputSchema: { type: "object", properties: { ...XY }, required: ["x_mils", "y_mils"] },
  },
  {
    name: "add_no_connect",
    description: "Mark a pin as intentionally unconnected (X flag).",
    inputSchema: { type: "object", properties: { ...XY }, required: ["x_mils", "y_mils"] },
  },
  {
    name: "edit_value",
    description: "Change the value field of an already-placed symbol.",
    inputSchema: {
      type: "object",
      properties: {
        reference: { type: "string", description: "Reference designator, e.g. R2." },
        new_value: { type: "string", description: "New value, e.g. '47k'." },
      },
      required: ["reference", "new_value"],
    },
  },
  {
    name: "move_component",
    description: "Move an already-placed symbol to a new position.",
    inputSchema: {
      type: "object",
      properties: {
        reference: { type: "string", description: "Reference designator, e.g. C3." },
        ...XY,
      },
      required: ["reference", "x_mils", "y_mils"],
    },
  },
  {
    name: "delete_component",
    description: "Delete a placed symbol by reference designator.",
    inputSchema: {
      type: "object",
      properties: {
        reference: { type: "string", description: "Reference designator to delete." },
      },
      required: ["reference"],
    },
  },
  {
    name: "delete_at",
    description:
      "Delete stray non-symbol items (wire, label, junction, no-connect) at/near a point. " +
      "Use to clean up orphan labels or dangling wire ends that ERC flags.",
    inputSchema: {
      type: "object",
      properties: {
        ...XY,
        radius_mils: { type: "integer", description: "Search radius in mils (default 30)." },
      },
      required: ["x_mils", "y_mils"],
    },
  },

  // ---- board tools; Envil-CAD routes these to the PCB editor by name ----
  {
    name: "get_board",
    description:
      "Read the open board: every footprint with its reference, value, fpid, layer, rotation " +
      "and [x_mils, y_mils], plus the board extents, copper layer names and net names. Call " +
      "this before moving or routing anything so you work from real coordinates.",
    inputSchema: {
      type: "object",
      properties: {
        include_pads: {
          type: "boolean",
          description: "Include each footprint's pads with their nets and positions (default false).",
        },
      },
    },
  },
  {
    name: "run_drc",
    description:
      "Run the full native Design Rules Check on the open board and return every violation " +
      "(severity, rule title, detail message, x/y) plus error/warning counts and a 'clean' " +
      "flag. The board's equivalent of run_erc: use it to check your layout and loop.",
    inputSchema: {
      type: "object",
      properties: {
        refill_zones: { type: "boolean", description: "Refill zones first (default false)." },
        test_footprints: {
          type: "boolean",
          description: "Also run schematic-parity tests (default false).",
        },
      },
    },
  },
  {
    name: "add_footprint",
    description:
      "Place a footprint from the configured libraries onto the board. The board's " +
      "add_component.",
    inputSchema: {
      type: "object",
      properties: {
        fpid: {
          type: "string",
          description:
            "Footprint library id 'Library:Footprint', e.g. 'Resistor_SMD:R_0603_1608Metric'.",
        },
        reference: { type: "string", description: "Reference designator, e.g. R1." },
        value: { type: "string", description: "Optional value, e.g. '10k'." },
        x_mils: { type: "integer", description: "X position in mils." },
        y_mils: { type: "integer", description: "Y position in mils." },
        rotation_deg: { type: "number", description: "Orientation in degrees." },
        layer: {
          type: "string",
          description: "'F.Cu' (default) or 'B.Cu' to place it on the back.",
        },
      },
      required: ["fpid"],
    },
  },
  {
    name: "move_footprint",
    description: "Move, rotate and/or flip a placed footprint on the board.",
    inputSchema: {
      type: "object",
      properties: {
        reference: { type: "string", description: "Reference designator, e.g. U1." },
        x_mils: { type: "integer", description: "X position in mils." },
        y_mils: { type: "integer", description: "Y position in mils." },
        rotation_deg: { type: "number", description: "Absolute orientation in degrees." },
        flip: { type: "boolean", description: "Flip to the other side of the board." },
      },
      required: ["reference"],
    },
  },
  {
    name: "add_track",
    description:
      "Route copper along a point path. Each consecutive pair of points becomes one track " +
      "segment. Give the net name so the track belongs to that net, and prefer 45-degree or " +
      "right-angle paths.",
    inputSchema: {
      type: "object",
      properties: {
        points: {
          type: "array",
          description: "Ordered [x_mils, y_mils] pairs, at least two.",
          items: { type: "array", items: { type: "integer" }, minItems: 2, maxItems: 2 },
          minItems: 2,
        },
        layer: {
          type: "string",
          description: "Copper layer name, e.g. 'F.Cu' (default) or 'B.Cu'.",
        },
        net: { type: "string", description: "Net name, e.g. 'GND' or '+3V3'." },
        width_mils: {
          type: "integer",
          description: "Track width in mils (default: the board's current track width).",
        },
      },
      required: ["points"],
    },
  },
  {
    name: "add_via",
    description: "Add a through via, e.g. to change layer mid-route.",
    inputSchema: {
      type: "object",
      properties: {
        x_mils: { type: "integer", description: "X position in mils." },
        y_mils: { type: "integer", description: "Y position in mils." },
        net: { type: "string", description: "Net name." },
        diameter_mils: { type: "integer", description: "Via pad diameter in mils." },
        drill_mils: { type: "integer", description: "Via drill diameter in mils." },
      },
      required: ["x_mils", "y_mils"],
    },
  },
  {
    name: "delete_track_at",
    description:
      "Delete the tracks/vias that end at or near a point. The board's delete_at, for " +
      "ripping up a bad route.",
    inputSchema: {
      type: "object",
      properties: {
        x_mils: { type: "integer", description: "X position in mils." },
        y_mils: { type: "integer", description: "Y position in mils." },
        radius_mils: { type: "integer", description: "Search radius in mils (default 20)." },
      },
      required: ["x_mils", "y_mils"],
    },
  },
];

const server = new Server(
  { name: "envil-cad", version: "0.1.0" },
  { capabilities: { tools: {} } }
);

server.setRequestHandler(ListToolsRequestSchema, async () => ({ tools: TOOLS }));

server.setRequestHandler(CallToolRequestSchema, async (req) => {
  const { name, arguments: args } = req.params;

  if (!TOOLS.some((t) => t.name === name))
    return { isError: true, content: [{ type: "text", text: `Unknown tool: ${name}` }] };

  try {
    const result = await callEnvil({ tool: name, input: args || {} });
    // Return the FULL result JSON, not just result.message — read tools like
    // get_schematic carry structured data (symbols + pin coordinates) the model
    // needs. A message-only reply would strip that and leave it wiring blind.
    return {
      isError: result.ok === false,
      content: [{ type: "text", text: JSON.stringify(result) }],
    };
  } catch (e) {
    return { isError: true, content: [{ type: "text", text: e.message }] };
  }
});

const transport = new StdioServerTransport();
await server.connect(transport);
console.error(`envil-mcp ready; bridging to Envil-CAD at ${HOST}:${PORT}`);
