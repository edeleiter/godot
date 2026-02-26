# Phase B: Hardware Ray Tracing — Full Effect Suite
**Date:** 2026-02-19
**Branch:** `unreal`
**Author:** Claude Code (claude-sonnet-4-6)
**Status:** Approved for implementation

---

## 1. Context and Dependencies

Phase A built the complete BLAS/TLAS lifecycle management infrastructure (`RTSceneManager`), shadow
static caching, and wired RT virtual dispatch into the scene cull pipeline. The Vulkan driver has
production-quality SBT management, `raytracing_pipeline_create()`, and
`raytracing_list_trace_rays()` fully implemented. `ShaderRD::setup_raytracing()` and
`UNIFORM_TYPE_ACCELERATION_STRUCTURE` both exist but are unused.

Phase B consumes that infrastructure to ship three rendering effects:
- **RT Reflections** — mirror/glossy reflections from screen-space ray tracing
- **RT Ambient Occlusion** — contact shadows and occlusion missed by SSAO
- **RT Shadows** — directional + omni + spot lights with soft penumbra

All three share the single TLAS from `rt_scene_manager.get_tlas()`.

### Reference Documentation
- `docs/RT_INFRASTRUCTURE.md` — authoritative Phase A reference (BLAS/TLAS lifecycle, shadow caching)
- `docs/RAY_TRACING_PHASE_A.md` — Phase A narrative explanation

---

## 2. User Decisions (Approved)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Ray pipeline | Full RT (raygen/rchit/rmiss via `ShaderRD::setup_raytracing()`) | Maximum quality |
| Deformable mesh handling | BLAS update (not rebuild) | Lighter GPU cost for animation |
| Temporal accumulation | TAA-style, 4–8 frames, motion vector rejection + neighborhood clamp | Ghost-free quality |
| Closest-hit shading | Per-instance base color buffer (no bindless textures) | Phase B scope limit |
| RT Shadows | Directional + omni + spot | All light types |

---

## 3. Architecture Overview

### 3.1 Deformable BLAS Support (Step 1 — Prerequisites All Effects)

**Problem:** Skinned and blend-shape meshes have BLASes built from the bind-pose vertex snapshot.
When the skeleton animates, the BLAS geometry is stale.

**Critical Fix C1 — Use Deformed Vertex Buffer:**
GPU skinning writes deformed vertices to `MeshInstance::Surface::vertex_buffer[current_buffer]`,
NOT to `Mesh::Surface::vertex_buffer` (the bind-pose buffer used at BLAS creation). The BLAS update
`VkCmdBuildAccelerationStructuresKHR` re-reads from the `vertexData.deviceAddress` stored at
creation time, which still points to the bind-pose buffer.

Fix: `acceleration_structure_update()` accepts the deformed vertex buffer RID. The driver
temporarily updates `geometry.geometry.triangles.vertexData.deviceAddress` to the deformed buffer's
device address before issuing the update-mode build.

**Critical Fix C2 — Frame Ordering:**
In `renderer_scene_cull.cpp`, `rt_update()` runs inside `update()`, which executes **before**
`render_scene()` where `RSG::mesh_storage->update_mesh_instances()` performs GPU skinning. The
deformed vertex data does not exist at `rt_update()` time.

Fix: Split the update path:
- `rt_update()` — handles static BLAS builds + TLAS rebuild for non-deformable changes (existing)
- `rt_update_deformable()` — new method called in `render_scene()` **after** `update_mesh_instances()`
  (~line 3473), before shadow/opaque passes

**Important — update_scratch_size (I4):**
Vulkan requires separate scratch buffers for build vs. update operations. `VkAccelerationStructureBuildSizesInfoKHR` reports different sizes for `buildScratchSize` and `updateScratchSize`. Both must be stored at BLAS creation time in `AccelerationStructureInfo::update_scratch_size`; only `update_scratch_size` is used when calling update-mode build.

### 3.2 RT Reflections (Step 2)

**Shader:** `rt_reflections.glsl` — three sections: `#[raygen]`, `#[closest_hit]`, `#[miss]`

**Uniform layout:**
- Set 0, bind 0: `accelerationStructureEXT tlas`
- Set 0, bind 1: `samplerCube sky_texture` (via `sky_get_radiance_texture_rd()`)
- Set 1, bind 0: SceneData UBO (inv_projection, inv_view, reprojection matrices)
- Set 2, bind 0: `sampler2D depth_buffer`
- Set 2, bind 1: `sampler2D normal_roughness_buffer`
- Set 3, bind 0: `image2D output_color` (rgba16f, writeonly)
- Set 3, bind 1: `sampler2D history_color`
- Set 3, bind 2: `image2D velocity_buffer` (rg16f, readonly)
- Set 4, bind 0: `layout(std430) buffer` instance_base_colors (vec4[], per TLAS entry)

