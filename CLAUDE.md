# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Godot uses SCons (Python-based build system). Requires Python 3.8+ and SCons 4.0+.

```bash
# Build editor (default target)
scons platform=windows        # Windows
scons platform=linuxbsd       # Linux/BSD
scons platform=macos          # macOS

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
scons platform=<platform> scu_build=yes           # Single compilation unit (faster, uses more RAM)
scons platform=<platform> ninja=yes               # Use ninja backend

# Build with tests
scons platform=<platform> tests=yes

# Dev mode shorthand (verbose=yes warnings=extra werror=yes tests=yes strict_checks=yes)
scons platform=<platform> dev_mode=yes
```

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

### Key Architectural Patterns

**Object System** (`core/object/`): All engine objects inherit from `Object` with reflection via `ClassDB`. Properties, methods, and signals are registered for scripting access.

**Server Architecture**: Rendering, physics, audio etc. are implemented as singleton "servers" that process requests. Scene nodes send commands to servers; servers handle the actual work.

**Module System**: Optional features in `modules/` have a `config.py` defining `can_build()`, `configure()`, and dependencies. Enable/disable with `module_<name>_enabled=yes/no`.

**Platform Abstraction**: Each platform has `detect.py` for build detection and platform-specific implementations. Platform aliases exist for backwards compatibility (e.g., `x11` → `linuxbsd`).

### Scripting Integration

- **GDScript**: `modules/gdscript/` - primary scripting language
- **C#/.NET**: `modules/mono/` - C# support
- **GDExtension**: `core/extension/` - native extension API for C++ plugins

## Code Style

Follow the [Godot code style guidelines](https://contributing.godotengine.org/en/latest/engine/guidelines/code_style.html). Key points:

- Use clang-format (config in `.clang-format`)
- Tabs for indentation in C++
- PascalCase for classes, snake_case for functions/variables
- Prefix private members with underscore

## Commit Message Format

```
Area: Short description in imperative form

Optional longer description wrapped at 80 chars.
```

Examples:
- `Add C# iOS support`
- `Core: Fix Object::has_method() for script static methods`
- `Fix GLES3 instanced rendering color and custom data defaults`

## Documentation

When adding/modifying exposed APIs, update class reference XML files:
```bash
./bin/godot.<platform>.editor.<arch> --doctool doc/classes
```

Then fill in descriptions in the generated XML.
