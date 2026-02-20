# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git Policy

**The user handles ALL git actions.** Never run `git commit`, `git push`, `git add`, or any other git write command. Only use read-only git commands (e.g. `git status`, `git diff`, `git log`) if needed for context.

## Build Environment

**Primary: Git Bash on Windows.** The build scripts and scons work natively with Python/uv on Windows. The scripts auto-create a `.venv` in the repo root if needed.

Prerequisites: Python 3.8+, [uv](https://docs.astral.sh/uv/), .NET 8 SDK (for C# builds), Visual Studio Build Tools or MinGW.

**Secondary: WSL2.** If cross-compiling from WSL2, the repo lives on an NTFS mount (`/mnt/f/godot`). NTFS is case-insensitive, which breaks Python packages like SCons — the venv must live on native Linux FS:

```bash
uv venv ~/.venvs/godot
VIRTUAL_ENV=~/.venvs/godot uv pip install scons
source ~/.venvs/godot/bin/activate
```

## Build Commands

### Full Build with C# Support

Use the build scripts for the complete 3-step process (scons → glue generation → C# assemblies):

```bash
./build_dotnet_editor.sh              # Production editor with C#
./build_dotnet_editor_dev.sh          # Dev editor with C# (debug symbols + extra checks)
```

These scripts handle everything: venv setup, scons build, glue generation, and C# assembly building with `--push-nupkgs-local` so NuGet can resolve the dev SDK packages.

### C++ Only (scons)

For C++-only iteration (skips C# glue and assembly steps):

```bash
# Build editor (default target)
scons platform=windows d3d12=no        # Windows (no D3D12 SDK)
scons platform=linuxbsd                 # Linux/BSD
scons platform=macos                    # macOS

# Build with specific target
scons platform=<platform> target=editor           # Editor (default)
scons platform=<platform> target=template_debug   # Debug export template
scons platform=<platform> target=template_release # Release export template

# Common build options
scons platform=<platform> dev_build=yes           # Dev build with extra checks
scons platform=<platform> production=yes          # Production build with LTO
scons platform=<platform> arch=x86_64             # Specify architecture
scons platform=<platform> compiledb=yes           # Generate compile_commands.json
scons platform=<platform> vsproj=yes              # Generate Visual Studio solution

# Faster builds
scons platform=<platform> scu_build=yes                    # Single compilation unit (fewer compiler invocations)
scons platform=<platform> fast_unsafe=yes                   # Faster incremental builds (less rebuild certainty)
scons platform=<platform> cpp_compiler_launcher=ccache      # Use ccache for compilation caching
scons platform=<platform> num_jobs=8                        # Parallel jobs (defaults to CPU count - 1)

# Build with tests
scons platform=<platform> tests=yes

# Dev mode shorthand (verbose=yes warnings=extra werror=yes tests=yes strict_checks=yes)
scons platform=<platform> dev_mode=yes

# Disable subsystems for faster builds when working on specific areas
scons platform=<platform> disable_3d=yes                    # Skip 3D engine
scons platform=<platform> disable_advanced_gui=yes          # Skip advanced GUI nodes
scons platform=<platform> module_mono_enabled=no            # Skip C# support
```

### C# Project Workflow

After a full build, C# projects (like `demo/`) can use `dotnet build` directly. The build scripts register dev packages via `--push-nupkgs-local`, and `demo/NuGet.config` points at `bin/GodotSharp/Tools/nupkgs/` so NuGet can resolve `Godot.NET.Sdk/4.7.0-dev`.

If `dotnet build` fails with "Could not resolve SDK", rebuild the assemblies:
```bash
./build_dotnet_editor_dev.sh   # or just re-run Step 3 from the script
```

See `modules/mono/README.md` for full NuGet and assembly details.

## Running Tests

Tests use doctest framework and are built into the editor binary when `tests=yes`.

```bash
# Build with tests enabled
scons platform=<platform> tests=yes

# Run all tests
./bin/godot.<platform>.editor.<arch> --test

# Run specific test suite
./bin/godot.<platform>.editor.<arch> --test --test-case="*String*"

# List available tests
./bin/godot.<platform>.editor.<arch> --test --list-test-cases
```

Create new test files using: `python tests/create_test.py ClassName path/relative/to/tests`

Tests live in `tests/core/`, `tests/scene/`, `tests/servers/` mirroring the source layout. Each test is a header included by `tests/test_main.cpp`. Test pattern:

```cpp
#include "tests/test_macros.h"

namespace TestMyClass {

TEST_CASE("[MyClass] Description") {
    MyClass obj;
    CHECK(obj.method() == expected);
    CHECK_MESSAGE(obj.other() == val, "Descriptive failure message.");
}

} // namespace TestMyClass
```

## Architecture Overview

### Core Directory Structure

| Directory | Purpose |
|-----------|---------|
| `core/` | Engine core: object system, math, IO, variant types, ClassDB reflection |
| `scene/` | Scene tree, nodes (2D/3D/GUI), animation, resources |
| `servers/` | Backend services: rendering, physics, audio, navigation, text |
| `editor/` | Editor application (only built for `target=editor`) |
| `drivers/` | Hardware abstraction: graphics APIs, audio drivers |
| `platform/` | Platform-specific code (Windows, Linux, macOS, iOS, Android, Web) |
| `modules/` | Optional pluggable features (GDScript, C#, physics engines, codecs) |
| `thirdparty/` | Bundled external dependencies |
| `tests/` | Unit tests (doctest framework) |

### Fork-Specific Systems (`unreal` branch)

This fork adds two new rendering systems not present in upstream Godot. See `docs/` for the
primary reference documentation:

- **`docs/RT_INFRASTRUCTURE.md`** — authoritative reference for both systems (BLAS/TLAS
  management and shadow static caching). Start here.
- **`docs/GRAPHICS_ENHANCEMENT_ANALYSIS.md`** — pre-implementation rationale and gap analysis
  (historical; Sections 2.4 and 2.6 are now outdated).
- **`docs/RAY_TRACING_PHASE_A.md`** — narrative explanation of what Phase A built, why, and
  what comes next.

#### Shadow Caching — Key Files & Symbols

| Symbol | Location |
|--------|----------|
| `Instance::shadow_moved_msec` | `servers/rendering/renderer_scene_cull.h:435` |
| `Instance::SHADOW_STATIC_THRESHOLD_SEC` (default 0.5 s) | `servers/rendering/renderer_scene_cull.h:436` |
| `Instance::is_shadow_static(double p_threshold_sec)` | `servers/rendering/renderer_scene_cull.h:437` |
| `RendererSceneCull::shadow_static_threshold_sec` | `servers/rendering/renderer_scene_cull.h:1027` |
| `ShadowAtlas::Quadrant::Shadow::static_cache_valid` | `servers/rendering/renderer_rd/storage_rd/light_storage.h:400` |
| `RenderShadowData::use_static_cache` / `mark_static_after_render` | `servers/rendering/renderer_rd/renderer_scene_render.h` |
| `_filter_static_cached_shadows()` | `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` and `forward_mobile/render_forward_mobile.cpp` |
| **Project setting** | `rendering/lights_and_shadows/shadow_cache_static_threshold` (float, 0.0–5.0+ s, default 0.5 s) |

#### RT Infrastructure — Key Files & Symbols

| Symbol | Location |
|--------|----------|
| `RTSceneManager` | `servers/rendering/renderer_rd/environment/rt_scene_manager.h/.cpp` |
| `MeshStorage::Surface::blas` / `blas_built` / `blas_pending` | `servers/rendering/renderer_rd/storage_rd/mesh_storage.h` |
| `mesh_surface_is_blas_built()` / `mesh_surface_mark_blas_built()` | `servers/rendering/renderer_rd/storage_rd/mesh_storage.h:457–470` |
| `RendererSceneRender::rt_register_mesh_instance()` | virtual method on `renderer_scene_render.h` |
| `RendererSceneRender::rt_update_instance_transform()` | virtual method on `renderer_scene_render.h` |
| `RendererSceneRender::rt_update()` | virtual method on `renderer_scene_render.h` |

**Hardware requirements:** RT features are Vulkan-only. D3D12 RT functions are stubs returning
error messages. GLES3 no-ops. Both features degrade gracefully on unsupported hardware.

#### New Test Suites

| File | What It Tests |
|------|---------------|
| `tests/servers/rendering/test_shadow_caching.h` | Static classification after settle time, cache invalidation on transform change, light version change |
| `tests/servers/rendering/test_rt_scene_manager.h` | Registration lifecycle, transform updates, RT-unavailable no-op paths (headless, no GPU context) |

---

### GDCLASS and Binding Pattern

This is the most common pattern in the engine. Every exposed class follows it:

```cpp
class MyNode : public Node2D {
    GDCLASS(MyNode, Node2D);  // Sets up reflection, RTTI, ClassDB integration

protected:
    static void _bind_methods();           // Register methods/properties/signals for scripting
    void _notification(int p_what);        // Handle lifecycle notifications
    void _validate_property(PropertyInfo &p_property) const;  // Dynamic property visibility

public:
    void set_speed(float p_speed);
    float get_speed() const;
};
```

The `_bind_methods()` body registers everything for scripting and the editor:

```cpp
void MyNode::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_speed", "speed"), &MyNode::set_speed);
    ClassDB::bind_method(D_METHOD("get_speed"), &MyNode::get_speed);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0,100,0.1"), "set_speed", "get_speed");
    ADD_SIGNAL(MethodInfo("speed_changed", PropertyInfo(Variant::FLOAT, "new_speed")));
    BIND_ENUM_CONSTANT(MODE_IDLE);
}
```

Classes are registered with macros: `GDREGISTER_CLASS`, `GDREGISTER_VIRTUAL_CLASS`, `GDREGISTER_ABSTRACT_CLASS`, `GDREGISTER_INTERNAL_CLASS`.

### Variant Type System

Variant (`core/variant/`) is the universal type used for scripting interop. 43 types:

- **Atomic**: NIL, BOOL, INT, FLOAT, STRING
- **Math vectors**: VECTOR2/2I, VECTOR3/3I, VECTOR4/4I, RECT2/2I, PLANE, QUATERNION, AABB
- **Math transforms**: TRANSFORM2D, BASIS, TRANSFORM3D, PROJECTION
- **Misc**: COLOR, STRING_NAME, NODE_PATH, RID, OBJECT, CALLABLE, SIGNAL
- **Containers**: DICTIONARY, ARRAY
- **Packed arrays**: PACKED\_\*\_ARRAY (byte, int32, int64, float32, float64, string, vector2, vector3, color, vector4)

`PropertyInfo` uses `Variant::Type` to define property types. `PropertyHint` controls editor display (RANGE, ENUM, FILE, RESOURCE_TYPE, etc.).

### Server Architecture and RID Pattern

Servers (RenderingServer, PhysicsServer2D/3D, NavigationServer, AudioServer, TextServer) are singletons accessed via `*Server::get_singleton()`. They use **RID** (Resource ID) handles — opaque identifiers that reference server-side resources without exposing pointers.

Scene nodes call server APIs to create/manipulate resources; servers own the actual data and process it (often on separate threads). This decouples scene representation from backend implementation.

### Node Lifecycle

Key notification order: `NOTIFICATION_ENTER_TREE` (10) → `NOTIFICATION_READY` (13) → `NOTIFICATION_PROCESS` (17) / `NOTIFICATION_PHYSICS_PROCESS` (16) → `NOTIFICATION_EXIT_TREE` (11). Thread safety is enforced via `ERR_THREAD_GUARD` macros.

### Module System

Optional features in `modules/` have a `config.py` with this interface:

```python
def can_build(env, platform):     # Check if module can build; call env.module_add_dependencies()
def configure(env):                # Configure build environment
def get_doc_classes():             # List classes with XML documentation
def get_doc_path():                # Path to doc_classes/ directory
def is_enabled():                  # Whether enabled by default
```

Enable/disable: `module_<name>_enabled=yes/no`. Custom modules: `custom_modules=path`.

### Scripting Integration

- **GDScript**: `modules/gdscript/` — primary scripting language
- **C#/.NET**: `modules/mono/` — C# support
- **GDExtension**: `core/extension/` — native extension API for C++ plugins

## Code Style

Follow the [Godot code style guidelines](https://contributing.godotengine.org/en/latest/engine/guidelines/code_style.html):

- **Formatting**: `.clang-format` enforces style automatically (based on LLVM, uses `#pragma once`)
- **Indentation**: Tabs in C++, spaces in Python/SCons/YAML (see `.editorconfig`)
- **Line endings**: LF everywhere, UTF-8, trailing whitespace trimmed
- **Naming**: PascalCase for classes, snake_case for functions/variables, underscore prefix for private members
- **Linting**: `.clang-tidy` checks for member init, deprecated headers, nullptr usage, braces

Format/validate scripts in `misc/`:
- `misc/scripts/file_format.py` — enforce encoding, line endings, whitespace
- `misc/scripts/copyright_headers.py` — check/update copyright headers
- `misc/scripts/header_guards.py` — convert to `#pragma once`

## Commit Message Format

```
Area: Short description in imperative form

Optional longer description wrapped at 80 chars.
```

Examples:
- `Add C# iOS support`
- `Core: Fix Object::has_method() for script static methods`
- `Fix GLES3 instanced rendering color and custom data defaults`

Use `git pull --rebase` to avoid merge commits. One PR per topic. Reference issues in PR description (not commit message) using GitHub closing keywords.

## Documentation

When adding/modifying exposed APIs, update class reference XML files:
```bash
./bin/godot.<platform>.editor.<arch> --doctool doc/classes
```

Then fill in descriptions in the generated XML.

## Claude MCP Module

The `modules/claude/` module provides an MCP server that exposes 42 Godot editor tools to Claude Code. It runs a TCP server inside the editor process, with a Python bridge (`bridge/claude_mcp_bridge.py`) translating between MCP's stdio protocol and TCP.

### Key Implementation Files

| File | Purpose |
|------|---------|
| `mcp/godot_mcp_server.cpp` | Server core, protocol handling, allowed resource types |
| `mcp/godot_mcp_tools_schema.cpp` | All 42 tool definitions and parameter schemas |
| `mcp/godot_mcp_tools_scene.cpp` | Scene, property, and selection tool implementations |
| `mcp/godot_mcp_tools_script.cpp` | Script create/read/modify/validate implementations |
| `mcp/godot_mcp_tools_runtime.cpp` | Runtime scene tree, output, errors, screenshot, camera tools |
| `mcp/godot_mcp_tools_project.cpp` | Project settings and input map tools |
| `mcp/godot_mcp_tools_signals.cpp` | Signal connect/disconnect tools |
| `mcp/godot_mcp_tools_3d.cpp` | Navigation mesh baking tool |
| `mcp/godot_mcp_tools_animation.cpp` | Animation creation and inspection tools |
| `mcp/godot_mcp_tools_resource.cpp` | Project file listing, filesystem scan, and import diagnostics |
| `mcp/godot_mcp_tools_editor.cpp` | Editor log, screenshot, viewport camera, editor control, canvas view, editor state tools |
| `mcp/godot_mcp_validation.cpp` | Path/type validation and JSON-to-Godot type coercion |

### Build

```bash
scons platform=windows module_claude_enabled=yes
```

### Claude Code Plugin

The `modules/claude/` directory is a Claude Code plugin marketplace. It provides the `godot-mcp` plugin with the Godot game development skill and auto-starts the MCP bridge.

The marketplace is auto-discovered via `.claude/settings.json`. First-time setup:

```bash
# Install the plugin (one-time, from within Claude Code)
/plugin install godot-mcp@godot-plugins

# Or from the CLI
claude plugin install godot-mcp@godot-plugins --scope user
```

After installation, the plugin loads automatically in all future sessions.

**What the plugin provides:**
- `skills/godot-game-dev/` — triggers on Godot game development tasks
- `.mcp.json` — starts the Python bridge to connect to the editor's MCP server

**Alternative (per-session, no install):**
```bash
claude --plugin-dir ./modules/claude
```

### Documentation

- [README](modules/claude/README.md) - Overview, quick start, tool list
- [TOOL_REFERENCE](modules/claude/docs/TOOL_REFERENCE.md) - Full API reference for all 42 tools
- [MCP_SERVER](modules/claude/docs/MCP_SERVER.md) - Protocol and architecture
- [SECURITY](modules/claude/docs/SECURITY.md) - Security model and risk assessment
- [IMPLEMENTATION_GUIDE](modules/claude/docs/IMPLEMENTATION_GUIDE.md) - Build and development guide