**Push constant (≤128 bytes):**
`ivec2 screen_size, uint quality, uint frame_index, float roughness_cutoff, float temporal_blend, float pad[2]`

**Algorithm (raygen):**
1. Reconstruct world position from depth + SceneData matrices
2. Read normal + roughness from G-buffer
3. Skip (write 0) if `roughness > roughness_cutoff`
4. Compute reflection direction; for MEDIUM/HIGH: importance-sample GGX lobe
5. `traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin, 0.001, direction, 1000.0, 0)`
6. Temporal blend with reprojection + motion vector rejection

**Closest-hit (CRITICAL — C3):**
Use `gl_InstanceCustomIndexEXT` (NOT `gl_InstanceID`).
- `gl_InstanceCustomIndexEXT` = user-provided 24-bit index from `VkAccelerationStructureInstanceKHR`
- Set to TLAS entry index `i` in `tlas_instances_buffer_fill()`
- Index into `instance_base_colors[]` per TLAS entry (one vec4 per mesh surface per instance)

**Miss:** Sample sky_texture with `gl_WorldRayDirectionEXT`; alpha = 0.0 (signal: no geometry hit)

**Accumulate:** `rt_reflections_accumulate.glsl` — 8×8 compute, TAA with 3×3 neighborhood AABB
clamping + velocity rejection.

**Instance color buffer population:**
Iterated immediately before each dispatch in the same fill order as `tlas_instances_buffer_fill()`.
Material lookup: `StandardMaterial3D::get_albedo()` → `vec4`; otherwise default `vec4(0.5, 0.5, 0.5, 1.0)`.
Reallocated when TLAS instance count changes (same dirty-flag pattern as TLAS).

**History buffer:** `RB_RT_REFLECTIONS_HISTORY` — rgba16f, full internal resolution.

**Implementation location:** `SSEffects` class (pragmatic Phase B choice to reuse SceneData UBO,
reprojection matrices, render buffer scopes). Add `// TODO: Extract to RTEffects class when this
section exceeds ~500 lines`.

**Call site:** `render_forward_clustered.cpp` `_process_ssr()` (~line 1470):
- If TLAS valid + env `rt_reflections_enabled`: call `rt_screen_reflection()`
- Else: existing HZB SSR path (unchanged)

### 3.3 RT Ambient Occlusion (Step 3)

**Shader:** `rt_ao.glsl` — `#[raygen]` + `#[miss]` (no closest-hit — uses
`gl_RayFlagsSkipClosestHitShaderEXT`)

**Uniform layout:**
- Set 0, bind 0: `accelerationStructureEXT tlas`
- Set 1, bind 0: SceneData UBO
- Set 2, bind 0: `sampler2D depth_buffer`
- Set 2, bind 1: `sampler2D normal_roughness_buffer`
- Set 3, bind 0: `image2D output_ao` (r8, writeonly)
- Set 3, bind 1: `sampler2D history_ao` (r8)
- Set 3, bind 2: `image2D velocity_buffer` (rg16f, readonly)

**Push constant (≤128 bytes):**
`ivec2 screen_size, uint frame_index (0–7), float max_distance, float temporal_blend (0.125), float pad[3]`

**Algorithm:**
1. Reconstruct world position + normal
2. Select cosine-weighted direction from precomputed 8-sample Halton hemisphere, rotated by `frame_index`
3. `traceRayEXT` with `gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT`, `tmax=max_distance`
4. Payload initialized `true` (occluded); miss shader sets `false`
5. Temporal blend result with history (blend_factor = 0.125)

**Miss:** `occluded = false`

**Accumulate:** `rt_ao_accumulate.glsl` — 8×8 compute, same TAA pattern on R8 format.

**Compositing with SSAO:**
RT AO R8 output bound alongside SSAO texture in lighting uniform set.
In lighting shader: `ao_factor = ssao * rt_ao`.
When RT AO enabled without SSAO: SSAO texture defaults to 1.0.

**Call site:** `render_forward_clustered.cpp` `_pre_opaque_render()` after existing SSAO block.

### 3.4 RT Shadows (Step 4)

**Shader:** `rt_shadows.glsl` — single `#[raygen]` + `#[miss]` with `light_type` push constant
discriminator.

**Light type dispatch:**
- `DIRECTIONAL (0)`: shadow ray = `-light_direction ± jitter within sun_disk_angle cone`; `tmax=10000.0`
- `OMNI (1)`: sample random point on sphere at `light_position` with radius; ray toward sample; `tmax=distance(surface_pos, sample_point)`
- `SPOT (2)`: sample random point within spotlight cone; ray toward sample; `tmax=min(light_range, distance)`
- All: `gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT`

