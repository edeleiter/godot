# Implementation Guide

## Overview

This guide provides step-by-step instructions for implementing the Claude MCP module. The module exposes Godot editor capabilities via the Model Context Protocol (MCP).

## Prerequisites

- Godot source code (4.x branch)
- Python 3.8+ with SCons
- C++ compiler (MSVC, GCC, or Clang)
- Claude Code CLI or VS Code extension (for testing)

## Phase 1: Module Skeleton

### Step 1.1: Create Directory Structure

```bash
mkdir -p modules/claude/{mcp,util,editor,doc_classes,docs}
```

### Step 1.2: Create config.py

```python
# modules/claude/config.py

def can_build(env, platform):
    """Return True if this module can be built on the given platform."""
    # Desktop platforms only - need editor for MCP server
    return platform in ["windows", "linuxbsd", "macos"]


def configure(env):
    """Configure the environment for this module."""
    pass


def get_doc_classes():
    """Return list of classes to generate documentation for."""
    return [
        "GodotMCPServer",
        "MCPSceneSerializer",
        "ClaudeMCPDock",
        "ClaudeEditorPlugin",
    ]


def get_doc_path():
    """Return path to documentation XML files."""
    return "doc_classes"
```

### Step 1.3: Create SCsub

```python
# modules/claude/SCsub

Import("env")
Import("env_modules")

env_claude = env_modules.Clone()

module_sources = []

# MCP server
module_sources += Glob("mcp/*.cpp")

# Utilities
module_sources += Glob("util/*.cpp")

# Editor integration (only when building with tools/editor)
if env.editor_build:
    module_sources += Glob("editor/*.cpp")

# Root registration files
module_sources += Glob("*.cpp")

env_claude.add_source_files(env.modules_sources, module_sources)
```

### Step 1.4: Create register_types files

See actual implementation in `register_types.h` and `register_types.cpp`.

### Step 1.5: Verify Build

```bash
# Windows
scons platform=windows target=editor module_claude_enabled=yes -j8

# Linux
scons platform=linuxbsd target=editor module_claude_enabled=yes -j8

# macOS
scons platform=macos target=editor module_claude_enabled=yes -j8
```

## Phase 2: MCP Server Core

### Step 2.1: Create GodotMCPServer

Create:

```text
modules/claude/mcp/godot_mcp_server.h
modules/claude/mcp/godot_mcp_server.cpp
```

The server implements:

- TCP server on localhost (default port 6009)
- MCP protocol handling (JSON-RPC over newline-delimited TCP)
- `initialize` - Server capability declaration
- `tools/list` - Expose available tools
- `tools/call` - Execute tool requests
- `resources/list` - List available resources
- `resources/read` - Read resource content

### Step 2.2: Create Python Bridge

Create `bridge/claude_mcp_bridge.py` to relay between Claude Code's stdio and the TCP server.

### Step 2.3: Verify MCP Protocol

Start the editor with the MCP server enabled, then test with the bridge:

```bash
python modules/claude/bridge/claude_mcp_bridge.py --port 6009
```

## Phase 3: Scene Serializer

### Step 3.1: Create MCPSceneSerializer

Create:

```text
modules/claude/util/mcp_scene_serializer.h
modules/claude/util/mcp_scene_serializer.cpp
```

Features:

- Serialize scene tree to JSON
- Property filtering (security blacklist)
- Configurable detail levels (Minimal, Standard, Full)
- Depth and node count limits

## Phase 4: MCP Tools

Implement these tools in `GodotMCPServer`:

