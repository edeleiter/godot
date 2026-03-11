# RT Debugging Session Prompt

Copy everything below the line into a new Claude Code session.

---

## Context

I'm working on the `unreal` branch of a Godot Engine fork that adds hardware ray tracing (RT reflections, RT AO, RT shadows). The TLAS builds successfully, shaders compile, and the scene runs at 60 FPS with zero runtime errors — but the **visual output is wrong**. Specifically:

- **RT Reflections**: Diagonal striped pattern (white/blue alternating) instead of actual scene reflections
- **RT AO**: Not visually verified yet
- **RT Shadows**: Not visually verified yet

Three bugs were already fixed in prior sessions:
1. `gl_RayFlagsCullBackFaceEXT` → `gl_RayFlagsCullBackFacingTrianglesEXT`
2. Reversed-Z depth check: `>= 0.9999` → `<= 0.0001`
3. TLAS deferral: removed `tlas_built_this_frame` mechanism that returned null RID on rebuild frames

Additional changes already made (currently in the working tree diff):
- Changed TLAS instance flags from `CULL_DISABLE` to `TRIANGLE_FLIP_FACING` (winding order fix)
- Directional light direction sign: removed negation in C++ (shader was double-negating)
- Omni light soft shadows: now uses `LIGHT_PARAM_SIZE` instead of `LIGHT_PARAM_RANGE` for sphere sampling radius
- Added `light_size` parameter to shadow dispatch pipeline
- Removed `gl_RayFlagsCullBackFacingTrianglesEXT` from reflection trace (now just `gl_RayFlagsOpaqueEXT`)
- Increased reflection tmin from 0.001 to 0.01 to reduce self-intersection
- Set accumulation `temporal_blend` to 1.0 (bypass) for both reflections and shadows as a diagnostic
- Added diagnostic `print_line` statements in BLAS build and TLAS update
- Closest-hit shader currently has diagnostic color-coding by instance index (not material colors)
- Shadow normal bias increased from 0.005 to 0.01

## Your Task

**Systematically debug and fix the RT rendering issues until reflections, shadows, and AO produce visually correct output.** Follow the debug plan in `docs/RT_EFFECTS_DEBUG_PLAN.md` which has a 12-phase decision tree. The recommended execution order (Phase 12) tells you which tests to run first.

### Ground Rules

1. **Read first, change second.** Before modifying any file, read it. The debug plan references specific line numbers — verify them against current code since prior edits may have shifted lines.
2. **One variable at a time.** Make one diagnostic change, build, observe the result (I'll report what I see), then decide the next step. Do NOT shotgun multiple speculative fixes.
3. **Build command:** `scons platform=windows d3d12=no` (from Git Bash on Windows, repo root `/mnt/f/godot`). Shader `.glsl` files are compiled during build — no separate shader compilation.
4. **No git commands.** I handle all git operations. Never run `git commit`, `git push`, `git add`, etc.
5. **Test scene:** A demo scene with a reflective floor, torus, sphere, and colored cubes.

### Key Files

| File | Purpose |
|------|---------|
| `servers/rendering/renderer_rd/shaders/effects/rt_reflections.glsl` | Reflections raygen + closest-hit + miss |
| `servers/rendering/renderer_rd/shaders/effects/rt_reflections_accumulate.glsl` | Temporal accumulation compute |
| `servers/rendering/renderer_rd/shaders/effects/rt_shadows.glsl` | Shadows raygen + miss |
| `servers/rendering/renderer_rd/shaders/effects/rt_shadows_accumulate.glsl` | Shadow temporal accumulation |
| `servers/rendering/renderer_rd/effects/ss_effects.cpp` | C++ dispatch for all RT effects |
| `servers/rendering/renderer_rd/effects/ss_effects.h` | Push constant structs, method signatures |
| `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` | Forward pass integration, binding setup |
| `servers/rendering/renderer_rd/environment/rt_scene_manager.cpp` | TLAS/BLAS management |
| `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` | BLAS creation from mesh surfaces |
| `drivers/vulkan/rendering_device_driver_vulkan.cpp` | Vulkan RT driver (TLAS instance fill) |

### Key Documentation

- `docs/RT_EFFECTS_DEBUG_PLAN.md` — **The comprehensive debug plan.** Read this first. It has phases 1-13 with diagnostic shader modifications, decision trees, and file/line references.
- `docs/RT_INFRASTRUCTURE.md` — Authoritative reference for BLAS/TLAS management and shadow caching.
- `docs/RT_SHADOWS_PHASE_C.md` — How RT shadows are composited into the forward pass.
- `CLAUDE.md` — Build commands, architecture overview, code style.

### What I Need From You

1. **Start by reading the current state of the modified files** (check `git diff` to see what diagnostics are already in place).
2. **Read `docs/RT_EFFECTS_DEBUG_PLAN.md`** for the full debug methodology.
3. **Propose the first diagnostic step** — tell me exactly what code change to make and what to look for. Follow the Phase 12 recommended order (start with the simplest, most informative tests).
4. **After each observation I report back**, analyze the result using the Phase 1.3 decision tree and propose the next step.
5. **When the root cause is found**, implement the fix, remove all diagnostic code (`print_line`, debug shader outputs, `temporal_blend=1.0` overrides), and restore production behavior.

### Current State Summary

The working tree has diagnostic code scattered throughout. Before starting the debug sequence, you should understand what diagnostics are currently active vs. what the production code should look like. Key things currently overridden:
- `temporal_blend` set to 1.0 in both reflections and shadows (bypasses accumulation)
- Closest-hit shader has hardcoded color-by-index instead of material color lookup
- Multiple `print_line` diagnostics in BLAS build and TLAS update
- Depth visualization (`Test A` from the debug plan) was previously inserted and then removed from `rt_reflections.glsl`

Let's systematically find and fix every RT issue. Start by reading the current diff and the debug plan, then tell me what diagnostic to run first.
