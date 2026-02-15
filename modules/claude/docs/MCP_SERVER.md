# MCP Server Architecture

## Overview

The Godot-Claude integration uses the **Model Context Protocol (MCP)** to expose Godot editor capabilities as tools that Claude can invoke. The module runs a TCP server inside the editor that communicates with a Python bridge process which translates between MCP's stdio protocol and TCP.

## Architecture

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                           User's Machine                                 │
│                                                                          │
│  ┌────────────────────┐         ┌────────────────────────────────────┐  │
│  │   Claude Code CLI  │         │          Godot Editor              │  │
│  │                    │         │                                    │  │
│  │  ┌──────────────┐  │  stdio  │  ┌──────────────────────────────┐  │  │
│  │  │ MCP Client   │◄─┼────────┼──┤  Python Bridge (TCP↔stdio)   │  │  │
│  │  └──────────────┘  │         │  └──────────────┬───────────────┘  │  │
│  │                    │         │                 │ TCP 127.0.0.1     │  │
│  └────────────────────┘         │                 ▼                   │  │
│                                 │  ┌──────────────────────────────┐  │  │
│                                 │  │      GodotMCPServer          │  │  │
│                                 │  │  (JSON-RPC over TCP)         │  │  │
│                                 │  │                              │  │  │
│                                 │  │  Tools:                      │  │  │
│                                 │  │  - Scene tree operations     │  │  │
│                                 │  │  - Node manipulation         │  │  │
│                                 │  │  - Script management         │  │  │
│                                 │  │  - Selection & execution     │  │  │
│                                 │  └──────────────┬───────────────┘  │  │
│                                 │                 ▼                   │  │
│                                 │  ┌──────────────────────────────┐  │  │
│                                 │  │     Godot Editor Core        │  │  │
│                                 │  │  - EditorInterface           │  │  │
│                                 │  │  - EditorUndoRedoManager     │  │  │
│                                 │  │  - SceneTree                 │  │  │
│                                 │  │  - EditorFileSystem          │  │  │
│                                 │  └──────────────────────────────┘  │  │
│                                 └────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

## Transport

The server uses **TCP on localhost** (default port 6009, max 4 clients). The Python bridge (`bridge/claude_mcp_bridge.py`) relays newline-delimited JSON between Claude Code's stdin/stdout and the TCP socket.

This design exists because:
- MCP requires stdio-based communication (client spawns subprocess)
- Godot's editor is single-threaded for scene/undo operations
- The TCP server processes requests on the main thread during `poll()`

## Claude Code Configuration

Add to your MCP settings (shown in the editor dock):

```json
{
  "mcpServers": {
    "godot": {
      "command": "python",
      "args": ["path/to/modules/claude/bridge/claude_mcp_bridge.py", "--port", "6009"]
    }
  }
}
```

## Available Tools

| Tool | Description | Risk |
|------|-------------|------|
| `godot_get_scene_tree` | Get scene tree as JSON | Read-only |
| `godot_add_node` | Add node to scene | Undo/redo |
| `godot_remove_node` | Remove node from scene | Undo/redo |
| `godot_set_property` | Set node property | Undo/redo |
| `godot_get_property` | Get node property value | Read-only |
| `godot_set_properties_batch` | Set multiple properties in one undo action | Undo/redo |
| `godot_save_scene` | Save current scene (or "save as" with path) | Disk write, path validation |
| `godot_new_scene` | Create empty scene with typed root | Undo/redo |
| `godot_open_scene` | Open existing scene file | Path validation |
| `godot_instance_scene` | Instance a PackedScene as child node | Undo/redo |
| `godot_create_script` | Create GDScript file | Undo/redo |
| `godot_read_script` | Read script content | Read-only |
| `godot_modify_script` | Modify existing script | Undo/redo |
| `godot_validate_script` | Validate GDScript for compilation errors | Read-only |
| `godot_connect_signal` | Connect signal between nodes | Undo/redo |
| `godot_disconnect_signal` | Disconnect signal connection | Undo/redo |
| `godot_project_settings` | Get/set/list project settings | Undo/redo |
| `godot_input_map` | Add/remove input actions and bindings | Undo/redo |
| `godot_get_class_info` | Query ClassDB for class properties, methods, signals | Read-only |
| `godot_get_node_info` | Full node inspector with all property values | Read-only |
| `godot_get_selected_nodes` | Get editor selection | Read-only |
| `godot_select_nodes` | Set editor selection | UI-only |
| `godot_project_files` | List project files, rescan filesystem, import diagnostics | Read-only |
| `godot_bake_navigation` | Bake navigation mesh on NavigationRegion3D | Undo/redo |
| `godot_create_animation` | Create animation with tracks and keyframes | Undo/redo |
| `godot_get_animation_info` | Inspect animations, tracks, and state machines | Read-only |
| `godot_transform_nodes` | Translate, rotate, scale, or set transform on nodes | Undo/redo |
| `godot_scene_operations` | Duplicate, reparent, set visibility, toggle lock, groups | Undo/redo |
| `godot_run_scene` | Run scene in editor | Uses editor run system |
| `godot_stop_scene` | Stop running scene | Uses editor run system |
| `godot_get_runtime_scene_tree` | Get running game scene tree | Read-only |
| `godot_get_runtime_output` | Get game output/log messages | Read-only |
| `godot_get_runtime_errors` | Get structured runtime errors with call stacks | Read-only |
| `godot_capture_screenshot` | Capture running game screenshot | Read-only |
| `godot_runtime_camera_control` | Control debug camera in running game | Low (camera override) |
| `godot_get_runtime_camera_info` | Get camera override state from running game | Read-only |
| `godot_get_editor_log` | Get editor Output panel messages | Read-only |
| `godot_editor_screenshot` | Capture editor viewport screenshot | Read-only |
| `godot_editor_viewport_camera` | Control 3D editor viewport camera | Low (editor camera) |
| `godot_editor_control` | Switch editor panels, set display mode, toggle grid | UI-only |
| `godot_canvas_view` | Control 2D canvas editor view | UI-only |
| `godot_editor_state` | Get comprehensive editor state | Read-only |

## Resources

The server exposes one MCP resource:
- `godot://scene/current` - The current scene tree as JSON

## Editor Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `network/claude_mcp/port` | 6009 | TCP port for the MCP server |
| `network/claude_mcp/host` | 127.0.0.1 | Bind address (localhost only) |
| `network/claude_mcp/autostart` | false | Start server on editor launch |
