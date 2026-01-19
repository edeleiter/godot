# Implementation Guide

## Overview

This guide provides step-by-step instructions for implementing the Claude module. Follow the phases in order as each builds on the previous.

## Prerequisites

- Godot source code (4.x branch)
- Python 3.8+ with SCons
- C++ compiler (MSVC, GCC, or Clang)
- Claude API key from Anthropic

## Phase 1: Module Skeleton

### Step 1.1: Create Directory Structure

```bash
mkdir -p modules/claude/{api,actions,editor,doc_classes,icons,docs}
```

### Step 1.2: Create config.py

```python
# modules/claude/config.py

def can_build(env, platform):
    """Return True if this module can be built on the given platform."""
    # Desktop platforms only (need editor)
    return platform in ["windows", "linuxbsd", "macos"]


def configure(env):
    """Configure the environment for this module."""
    pass


def get_doc_classes():
    """Return list of classes to generate documentation for."""
    return [
        "ClaudeClient",
        "ClaudeSceneSerializer",
        "ClaudePromptBuilder",
        "ClaudeAction",
        "ClaudeActionParser",
        "ClaudeActionExecutor",
        "ClaudeDock",
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

# Module sources (add files as they're created)
module_sources = []

# API layer
module_sources += Glob("api/*.cpp")

# Action system
module_sources += Glob("actions/*.cpp")

# Editor integration (only when building with tools)
if env.editor_build:
    module_sources += Glob("editor/*.cpp")

# Root files
module_sources += Glob("*.cpp")

env_claude.add_source_files(env.modules_sources, module_sources)
```

### Step 1.4: Create register_types.h

```cpp
// modules/claude/register_types.h

#ifndef CLAUDE_REGISTER_TYPES_H
#define CLAUDE_REGISTER_TYPES_H

#include "modules/register_module_types.h"

void initialize_claude_module(ModuleInitializationLevel p_level);
void uninitialize_claude_module(ModuleInitializationLevel p_level);

#endif // CLAUDE_REGISTER_TYPES_H
```

### Step 1.5: Create register_types.cpp (minimal)

```cpp
// modules/claude/register_types.cpp

#include "register_types.h"
#include "core/object/class_db.h"

void initialize_claude_module(ModuleInitializationLevel p_level) {
    // Will add class registrations as we implement them
}

void uninitialize_claude_module(ModuleInitializationLevel p_level) {
    // Cleanup will be added as needed
}
```

### Step 1.6: Verify Build

```bash
# Windows
scons platform=windows target=editor module_claude_enabled=yes -j8

# Linux
scons platform=linuxbsd target=editor module_claude_enabled=yes -j8

# macOS
scons platform=macos target=editor module_claude_enabled=yes -j8
```

## Phase 2: Core Classes

### Step 2.1: Create ClaudeClient

Create the files from [API_COMMUNICATION.md](API_COMMUNICATION.md):

```
modules/claude/api/claude_client.h
modules/claude/api/claude_client.cpp
```

Update `register_types.cpp`:

```cpp
#include "api/claude_client.h"

void initialize_claude_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(ClaudeClient);
    }
}
```

### Step 2.2: Create ClaudeSceneSerializer

Create:
```
modules/claude/api/claude_scene_serializer.h
modules/claude/api/claude_scene_serializer.cpp
```

Register in `register_types.cpp`.

### Step 2.3: Create ClaudePromptBuilder

Create:
```
modules/claude/api/claude_prompt_builder.h
modules/claude/api/claude_prompt_builder.cpp
```

Register in `register_types.cpp`.

### Step 2.4: Verify Build

```bash
scons platform=windows target=editor module_claude_enabled=yes -j8
```

## Phase 3: Action System

### Step 3.1: Create ClaudeAction

Create the files from [ACTION_SYSTEM.md](ACTION_SYSTEM.md):

```
modules/claude/actions/claude_action.h
modules/claude/actions/claude_action.cpp
```

### Step 3.2: Create ClaudeActionParser

Create:
```
modules/claude/actions/claude_action_parser.h
modules/claude/actions/claude_action_parser.cpp
```

