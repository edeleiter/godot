# Claude MCP Server Module for Godot

This module integrates Claude AI with the Godot editor via the **Model Context Protocol (MCP)**. It runs a TCP server inside the editor process, exposing Godot editor capabilities as MCP tools that Claude Code can invoke.

## Features

- **MCP Server**: Exposes Godot editor actions as standardized MCP tools
- **Scene Manipulation**: Add, remove, and modify nodes with full undo/redo support
- **Script Generation**: Create and edit GDScript files
- **Context Awareness**: Claude understands your scene structure
- **Works with Claude Code**: Use the Claude Code CLI or VS Code extension
- **Type Coercion**: Automatically converts JSON dictionaries to Vector2, Vector3, Color, etc.

## Quick Start

### 1. Build Godot with the module

```bash
scons platform=windows module_claude_enabled=yes
```

### 2. Launch the editor

Open your Godot project. Find the "Claude MCP" dock (right panel) and click
**Start** to launch the MCP server on port 6009. Alternatively, enable
autostart in Editor > Editor Settings > Network > Claude MCP.

### 3. Configure Claude Code

Add to your Claude Code MCP settings (`~/.claude/mcp.json` or VS Code settings):

```json
{
  "mcpServers": {
    "godot": {
      "command": "python",
      "args": ["/path/to/godot/modules/claude/bridge/claude_mcp_bridge.py", "--port", "6009"]
    }
  }
}
```

### 4. Use Claude Code with your Godot project

```bash
cd /path/to/your/godot/project
claude

> Add a player character with WASD movement to my scene
```

## Available MCP Tools

| Tool | Description |
|------|-------------|
| `godot_get_scene_tree` | Get current scene structure as JSON |
| `godot_add_node` | Add a new node to the scene tree |
| `godot_remove_node` | Remove a node from the scene tree |
| `godot_set_property` | Set a property on a node |
| `godot_get_property` | Get a property value from a node |
| `godot_create_script` | Create a new GDScript file |
| `godot_read_script` | Read script content |
| `godot_modify_script` | Modify an existing script |
| `godot_get_selected_nodes` | Get currently selected nodes |
| `godot_select_nodes` | Select nodes in the editor |
| `godot_run_scene` | Run the scene |
| `godot_stop_scene` | Stop running scene |
| `godot_get_runtime_scene_tree` | Get scene tree from running game |
| `godot_get_runtime_output` | Get log/print output from running game |
| `godot_capture_screenshot` | Capture screenshot from running game |

## Architecture

```
┌─────────────────────┐         ┌────────────────────────────────┐
│   Claude Code CLI   │         │        Godot Editor            │
│   or VS Code Ext    │         │                                │
│                     │  stdio  │  ┌──────────────────────────┐  │
│  ┌───────────────┐  │◄───────►│  │    MCP Server (TCP)      │  │
│  │  MCP Client   │  │         │  │    port 6009             │  │
│  └───────────────┘  │         │  │                          │  │
│         │           │         │  │  - Tool definitions      │  │
│         ▼           │  bridge │  │  - Scene serialization   │  │
│  ┌───────────────┐  │  script │  │  - Undo/redo integration │  │
│  │  Claude API   │  │         │  └──────────────────────────┘  │
│  └───────────────┘  │         │                                │
└─────────────────────┘         └────────────────────────────────┘
         ▲
         │ stdio
         ▼
┌─────────────────────┐
│  claude_mcp_bridge  │
│  (Python, TCP↔stdio)│
└─────────────────────┘
```

## Editor Settings

Configure via Editor > Editor Settings > Network > Claude MCP:

| Setting | Default | Description |
|---------|---------|-------------|
| `port` | 6009 | TCP port for the MCP server |
| `host` | 127.0.0.1 | Bind address (localhost only by default) |
| `autostart` | false | Start server when editor opens |

## Editor Dock

The module includes a status dock (Claude MCP) in the editor that shows:
- MCP server status and port
- Connected client count
- Configuration instructions with copy button
- Recent tool call log

## Directory Structure

```
modules/claude/
├── config.py              # Build configuration
├── SCsub                  # SCons build rules
├── register_types.cpp/h   # Type registration
├── mcp/
│   └── godot_mcp_server.* # MCP protocol server (TCP)
├── util/
│   └── mcp_scene_serializer.* # Scene to JSON
├── editor/
│   ├── claude_mcp_dock.*      # Status dock
│   └── claude_editor_plugin.* # Plugin (lifecycle + polling)
├── bridge/
│   └── claude_mcp_bridge.py   # Stdio-to-TCP bridge
└── docs/
    ├── IMPLEMENTATION_GUIDE.md  # Build & development guide
    ├── MCP_SERVER.md            # Protocol details
    └── SECURITY.md              # Security model
```

## Requirements

- Godot 4.x source code
- Python 3.8+ with SCons (build time)
- Python 3.x (runtime, for bridge script)
- Claude Code CLI or VS Code extension

## Security

- All file operations are restricted to `res://` paths
- Node operations are protected by undo/redo
- Cannot modify editor internals
- Cannot remove scene root
- TCP server binds to localhost only by default
- Max 4 simultaneous clients
- 4MB message size limit

See [SECURITY.md](docs/SECURITY.md) for details.