**Push constant (≤128 bytes, actual ~60 bytes):**
`ivec2 screen_size, uint light_type, uint frame_index, vec3 light_direction, float light_range, vec3 light_position, float sun_disk_angle, float spot_angle, float temporal_blend, float pad[1]`

**Design note — single shader:** Using one shader with `light_type` discriminator is the correct
Phase B choice. If branch divergence on GPU becomes a measurable concern in Phase C, convert to
specialization constants (NOT separate shaders).

**Accumulate:** `rt_shadows_accumulate.glsl` — REQUIRED (prevents shadow ghosting on moving
objects). TAA with neighborhood clamping. blend_factor = 0.25 (4-frame accumulation).

**History pool (I1 — per-light, not per-frame):**
- `RTShadowsRenderBuffers` with `HashMap<RID, RTShadowsHistoryEntry> history_pool`
- Fixed 16-slot LRU pool keyed by light RID
- `EVICTION_GRACE_FRAMES = 60` (prevents flicker when lights briefly off-screen)
- Method: `rt_shadow_dispatch()` (named to avoid collision with `rt_shadows` struct member — M3)

**Dispatch strategy:**
- Directional: up to `MAX_DIRECTIONAL_LIGHTS` (8), one dispatch each
- Omni/Spot: up to 4 each, ranked by screen-space coverage

**Compositing:**
RT shadow mask bound in lighting uniform set. Lighting shader: `shadow = raster_shadow * rt_shadow_mask`.
Rasterized shadow maps continue to be computed.

**Stereo (I1 addendum):** `RTShadowsRenderBuffers` is per render target (like all
`RenderSceneBuffersRD` data). VR stereo handled naturally — each eye has its own buffers.

### 3.5 Environment Node + Project Settings (Step 5)

New `Environment` struct fields (in `environment_storage.h`):
```cpp
// RT Reflections
bool  rt_reflections_enabled = false;
int   rt_reflections_quality = 0;
float rt_reflections_roughness_cutoff = 0.7f;

// RT AO
bool  rt_ao_enabled = false;
float rt_ao_max_distance = 1.0f;

// RT Shadows
bool  rt_shadows_enabled = false;
float rt_shadows_sun_disk_angle = 0.008726646f;  // 0.5° in radians
int   rt_shadows_omni_max_lights = 4;
int   rt_shadows_spot_max_lights = 4;
```

Project settings (in `renderer_scene_cull.cpp` constructor):
- `rendering/lights_and_shadows/rt_reflections_enabled` (bool, false)
- `rendering/lights_and_shadows/rt_reflections_quality` (enum: Low,Medium,High, 0)
- `rendering/lights_and_shadows/rt_reflections_roughness_cutoff` (float 0–1, 0.7)
- `rendering/lights_and_shadows/rt_ao_enabled` (bool, false)
- `rendering/lights_and_shadows/rt_ao_max_distance` (float 0.1–10.0m, 1.0)
- `rendering/lights_and_shadows/rt_shadows_enabled` (bool, false)

---

## 4. Critical Risks and Mitigations

| ID | Risk | Mitigation |
|----|------|-----------|
| C1 | BLAS update reads bind-pose buffer, not deformed buffer | `acceleration_structure_update()` accepts deformed vertex buffer RID; driver updates `vertexData.deviceAddress` |
| C2 | `rt_update_deformable()` runs before GPU skinning | New call site in `render_scene()` after `update_mesh_instances()`, not in `update()` |
| C3 | `gl_InstanceID` vs `gl_InstanceCustomIndexEXT` confusion | All closest-hit shaders use `gl_InstanceCustomIndexEXT`; SSBO indexed per TLAS entry in same fill order |
| I1 | Shadow history pool unbounded growth | Fixed 16-slot LRU with 60-frame grace period eviction |
| I3 | Shadow ghosting on moving objects | Mandatory `rt_shadows_accumulate.glsl` with TAA neighborhood clamping |
| I4 | `updateScratchSize` differs from `buildScratchSize` | Store both at BLAS creation; use `update_scratch_size` in update-mode build |
| I5 | Instance color SSBO stale when instance count changes | Reallocate + re-upload whenever TLAS instance count changes |
| M3 | `rt_shadows` struct/method name collision | Method named `rt_shadow_dispatch()` |
| — | `UNIFORM_TYPE_ACCELERATION_STRUCTURE` not handled in Vulkan uniform set | Verify handling before creating RT uniform sets; add `ERR_FAIL_COND` guard |
| — | Camera cut causing temporal ghosting | Velocity > threshold or UV out-of-bounds → `blend_factor = 1.0` |
| — | forward_mobile has no RT hardware | `rt_scene_manager.is_enabled()` returns false — guaranteed no-op |