### Step 3.3: Create ClaudeActionExecutor

Create:
```
modules/claude/actions/claude_action_executor.h
modules/claude/actions/claude_action_executor.cpp
```

### Step 3.4: Update register_types.cpp

```cpp
#include "api/claude_client.h"
#include "api/claude_scene_serializer.h"
#include "api/claude_prompt_builder.h"
#include "actions/claude_action.h"
#include "actions/claude_action_parser.h"
#include "actions/claude_action_executor.h"

void initialize_claude_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(ClaudeClient);
        GDREGISTER_CLASS(ClaudeSceneSerializer);
        GDREGISTER_CLASS(ClaudePromptBuilder);
        GDREGISTER_CLASS(ClaudeAction);
        GDREGISTER_CLASS(ClaudeActionParser);
        GDREGISTER_CLASS(ClaudeActionExecutor);
    }
}
```

## Phase 4: Editor Integration

### Step 4.1: Create ClaudeDock

Create the files from [EDITOR_INTEGRATION.md](EDITOR_INTEGRATION.md):

```
modules/claude/editor/claude_dock.h
modules/claude/editor/claude_dock.cpp
```

### Step 4.2: Create ClaudeEditorPlugin

Create:
```
modules/claude/editor/claude_editor_plugin.h
modules/claude/editor/claude_editor_plugin.cpp
```

### Step 4.3: Update register_types.cpp

```cpp
#include "api/claude_client.h"
#include "api/claude_scene_serializer.h"
#include "api/claude_prompt_builder.h"
#include "actions/claude_action.h"
#include "actions/claude_action_parser.h"
#include "actions/claude_action_executor.h"

#ifdef TOOLS_ENABLED
#include "editor/claude_dock.h"
#include "editor/claude_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_claude_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        GDREGISTER_CLASS(ClaudeClient);
        GDREGISTER_CLASS(ClaudeSceneSerializer);
        GDREGISTER_CLASS(ClaudePromptBuilder);
        GDREGISTER_CLASS(ClaudeAction);
        GDREGISTER_CLASS(ClaudeActionParser);
        GDREGISTER_CLASS(ClaudeActionExecutor);
    }

#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        GDREGISTER_CLASS(ClaudeDock);
        GDREGISTER_CLASS(ClaudeEditorPlugin);
        EditorPlugins::add_by_type<ClaudeEditorPlugin>();
    }
#endif
}

void uninitialize_claude_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
    if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
        // EditorPlugins handles cleanup automatically
    }
#endif
}
```

### Step 4.4: Add Icon

Create a 16x16 SVG icon:

```xml
<!-- modules/claude/icons/Claude.svg -->
<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">
  <circle cx="8" cy="8" r="7" fill="#D97706" stroke="#92400E" stroke-width="1"/>
  <text x="8" y="11" text-anchor="middle" fill="white" font-size="8" font-family="sans-serif">C</text>
</svg>
```

## Phase 5: Testing

### Step 5.1: Build and Launch

```bash
# Build
scons platform=windows target=editor module_claude_enabled=yes -j8

# Launch
./bin/godot.windows.editor.x86_64.exe
```

### Step 5.2: Configure API Key

1. Open Editor Settings
2. Navigate to Claude > API
3. Enter your Anthropic API key

### Step 5.3: Basic Functionality Test

1. Create a new scene with a Node3D root
2. Open the Claude dock (should appear in right panel)
3. Type: "Add a Sprite2D named TestSprite"
4. Verify:
   - Response appears in chat
   - Action appears in preview panel
   - Clicking "Apply" adds the node
   - Ctrl+Z undoes the action

### Step 5.4: Context Test

1. Select a node in the scene
2. Check "Selection" checkbox in Claude dock
3. Type: "What node do I have selected?"
4. Verify Claude knows about the selected node

### Step 5.5: Script Test

1. Type: "Create a script for a simple player controller"
2. Verify:
   - Script creation action appears
   - Clicking Apply creates the file
   - Script contains valid GDScript

