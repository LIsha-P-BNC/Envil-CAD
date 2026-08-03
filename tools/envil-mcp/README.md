# envil-mcp — connect your own Claude client to Envil-CAD

Envil-CAD gives you **two ways** to use AI. Pick whichever fits:

| Method | Who pays | Where you chat | Setup |
|---|---|---|---|
| **A. In-app AI panel** | Your Anthropic **API key** (credits) | Inside Envil-CAD (Anvil panel) | Set `CLAUDE_API_KEY` |
| **B. MCP** (this tool) | Your **Claude subscription** (Desktop / Code / Cursor) | Your own Claude client | Add the config below |

Method B needs no API key: your Claude client provides the model, and Envil-CAD
exposes its schematic **and board** tools over MCP. Envil-CAD executes each tool
in the open document through the exact same path as the in-app panel.

## How it works

```
Your Claude client ──stdio──▶ envil-mcp (this) ──127.0.0.1:5571──▶ Envil-CAD ──▶ schematic
   (your subscription)         (Node bridge)      (tool socket)       (KIWAY mail)  or board
```

Envil-CAD starts the loopback tool socket automatically on launch (port 5571,
override with the `ENVIL_MCP_PORT` env var). It routes each call to the schematic
or the board editor **by tool name**, so a mixed session works without switching
anything. **Envil-CAD must be running with the relevant document open** — the
schematic for `add_component`, the board for `add_track`.

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

## Schematic tools

| Tool | Does |
|---|---|
| `get_schematic` | **Read** the sheet: every symbol + each pin's number, name, absolute `x_mils`/`y_mils` |
| `run_erc` | Run the Electrical Rules Check; returns violations + counts + a `clean` flag |
| `snap_to_grid` | Pull off-grid pins and wire ends onto the connection grid, carrying connections along |
| `annotate` | Assign reference designators to un-annotated symbols |
| `add_component` | Place a symbol (`lib_id`, `reference`, optional `value`, `x_mils`, `y_mils`) |
| `add_wire` | Draw a wire path (`points`: list of `[x_mils, y_mils]`, one segment per pair) |
| `add_label` | Name a net (`name`, `x_mils`, `y_mils`, `kind`: label/global/hier) |
| `add_junction` | Junction dot at (`x_mils`, `y_mils`) |
| `add_no_connect` | No-connect X at (`x_mils`, `y_mils`) |
| `edit_value` | Change a placed symbol's value (`reference`, `new_value`) |
| `move_component` | Move a placed symbol (`reference`, `x_mils`, `y_mils`) |
| `delete_component` | Delete a placed symbol (`reference`) |
| `delete_at` | Delete a stray wire/label/junction/no-connect near a point |

Wiring flow: call `get_schematic` first to read each pin's exact position, then
route `add_wire` endpoints to those coordinates so wires land on pins.

## Board tools

These go to the PCB editor. Positions are in mils, but unlike the schematic they
are not restricted to the 50-mil connection grid.

| Tool | Does |
|---|---|
| `get_board` | **Read** the board: footprints with reference/value/fpid/layer/rotation + mil position, board extents, copper layer names, net names |
| `run_drc` | Run the Design Rules Check; returns violations + counts + a `clean` flag |
| `add_footprint` | Place a footprint from the libraries (`fpid`, `reference`, `value`, `x_mils`, `y_mils`, `rotation_deg`, `layer`) |
| `move_footprint` | Move / rotate / flip a placed footprint (`reference`, `x_mils`, `y_mils`, `rotation_deg`, `flip`) |
| `add_track` | Route copper along `points`, on a `layer` and `net`, optional `width_mils` |
| `add_via` | Add a through via at (`x_mils`, `y_mils`) on a `net` |
| `delete_track_at` | Rip up the tracks/vias that end near a point |
| `set_text_variable` | Define a project text variable (`name`, `value`) — clears "Unresolved text variable" |
| `capture_footprints` | Harvest the board's footprints into a project library and register it — clears "Footprint not found in libraries" |

Layout flow: `get_board` for real coordinates → `move_footprint` / `add_track` →
`run_drc` → fix the violations → `run_drc` again until clean.

More tools can be added the same way: expose them here, handle them in
`EnvilExecAiTool` (schematic) or `EnvilExecPcbTool` (board), and — for board
tools — add the name to `EnvilIsBoardTool` so the bridge routes them.

## Troubleshooting

- **"Cannot reach Envil-CAD on 127.0.0.1:5571"** — Envil-CAD isn't running, or a
  firewall blocks loopback. Launch Envil-CAD first.
- **"No schematic editor is open" / "No board editor is open"** — open that
  editor and try again.
- **Port clash** — set `ENVIL_MCP_PORT` for both Envil-CAD and this bridge.