---

## 5. File Map

### New Files (create)
| File | Purpose |
|------|---------|
| `docs/plans/2026-02-19-rt-phase-b-design.md` | This document |
| `servers/rendering/renderer_rd/shaders/effects/rt_reflections.glsl` | RT Reflections raygen/rchit/miss |
| `servers/rendering/renderer_rd/shaders/effects/rt_reflections_accumulate.glsl` | Reflections TAA compute |
| `servers/rendering/renderer_rd/shaders/effects/rt_ao.glsl` | RT AO raygen/miss |
| `servers/rendering/renderer_rd/shaders/effects/rt_ao_accumulate.glsl` | AO TAA compute |
| `servers/rendering/renderer_rd/shaders/effects/rt_shadows.glsl` | RT Shadows raygen/miss |
| `servers/rendering/renderer_rd/shaders/effects/rt_shadows_accumulate.glsl` | Shadows TAA accumulate |
| `tests/servers/rendering/test_rt_phase_b.h` | Phase B unit tests |

### Modified Files
| File | Changes |
|------|---------|
| `servers/rendering/rendering_device.h` | `ALLOW_UPDATE` flag, `acceleration_structure_update()` |
| `drivers/vulkan/rendering_device_driver_vulkan.h` | `update_scratch_size` in `AccelerationStructureInfo` |
| `drivers/vulkan/rendering_device_driver_vulkan.cpp` | Store `updateScratchSize`; implement BLAS update command |
| `servers/rendering/renderer_rd/storage_rd/mesh_storage.h` | `blas_allow_update` field |
| `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp` | Deformable detection in `build_pending_blas_surfaces()` |
| `servers/rendering/renderer_rd/environment/rt_scene_manager.h` | `deformable_instances`, new methods |
| `servers/rendering/renderer_rd/environment/rt_scene_manager.cpp` | `mark_instance_deformable()`, `update_deformable_blas_and_rebuild_tlas()` |
| `servers/rendering/renderer_scene_render.h` | `rt_mark_instance_deformable()`, `rt_update_deformable()` virtuals |
| `servers/rendering/renderer_rd/renderer_scene_render_rd.cpp` | Implement both new virtuals |
| `servers/rendering/renderer_scene_cull.cpp` | Call sites: `_instance_update_mesh_instance()` + after `update_mesh_instances()` |
| `servers/rendering/renderer_rd/effects/ss_effects.h` | All three RT effect structs + declarations |
| `servers/rendering/renderer_rd/effects/ss_effects.cpp` | All three RT effect implementations |
| `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` | RT effect call site branches |
| `servers/rendering/storage/environment_storage.h` | New RT environment properties |
| `scene/resources/environment.cpp` | Bind new properties |
| `tests/test_main.cpp` | Register test_rt_phase_b.h |
| `demo/main.tscn` | Reflective floor, metallic sphere, archway, skinned mesh |
| `demo/scripts/DemoController.cs` | RT toggle methods + HUD labels |

---

## 6. Verification

### Unit Tests
```bash
scons platform=windows tests=yes module_claude_enabled=yes
./bin/godot.windows.editor.x86_64 --test --test-case="*RTPhaseB*"
# Expected: all tests pass
```

### Visual Verification (requires Vulkan RT-capable GPU: RTX / RDNA2+)
1. Build editor and open demo scene
2. **RT Reflections:** Enable → reflective floor shows accurate reflections; skinned mesh reflection tracks deformed surface
3. **RT AO:** Enable → archway underside darkens with contact shadows SSAO misses at range
4. **RT Shadows (directional):** Enable → soft shadow with natural penumbra
5. **RT Shadows (omni):** Point lights show spherical soft shadows
6. **RT Shadows (spot):** Spotlight shows cone-correct soft shadows
7. **Fallback:** Disable all RT → HZB SSR + SSAO + rasterized shadows, no visual regression
8. **Non-RT hardware / D3D12:** All RT effects silently no-op, no crashes, no warning spam

---

## 7. Design Decisions Not To Revisit in Phase B

1. **No bindless textures in closest-hit** — the per-instance base color SSBO approach is simpler
   and sufficient for Phase B quality. Phase C can add full PBR material sampling.
2. **Single `rt_shadows.glsl` shader** — branch divergence cost is acceptable at Phase B scale.
   Specialization constants would be a Phase C optimization.
3. **SSEffects as host class** — pragmatic reuse of existing SceneData UBO infrastructure.
   The `// TODO: Extract to RTEffects` comment marks the refactor point.
4. **No BLAS rebuild for deformables** — update-mode is sufficient for smooth animation.
   Severely deforming meshes (topology changes) are out of Phase B scope.