## Phase 6: Documentation

### Step 6.1: Create XML Documentation

```xml
<!-- modules/claude/doc_classes/ClaudeClient.xml -->
<?xml version="1.0" encoding="UTF-8" ?>
<class name="ClaudeClient" inherits="RefCounted" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="../../../doc/class.xsd">
    <brief_description>
        Handles communication with the Claude AI API.
    </brief_description>
    <description>
        ClaudeClient manages HTTP communication with the Claude API, including authentication, request/response handling, and streaming support.
    </description>
    <tutorials>
    </tutorials>
    <methods>
        <method name="send_message">
            <return type="int" enum="Error" />
            <param index="0" name="message" type="String" />
            <param index="1" name="context" type="Dictionary" />
            <description>
                Sends a message to Claude with the given context.
            </description>
        </method>
        <!-- Add more methods -->
    </methods>
    <signals>
        <signal name="response_complete">
            <param index="0" name="response" type="String" />
            <description>
                Emitted when a complete response is received.
            </description>
        </signal>
        <!-- Add more signals -->
    </signals>
</class>
```

### Step 6.2: Generate Documentation

```bash
# Generate docs from source
./bin/godot.windows.editor.x86_64.exe --doctool doc/classes

# Build HTML docs
cd doc && make html
```

## Troubleshooting

### Module Not Loading

Check:
1. `config.py` returns `True` for your platform
2. No syntax errors in `SCsub`
3. All `#include` paths are correct
4. `register_types.cpp` includes all headers

### Dock Not Appearing

Check:
1. `EditorPlugins::add_by_type` is called
2. `ClaudeEditorPlugin` constructor creates dock
3. `add_dock()` is called on the dock

### API Connection Failed

Check:
1. API key is set in Editor Settings
2. Internet connection is available
3. HTTPS endpoint is correct
4. Check for proxy settings

### Actions Not Executing

Check:
1. EditorUndoRedoManager is available
2. Scene has a root node
3. Action validation passes
4. Check editor output for errors

## File Checklist

After implementation, verify these files exist:

```
modules/claude/
├── config.py                          ✓
├── SCsub                              ✓
├── register_types.h                   ✓
├── register_types.cpp                 ✓
├── api/
│   ├── claude_client.h                ✓
│   ├── claude_client.cpp              ✓
│   ├── claude_scene_serializer.h      ✓
│   ├── claude_scene_serializer.cpp    ✓
│   ├── claude_prompt_builder.h        ✓
│   └── claude_prompt_builder.cpp      ✓
├── actions/
│   ├── claude_action.h                ✓
│   ├── claude_action.cpp              ✓
│   ├── claude_action_parser.h         ✓
│   ├── claude_action_parser.cpp       ✓
│   ├── claude_action_executor.h       ✓
│   └── claude_action_executor.cpp     ✓
├── editor/
│   ├── claude_dock.h                  ✓
│   ├── claude_dock.cpp                ✓
│   ├── claude_editor_plugin.h         ✓
│   └── claude_editor_plugin.cpp       ✓
├── doc_classes/
│   ├── ClaudeClient.xml               ✓
│   ├── ClaudeAction.xml               ✓
│   └── ClaudeDock.xml                 ✓
├── icons/
│   └── Claude.svg                     ✓
└── docs/
    ├── ARCHITECTURE.md                ✓
    ├── API_COMMUNICATION.md           ✓
    ├── ACTION_SYSTEM.md               ✓
    ├── EDITOR_INTEGRATION.md          ✓
    ├── SECURITY.md                    ✓
    └── IMPLEMENTATION_GUIDE.md        ✓
```

## Next Steps

After basic implementation:

1. **Streaming Display**: Show response as it streams in
2. **Conversation History**: Maintain context across messages
3. **Context Menu Integration**: Right-click actions on nodes
4. **Script Editor Integration**: Ask Claude about open scripts
5. **Error Recovery**: Graceful handling of partial failures
6. **Unit Tests**: Test action parsing and execution
7. **Performance**: Optimize scene serialization for large scenes