| Tool | Priority | Description |
|------|----------|-------------|
| `godot_get_scene_tree` | P0 | Get scene structure |
| `godot_add_node` | P0 | Add node with undo/redo |
| `godot_remove_node` | P0 | Remove node with undo/redo |
| `godot_set_property` | P0 | Set property with undo/redo |
| `godot_get_property` | P1 | Get property value |
| `godot_create_script` | P1 | Create GDScript file |
| `godot_read_script` | P1 | Read script content |
| `godot_modify_script` | P2 | Edit existing script |
| `godot_get_selected_nodes` | P2 | Get selection |
| `godot_select_nodes` | P2 | Set selection |
| `godot_run_scene` | P2 | Run scene |
| `godot_stop_scene` | P2 | Stop scene |
| `godot_get_runtime_scene_tree` | P2 | Get runtime scene tree |
| `godot_get_runtime_output` | P2 | Get runtime log output |
| `godot_capture_screenshot` | P2 | Capture game screenshot |
| `godot_runtime_camera_control` | P2 | Control debug camera in running game |
| `godot_get_runtime_camera_info` | P2 | Get camera state from running game |
| `godot_validate_script` | P2 | Validate GDScript for compilation errors |
| `godot_get_runtime_errors` | P2 | Get structured runtime errors with call stacks |
| `godot_get_editor_log` | P2 | Get editor Output panel messages |

## Phase 5: Editor Integration

### Step 5.1: Create ClaudeMCPDock

A minimal status dock showing:

- MCP server status (running/stopped)
- Start/stop button
- Configuration instructions
- Recent tool call log

### Step 5.2: Create ClaudeEditorPlugin

Plugin wrapper that:

- Creates MCP server instance
- Adds dock to editor
- Manages lifecycle

## Phase 6: Testing

### Step 6.1: Configure Claude Code

Add to your Claude Code MCP settings:

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

### Step 6.2: Basic Test

```bash
cd /path/to/godot/project
claude

> What's in my scene?
> Add a CharacterBody3D named Player
> Create a movement script for the player
```

### Step 6.3: Verify

- [ ] Scene tree is returned correctly
- [ ] Nodes can be added/removed
- [ ] Properties can be get/set
- [ ] Scripts can be created
- [ ] Undo/redo works
- [ ] Selection works

## Troubleshooting

### Module Not Loading

Check:

1. `config.py` returns `True` for your platform
2. No syntax errors in `SCsub`
3. All `#include` paths are correct
4. `register_types.cpp` includes all headers

### MCP Server Not Responding

Check:

1. Server is started in the editor (check dock status)
2. Bridge script can connect to the port (`python claude_mcp_bridge.py --port 6009`)
3. Port is not blocked by firewall or another process

### Tools Not Working

Check:

1. EditorUndoRedoManager is available
2. Scene has a root node
3. Path validation passes
4. Check editor output for errors

## File Checklist

```text
modules/claude/
├── config.py                             ✓
├── SCsub                                 ✓
├── register_types.h                      ✓
├── register_types.cpp                    ✓
├── bridge/
│   └── claude_mcp_bridge.py              ✓
├── mcp/
│   ├── godot_mcp_server.h                ✓
│   ├── godot_mcp_server.cpp              ✓
│   ├── godot_mcp_tools_schema.cpp        ✓
│   ├── godot_mcp_tools_scene.cpp         ✓
│   ├── godot_mcp_tools_script.cpp        ✓
│   ├── godot_mcp_tools_runtime.cpp       ✓
│   ├── godot_mcp_tools_editor.cpp        ✓
│   └── godot_mcp_validation.cpp          ✓
├── util/
│   ├── mcp_scene_serializer.h            ✓
│   └── mcp_scene_serializer.cpp          ✓
├── editor/
│   ├── claude_mcp_dock.h                 ✓
│   ├── claude_mcp_dock.cpp               ✓
│   ├── claude_editor_plugin.h            ✓
│   └── claude_editor_plugin.cpp          ✓
├── doc_classes/
│   ├── GodotMCPServer.xml                ✓
│   ├── MCPSceneSerializer.xml            ✓
│   ├── ClaudeMCPDock.xml                 ✓
│   └── ClaudeEditorPlugin.xml            ✓
└── docs/
    ├── TOOL_REFERENCE.md                 ✓
    ├── MCP_SERVER.md                     ✓
    ├── SECURITY.md                       ✓
    └── IMPLEMENTATION_GUIDE.md           ✓
```

## Next Steps

After basic implementation:

1. **TCP Authentication**: Add token-based handshake for local security
2. **Resource Subscriptions**: Notify on scene changes
3. **Additional Tools**: Duplicate, reparent, connect signals
4. **Performance**: Optimize scene serialization for large scenes
5. **Unit Tests**: Test tool implementations
