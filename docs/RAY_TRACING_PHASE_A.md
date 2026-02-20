# Ray Tracing Phase A: What We Built and Why

**Branch:** `unreal` | **Date:** February 2026
**Audience:** Anyone asking "what did we actually do with ray tracing, and where does it go next?"

> For the technical reference (symbols, call graphs, per-frame flow), see
> [RT_INFRASTRUCTURE.md](RT_INFRASTRUCTURE.md).

---

## 1. The Problem

Godot's Vulkan backend has a **complete, production-quality hardware ray tracing API**. Every
function you'd need to trace rays on an RTX or RDNA2+ GPU is fully implemented in
`drivers/vulkan/rendering_device_driver_vulkan.cpp`:

- `blas_create()` — allocate a GPU Bottom-Level Acceleration Structure (BVH over mesh triangles)
- `tlas_create()` — allocate a Top-Level Acceleration Structure (BVH over the scene)
- `acceleration_structure_build()` — trigger the GPU to build the BVH
- `trace_rays()` — dispatch a ray tracing shader
- `ShaderRD::setup_raytracing()` — compile raygen/hit/miss shaders

**None of these functions had any callers.** The rendering pipeline was 100% rasterization. RTX
and RDNA2+ GPUs expose full hardware RT capability that Godot was completely ignoring — not
because the driver work hadn't been done (it had), but because nobody had written the scene
management layer that feeds geometry into those APIs.

There was also a secondary, independent problem: Godot re-rendered **all** shadow casters every
frame for every shadow-casting light, even if every object in the scene had been completely
motionless for seconds. For a scene with 7 lights and 37 meshes, this is up to
7 × 37 = 259 unnecessary shadow render passes per frame at steady state.

---

## 2. What We Built (Phase A)

Phase A ships two independent features. Neither requires the other to function, and neither
breaks any existing rendering path.

### Feature A — BLAS/TLAS Scene Management (`RTSceneManager`)

We wrote the missing scene management layer: a new `RTSceneManager` class
(`servers/rendering/renderer_rd/environment/rt_scene_manager.h/.cpp`) that:

1. **Auto-generates one BLAS per mesh surface** when a mesh is loaded. The build is deferred
   (queued as `blas_pending = true` on the surface struct) so it doesn't stall mesh loading
   with synchronous GPU work. On the next TLAS update, pending BLASes are built in a batch.

2. **Rebuilds the TLAS every frame** when the scene has changed (any instance registered,
   unregistered, or moved). The TLAS encodes the world-space transform of every BLAS, giving
   RT shaders a traversable representation of the entire scene.

3. **Prevents redundant BLAS rebuilds** via a per-surface `blas_built` flag. Static geometry
   is built once and never touched again.

4. **Guards against editor reentrancy.** Godot's editor `ProgressDialog` pumps the main loop
   during long operations (filesystem scans, imports), which can re-enter the render loop while
   a TLAS update is in progress. An `is_updating_tlas` bool detects this and defers the update
   cleanly to the next frame.

5. **No-ops on non-RT hardware.** On construction, `RTSceneManager` checks for
   `SUPPORTS_RAYTRACING_PIPELINE` or `SUPPORTS_RAY_QUERY`. If neither is available (GLES3,
   D3D12, older Vulkan hardware), every method returns immediately.

### Feature B — Shadow Static Cache

We added a lightweight caching layer to the shadow render pipeline that tracks how long each
shadow-casting instance has been motionless:

- Each `Instance` struct gains a `shadow_moved_msec` timestamp (wall-clock, not frame count)
  that is updated on every `instance_set_transform()` call.
- `is_shadow_static(double p_threshold_sec)` returns `true` once the instance has been still
  for at least `threshold` seconds. The threshold is user-configurable via
  `rendering/lights_and_shadows/shadow_cache_static_threshold` (default 0.5 s).
- Each `ShadowAtlas::Quadrant::Shadow` tile gains a `static_cache_valid` flag. Once an
  instance is classified as static and its tile is rendered one final time, the tile is locked
  and the instance is filtered out of all subsequent shadow render passes by
  `_filter_static_cached_shadows()`.
- The cache is **invalidated immediately** if the instance moves (resetting `shadow_moved_msec`)
  or if the light changes (incrementing its version, which clears `static_cache_valid`).

