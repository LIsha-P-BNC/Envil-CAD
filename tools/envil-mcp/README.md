# envil-mcp — connect your own Claude client to Envil-CAD

Envil-CAD gives you **two ways** to use AI. Pick whichever fits:

| Method | Who pays | Where you chat | Setup |
|---|---|---|---|
| **A. In-app AI panel** | Your Anthropic **API key** (credits) | Inside Envil-CAD (Anvil panel) | Set `CLAUDE_API_KEY` |
| **B. MCP** (this tool) | Your **Claude subscription** (Desktop / Code / Cursor) | Your own Claude client | Add the config below |

Method B needs no API key: your Claude client provides the model, and Envil-CAD
exposes its schematic tools over MCP. Envil-CAD executes each tool in the open
schematic through the exact same path as the in-app panel.

## How it works

```
Your Claude client ──stdio──▶ envil-mcp (this) ──127.0.0.1:5571──▶ Envil-CAD ──▶ schematic
   (your subscription)         (Node bridge)      (tool socket)       (KIWAY mail)
```

Envil-CAD starts the loopback tool socket automatically on launch (port 5571,
override with the `ENVIL_MCP_PORT` env var). **Envil-CAD must be running with a
schematic open** for tool calls to place parts.

## One-time install

```sh
cd tools/envil-mcp
npm install
```

## Connect Claude Code (CLI)

```sh
claude mcp add envil-cad -- node D:/Ki_Cad_Full/Envil-CAD/tools/envil-mcp/index.mjs
```

## Connect Claude Desktop / Cursor

Add to the client's MCP config (Claude Desktop:
`%APPDATA%\Claude\claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "envil-cad": {
      "command": "node",
      "args": ["D:/Ki_Cad_Full/Envil-CAD/tools/envil-mcp/index.mjs"]
    }
  }
}
```

Restart the client. You should see an **envil-cad** tool group with `add_component`.

## Try it

With Envil-CAD open on a schematic, tell your Claude client:

> Using envil-cad, add a 10k resistor R1 at 1000,1000.

It calls `add_component` and the part appears on your sheet.

## Tools

| Tool | Does |
|---|---|
| `get_schematic` | **Read** the sheet: every symbol + each pin's number, name, absolute `x_mils`/`y_mils` |
| `add_component` | Place a symbol (`lib_id`, `reference`, optional `value`, `x_mils`, `y_mils`) |
| `add_wire` | Draw a wire path (`points`: list of `[x_mils, y_mils]`, one segment per pair) |
| `add_label` | Name a net (`name`, `x_mils`, `y_mils`, `kind`: label/global/hier) |
| `add_junction` | Junction dot at (`x_mils`, `y_mils`) |
| `add_no_connect` | No-connect X at (`x_mils`, `y_mils`) |
| `edit_value` | Change a placed symbol's value (`reference`, `new_value`) |
| `move_component` | Move a placed symbol (`reference`, `x_mils`, `y_mils`) |
| `delete_component` | Delete a placed symbol (`reference`) |

Wiring flow: call `get_schematic` first to read each pin's exact position, then
route `add_wire` endpoints to those coordinates so wires land on pins.

More tools (nets, ERC, export) can be added the same way: expose them here and
handle them in Envil-CAD's `EnvilExecAiTool`.

## Troubleshooting

- **"Cannot reach Envil-CAD on 127.0.0.1:5571"** — Envil-CAD isn't running, or a
  firewall blocks loopback. Launch Envil-CAD first.
- **"No schematic editor is open"** — open the Schematic Editor and try again.
- **Port clash** — set `ENVIL_MCP_PORT` for both Envil-CAD and this bridge.
