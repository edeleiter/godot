# Claude MCP Server Module for Godot

This module integrates Claude AI with the Godot editor via the **Model Context Protocol (MCP)**. It runs a TCP server inside the editor process, exposing Godot editor capabilities as MCP tools that Claude Code can invoke.

## Features

- **MCP Server**: Exposes 32 Godot editor tools as standardized MCP operations
- **Scene Persistence**: Save, create, open, and instance scenes — full game development workflow
- **Scene Manipulation**: Add, remove, and modify nodes with full undo/redo support
- **Signal Wiring**: Connect and disconnect signals between nodes via the editor
- **Project Settings**: Configure window size, physics, input maps, rendering, and more
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

### 3. Install the Claude Code plugin (one-time)

If working in the Godot source repo, the marketplace is auto-discovered:

```bash
# From within Claude Code
/plugin install godot-mcp@godot-plugins

# Or from the CLI
claude plugin install godot-mcp@godot-plugins --scope user
```

This installs the MCP bridge, game development skill, and all 32 tools.

**Alternative (manual MCP config):**

Add to your MCP settings (`~/.claude.json` or VS Code settings):
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

### Scene Manipulation
| Tool | Description |
|------|-------------|
| `godot_get_scene_tree` | Get current scene structure as JSON |
| `godot_add_node` | Add a new node to the scene tree |
| `godot_remove_node` | Remove a node from the scene tree |
| `godot_set_property` | Set a property on a node |
| `godot_get_property` | Get a property value from a node |

### Scene Persistence
| Tool | Description |
|------|-------------|
| `godot_save_scene` | Save current scene (or "save as" with path) |
| `godot_new_scene` | Create empty scene with typed root |
| `godot_open_scene` | Open existing scene file |
| `godot_instance_scene` | Instance a PackedScene as child (prefab pattern) |

### Signals
| Tool | Description |
|------|-------------|
| `godot_connect_signal` | Connect signal between nodes |
| `godot_disconnect_signal` | Disconnect signal connection |

### Scripts
| Tool | Description |
|------|-------------|
| `godot_create_script` | Create a new GDScript file |
| `godot_read_script` | Read script content |
| `godot_modify_script` | Modify an existing script |

### Project Settings
| Tool | Description |
|------|-------------|
| `godot_project_settings` | Get/set/list project settings |

### Selection
| Tool | Description |
|------|-------------|
| `godot_get_selected_nodes` | Get currently selected nodes |
| `godot_select_nodes` | Select nodes in the editor |

### Input
| Tool | Description |
|------|-------------|
| `godot_input_map` | Add/remove input actions and key/button/axis bindings |

### Introspection
| Tool | Description |
|------|-------------|
| `godot_get_class_info` | Query ClassDB for class properties, methods, signals, enums |
| `godot_get_node_info` | Full node inspector: all properties with current values |

### Batch Operations
| Tool | Description |
|------|-------------|
| `godot_set_properties_batch` | Set multiple properties in one undo action |

### Resource
| Tool | Description |
|------|-------------|
| `godot_project_files` | List project files or trigger filesystem rescan |

### 3D
| Tool | Description |
|------|-------------|
| `godot_bake_navigation` | Bake navigation mesh on a NavigationRegion3D |

### Animation
| Tool | Description |
|------|-------------|
| `godot_create_animation` | Create animation with tracks and keyframes |
| `godot_get_animation_info` | Inspect animations, tracks, and state machines |

### Runtime (requires running game)
| Tool | Description |
|------|-------------|
| `godot_run_scene` | Run the scene |
| `godot_stop_scene` | Stop running scene |
| `godot_get_runtime_scene_tree` | Get scene tree from running game |
| `godot_get_runtime_output` | Get log/print output from running game |
| `godot_capture_screenshot` | Capture screenshot from running game |
| `godot_runtime_camera_control` | Control debug camera in running game |
| `godot_get_runtime_camera_info` | Get camera state from running game |

See [TOOL_REFERENCE.md](docs/TOOL_REFERENCE.md) for full parameter details, return values, and error messages.

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

## Type Coercion

When setting properties, JSON values are automatically converted to Godot types:

| JSON | Godot Type |
|------|------------|
| `{"x": 1, "y": 2}` | `Vector2` / `Vector2i` |
| `{"x": 1, "y": 2, "z": 3}` | `Vector3` / `Vector3i` |
| `{"x": 1, "y": 2, "z": 3, "w": 4}` | `Vector4` |
| `{"r": 1, "g": 0.5, "b": 0, "a": 1}` | `Color` |
| `{"x": 0, "y": 0, "width": 100, "height": 50}` | `Rect2` / `Rect2i` |
| `"BoxMesh"` | Resource instance (from allowlist) |
| `{"_type": "BoxMesh", "size": {...}}` | Resource instance with properties |

Only resource types on the security allowlist can be instantiated. See [TOOL_REFERENCE.md](docs/TOOL_REFERENCE.md) for the full conversion table and allowed resource types.

## Directory Structure

```
modules/claude/
├── .claude-plugin/
│   ├── plugin.json              # Claude Code plugin manifest
│   └── marketplace.json         # Local marketplace manifest
├── .mcp.json                    # MCP bridge auto-start config
├── config.py                    # Build configuration
├── SCsub                        # SCons build rules
├── register_types.cpp/h         # Type registration
├── mcp/
│   ├── godot_mcp_server.*           # MCP protocol server (TCP)
│   ├── godot_mcp_tools_schema.cpp   # Tool definitions & parameter schemas
│   ├── godot_mcp_tools_scene.cpp    # Scene, property, selection, persistence, and instancing tools
│   ├── godot_mcp_tools_script.cpp   # Script create/read/modify tools
│   ├── godot_mcp_tools_signals.cpp  # Signal connect/disconnect tools
│   ├── godot_mcp_tools_project.cpp  # Project settings and input map tools
│   ├── godot_mcp_tools_runtime.cpp  # Runtime, screenshot, and camera tools
│   ├── godot_mcp_tools_3d.cpp       # Navigation mesh baking tool
│   ├── godot_mcp_tools_animation.cpp # Animation creation and inspection tools
│   ├── godot_mcp_tools_resource.cpp # Project file listing and filesystem scan
│   └── godot_mcp_validation.cpp     # Path/type validation & type coercion
├── util/
│   └── mcp_scene_serializer.*   # Scene to JSON
├── editor/
│   ├── claude_mcp_dock.*        # Status dock
│   └── claude_editor_plugin.*   # Plugin (lifecycle + polling)
├── bridge/
│   └── claude_mcp_bridge.py     # Stdio-to-TCP bridge
├── skills/
│   └── godot-game-dev/          # Game development skill (recipes & patterns)
├── doc_classes/                  # XML class documentation
│   ├── GodotMCPServer.xml
│   ├── MCPSceneSerializer.xml
│   ├── ClaudeMCPDock.xml
│   └── ClaudeEditorPlugin.xml
└── docs/
    ├── TOOL_REFERENCE.md        # Full tool API reference
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
