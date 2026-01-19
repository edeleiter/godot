# Claude Code Integration Module for Godot

This module integrates Claude AI as a first-class assistant in the Godot editor, matching the experience of the Claude Code VS Code extension.

## Features

- **OAuth Authentication**: Sign in via browser, no API key management needed
- **Streaming Responses**: See Claude's responses as they're generated
- **Permission Modes**: Toggle between Ask/Auto/Plan modes with Shift+Tab
- **Scene Manipulation**: Claude can add, remove, and modify nodes with full undo/redo
- **Script Generation**: Claude can create and attach GDScript files
- **Context Awareness**: Claude understands your scene structure and selected nodes

## Quick Start

1. Build Godot with the module:
   ```bash
   scons platform=windows module_claude_enabled=yes
   ```

2. Launch the editor and find the Claude dock in the right panel

3. Click "Sign In" to authenticate via your browser

4. Start chatting with Claude about your project!

## Permission Modes

Toggle between modes with **Shift+Tab**:

| Mode | Behavior |
|------|----------|
| **ASK** | Claude proposes changes, you approve before execution |
| **AUTO** | Changes apply automatically (prompts for high-risk) |
| **PLAN** | Claude creates a detailed plan before any execution |

## Documentation

### Core Documentation

- [Architecture Overview](docs/ARCHITECTURE.md) - Component diagrams, data flow, layer responsibilities
- [Authentication](docs/AUTHENTICATION.md) - OAuth flow, token storage, security
- [Permission Modes](docs/PERMISSION_MODES.md) - Ask/Auto/Plan modes, Shift+Tab toggle

### Implementation Details

- [API Communication](docs/API_COMMUNICATION.md) - ClaudeClient, streaming, scene serialization
- [Action System](docs/ACTION_SYSTEM.md) - Action types, parsing, execution with undo/redo
- [Editor Integration](docs/EDITOR_INTEGRATION.md) - ClaudeDock UI, plugin registration
- [Security](docs/SECURITY.md) - Token encryption, path restrictions, safety checks
- [Implementation Guide](docs/IMPLEMENTATION_GUIDE.md) - Step-by-step build instructions

## Directory Structure

```
modules/claude/
├── config.py                 # Module build configuration
├── SCsub                     # SCons build rules
├── register_types.cpp/.h     # Type registration
├── api/
│   ├── claude_auth.*         # OAuth authentication
│   ├── claude_client.*       # HTTP/streaming API
│   ├── claude_scene_serializer.*  # Scene to JSON
│   └── claude_prompt_builder.*    # System prompts
├── actions/
│   ├── claude_action.*       # Action data structures
│   ├── claude_action_parser.*    # Parse Claude responses
│   └── claude_action_executor.*  # Execute with undo/redo
├── editor/
│   ├── claude_dock.*         # UI dock panel
│   └── claude_editor_plugin.*    # Plugin wrapper
├── doc_classes/              # XML documentation
├── docs/                     # Detailed documentation
└── icons/                    # Editor icons
```

## Requirements

- Godot 4.x source code
- Python 3.8+ with SCons
- Internet connection for OAuth and API calls

## Configuration

After building, optional settings are available in Editor Settings:

| Setting | Description |
|---------|-------------|
| `claude/behavior/permission_mode` | Default mode: Ask, Auto, or Plan |
| `claude/behavior/auto_apply_threshold` | Risk level for auto-apply |
| `claude/context/max_nodes` | Max nodes to include in context |
| `claude/api/key` | Fallback API key (for enterprise/offline) |

## Comparison with VS Code Extension

| Feature | VS Code | Godot |
|---------|---------|-------|
| OAuth sign-in | ✓ | ✓ |
| Streaming responses | ✓ | ✓ |
| Permission toggle (Shift+Tab) | ✓ | ✓ |
| Ask/Auto/Plan modes | ✓ | ✓ |
| File editing | ✓ | ✓ (scripts) |
| Scene manipulation | - | ✓ |
| Node creation | - | ✓ |
| Undo/redo integration | ✓ | ✓ |
