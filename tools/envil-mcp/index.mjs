#!/usr/bin/env node
/*
 * envil-mcp — MCP server that bridges an external Claude client (Claude Desktop, Claude
 * Code, Cursor, ...) to a running Envil-CAD instance.
 *
 * The user's own client provides the model (their subscription); this process just exposes
 * Envil-CAD's schematic tools over MCP and forwards each call to the app's loopback tool
 * socket (ENVIL_AI_TOOL_SERVER, default 127.0.0.1:5571). Envil-CAD executes the call in the
 * open schematic through exactly the same path as its in-app AI panel.
 *
 * No API key involved. Envil-CAD must be running with a schematic open.
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

const TOOLS = [
  {
    name: "add_component",
    description:
      "Place a component symbol into the schematic currently open in Envil-CAD.",
    inputSchema: {
      type: "object",
      properties: {
        lib_id: {
          type: "string",
          description:
            "KiCad library id 'Library:Symbol', e.g. 'Regulator_Linear:AP2112K-3.3', " +
            "'Device:R', 'Device:C', 'power:GND'.",
        },
        reference: { type: "string", description: "Reference designator, e.g. U1, R1, C1." },
        value: { type: "string", description: "Optional value, e.g. '10k'." },
        x_mils: { type: "integer", description: "X position in mils (multiple of 50)." },
        y_mils: { type: "integer", description: "Y position in mils (multiple of 50)." },
      },
      required: ["lib_id", "reference"],
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
    return {
      isError: result.ok === false,
      content: [{ type: "text", text: result.message || JSON.stringify(result) }],
    };
  } catch (e) {
    return { isError: true, content: [{ type: "text", text: e.message }] };
  }
});

const transport = new StdioServerTransport();
await server.connect(transport);
console.error(`envil-mcp ready; bridging to Envil-CAD at ${HOST}:${PORT}`);