The three-state lifecycle is: **DIRTY** (moving or unsettled) → **JUST_SETTLED** (threshold
passed, cache will be populated this frame) → **CACHED** (filtered out of shadow passes).

---

## 3. Why We Did It This Way

### Additive, not invasive

Both features are purely additive. The RT infrastructure adds new code paths that existing
shaders never touch. The shadow cache adds filtering that degrades to full re-render if the
cache is invalid. No existing rendering path was modified in a way that changes visible output
on hardware that was working before.

### The shadow cache is independently valuable

Shadow caching doesn't require RT shaders. It reduces real render work today — on every
platform, including GLES3 — and its cost is almost zero on scenes without static geometry (the
filter function exits early if no tiles are valid).

### The TLAS is the foundation for everything RT

Without a live TLAS, none of the future RT effects (reflections, AO, GI, shadows) can be
tested or shipped. Building the scene management layer first means that every subsequent phase
is "just" writing a shader and hooking it up — the hard infrastructure part is done.

### Frame-rate independence for shadow caching

Using wall-clock milliseconds (`get_ticks_msec()`) rather than frame count for the settle
threshold means the 0.5s default behaves identically at 30fps and 144fps. A frame-count
threshold would need to be calibrated per target frame rate, which is fragile.

---

## 4. What It Enables

With the TLAS built and updated every frame, each of the following phases becomes a
**shader-writing exercise** rather than an infrastructure project:

| Phase | What It Replaces/Augments | Key Integration Point |
|-------|--------------------------|----------------------|
| **B — RT Reflections** | Screen-Space Reflections (SSR) | `servers/rendering/renderer_rd/effects/ss_effects.h/.cpp` |
| **C — RT Ambient Occlusion** | SSAO | `servers/rendering/renderer_rd/effects/ss_effects.h/.cpp` |
| **D — RT Global Illumination** | SDFGI (near-field augmentation) | `servers/rendering/renderer_rd/environment/gi.h/.cpp` |
| **E — RT Shadows** | Shadow atlas (area lights) | Shadow pass in `render_forward_clustered.cpp` |

For Phases B and C, no bindless resources or material access are needed — just hit/miss
determination. These are the lowest-barrier next steps.

---

## 5. What's Next: Phase B (RT Reflections)

The concrete next steps to add hardware RT reflections as an alternative to SSR:

1. **Write three shaders** in `servers/rendering/renderer_rd/shaders/`:
   - `rt_reflections.raygen` — for each pixel, read G-buffer normal and view direction, compute
     reflection vector, call `traceRayEXT`
   - `rt_reflections.rchit` — at a hit, sample material albedo and roughness from surface data
   - `rt_reflections.rmiss` — sample the environment (sky) for reflected rays that escape the
     scene

2. **Hook into `ss_effects.cpp`** as a quality option. When the new option is selected, call
   `rd->trace_rays(raygen_pipeline, ray_buffer, tlas)` instead of (or in addition to) the
   existing HZB ray march.

3. **Add temporal accumulation.** One ray per pixel per frame is noisy. Accumulating over 4–8
   frames with motion-vector-driven history rejection produces clean results at low per-frame
   cost.

4. **Handle deformable meshes.** Currently skinned meshes and morph targets don't rebuild their
   BLAS when they deform (marked as TODO in `rt_scene_manager.cpp`). For Phase B this is
   acceptable if RT reflections are disabled for such meshes; full support can follow.

---

## 6. Key Numbers from the Demo Scene

The `demo/` scene was designed to exercise both features simultaneously:

| Metric | Value | What It Means |
|--------|-------|---------------|
| BLAS surfaces | 37 | One per mesh surface, auto-generated; zero manual setup |
| Dynamic objects | 12 | Camera + torus + 6 orbit spheres + 4 rotating cubes + bouncing sphere |
| TLAS rebuilds | Every frame | Because 12 objects move constantly |
| Static meshes cached | 25 | 37 total − 12 dynamic; settled after 0.5 s |
| Shadow lights | 7 | 1 directional + 4 omni + 2 spot, all with shadow maps |
| Shadow passes saved | ~175/frame | 25 static × 7 lights, at steady state |

The "175 shadow passes saved" figure is a ceiling: it assumes all 25 static meshes are in view
of all 7 lights with valid cached tiles. In practice, view-frustum culling and atlas eviction
reduce both the numerator and denominator, but the saving is significant in any static scene.
