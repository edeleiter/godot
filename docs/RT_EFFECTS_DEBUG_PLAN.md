# RT Effects Debugging Plan — Reflections, AO, Shadows

## Status

Scene runs at 60 FPS with zero runtime errors. The TLAS is built and ray tracing is executing. However, the visual output is incorrect:

- **RT Reflections**: Diagonal striped pattern (white/blue alternating) instead of actual scene reflections
- **RT AO**: Not yet visually verified
- **RT Shadows**: Not yet visually verified

Three bugs were already fixed in a prior session:
1. `gl_RayFlagsCullBackFaceEXT` changed to `gl_RayFlagsCullBackFacingTrianglesEXT`
2. Reversed-Z depth check: `>= 0.9999` changed to `<= 0.0001`
3. TLAS deferral: removed `tlas_built_this_frame` mechanism that returned null RID on rebuild frames

---

## PHASE 1: Isolate the Raygen Output (RT Reflections)

**Goal**: Determine whether the striped pattern originates in the raygen shader or the accumulate pass.

### Step 1.1: Bypass the Accumulate Pass

Edit `servers/rendering/renderer_rd/effects/ss_effects.cpp` at lines 2311–2336 to skip the accumulate pass and copy-back. The forward pass reads from `refl_current` (the `RB_RT_CURRENT` texture), so we need to ensure the raw raygen output goes straight to that texture.

**What to do**: Comment out the entire accumulate pass and the `texture_copy` at lines 2311–2336. The raygen pass already writes into `refl_current`, so the forward pass will read raw 1-SPP output directly.

```cpp
// --- TEMPORARILY DISABLED: Accumulate pass ---
// Lines 2311-2336: comment out everything from "// --- Accumulate pass" to texture_copy
```

**Expected results**:
- If stripes **DISAPPEAR** and you see noisy but directionally-correct reflections: the bug is in the accumulate pass (velocity buffer, neighborhood clamping, or ping-pong logic)
- If stripes **PERSIST**: the bug is in the raygen shader (depth reconstruction, normal decoding, ray direction, TLAS geometry)
- If screen goes **BLACK**: the raygen is not writing to the correct texture, or the texture is being cleared elsewhere

### Step 1.2: Debug Color Output

If stripes persist after Step 1.1, add diagnostic color outputs to the raygen shader at `servers/rendering/renderer_rd/shaders/effects/rt_reflections.glsl`.

**Test A — Depth visualization** (replace lines 97–143 with):
```glsl
// DEBUG: visualize depth buffer
imageStore(output_color, coord, vec4(depth, depth, depth, 1.0));
return;
```
Expected: smooth grayscale gradient from white (near, depth near 1.0 in reversed-Z) to black (far, depth near 0.0). If you see stripes HERE, the depth buffer binding is wrong.

**Test B — Normal visualization** (replace lines 97–143 with):
```glsl
// DEBUG: visualize decoded world normal
vec3 normal = normalize((scene_data.inv_view * vec4(normalize(normal_roughness.xyz * 2.0 - 1.0), 0.0)).xyz);
imageStore(output_color, coord, vec4(normal * 0.5 + 0.5, 1.0));
return;
```
Expected: smooth colored surface (red/green/blue showing world-space normal directions). The floor should be mostly green (Y-up). If stripes appear here, the normal_roughness buffer binding or decode is wrong.

**Test C — UV visualization** (replace lines 71–143 with):
```glsl
imageStore(output_color, coord, vec4(uv, 0.0, 1.0));
return;
```
Expected: smooth red-green gradient from (0,0) at top-left to (1,1) at bottom-right. If this shows stripes, the `gl_LaunchIDEXT` or `pc.screen_size` is wrong.

**Test D — World position visualization** (replace lines 97–143 with):
```glsl
vec3 world_pos = reconstruct_world_pos(uv, depth);
vec3 pos_color = fract(world_pos * 0.1); // tile at 10-unit intervals
imageStore(output_color, coord, vec4(pos_color, 1.0));
return;
```
Expected: checkerboard-like tiling pattern that changes smoothly with camera movement. If positions are wrong, the `inv_projection` matrix setup is incorrect.

**Test E — Reflect direction visualization** (add before `traceRayEXT`):
```glsl
// DEBUG: visualize reflection direction
imageStore(output_color, coord, vec4(reflect_dir * 0.5 + 0.5, 1.0));
return;
```
Expected: colored pattern showing reflection directions. On a flat floor looking down, should be mostly green (reflecting upward).

**Test F — Ray hit vs miss** (replace closest-hit and miss payloads):
```glsl
// In raygen, after traceRayEXT:
// hit_color is set by closest-hit (alpha=1.0) or miss (alpha=0.0)
imageStore(output_color, coord, vec4(hit_color.a, 0.0, 1.0 - hit_color.a, 1.0));
return;
```
Expected: Red where rays hit geometry, blue where rays miss to sky. On a floor reflecting toward a cube, the cube's reflected position should be red.

### Step 1.3: Decision Tree After Phase 1

```
If Test A (depth) shows stripes:
  → Depth buffer is not correctly bound or has wrong format
  → Check: binding 0 of set 2 in rt_screen_reflection()
  → Check: is p_depth the right texture? Compare with what _process_ssr uses

If Test B (normals) shows stripes:
  → Normal buffer binding or format issue
  → Check: binding 1 of set 2
  → Check: normal_roughness format (should be RGBA8_UNORM)

If Test C (UV) shows stripes:
  → gl_LaunchIDEXT or screen_size mismatch
  → Check: pc.screen_size vs actual texture dimensions

If Tests A–C are clean but Test D (world pos) is wrong:
  → inv_projection matrix is wrong
  → See Phase 2 for matrix debugging

If Tests A–D are clean but Test E (reflect dir) is wrong:
  → Normal decode produces wrong direction after inv_view transform
  → See Phase 3

If Tests A–E are clean but Test F (hit/miss) is all blue:
  → Rays are all missing: TLAS has no geometry, or geometry is at wrong positions
  → See Phase 6

If Tests A–E are clean and Test F shows correct hit pattern:
  → Closest-hit shader is producing wrong colors
  → Check instance_base_colors SSBO indexing
  → See Phase 6

If Step 1.1 (bypass accumulate) fixes stripes:
  → Accumulate pass has a bug
  → See Phase 7
```

---

## PHASE 2: Depth Buffer and World Position Reconstruction

**Goal**: Verify the depth buffer is correctly read and the inverse projection correctly reconstructs world-space positions.

### Step 2.1: Verify Depth Buffer Reversed-Z Convention

Godot uses **reversed-Z** on Vulkan: near plane maps to depth=1.0, far plane to depth=0.0. The sky detection in the shader at line 77 is:
```glsl
if (depth <= 0.0001) {  // Sky is at depth ~0.0 in reversed-Z
```

**Verification**: Use Test A from Phase 1. Near objects should be WHITE (depth near 1.0), far objects darker. Sky should be BLACK (depth 0.0).

If depth appears inverted (near=black, far=white), the `set_depth_correction(true)` is not being applied or the depth buffer is from a different pipeline stage.

### Step 2.2: Verify Inverse Projection Matrix

The C++ code at `servers/rendering/renderer_rd/effects/ss_effects.cpp:2229–2231`:
```cpp
Projection correction;
correction.set_depth_correction(true);  // flip_y=true, reverse_z=true, remap_z=true
Projection inv_proj = (correction * p_projection).inverse();
```

`set_depth_correction(true)` creates this matrix (from `core/math/projection.cpp:787–807`):
```
[1,  0,    0,   0]
[0, -1,    0,   0]
[0,  0, -0.5,   0]
[0,  0,  0.5,   1]
```
This flips Y (Vulkan convention) and remaps Z from [-1,1] to [1,0] (reversed-Z + Vulkan [0,1] range).

The `p_projection` from `view_projection[0]` is the raw camera projection (OpenGL convention). `correction * p_projection` converts it to Vulkan clip space. The `.inverse()` then gives us the inverse that maps FROM Vulkan depth buffer values BACK to view space.

**Critical check**: The shader does:
```glsl
vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
vec4 view_pos = scene_data.inv_projection * ndc;
view_pos /= view_pos.w;
```

This assumes `depth` is in the Vulkan [0,1] range, which it is (depth buffer stores Vulkan depth). The UV remapping `uv * 2.0 - 1.0` converts from [0,1] to [-1,1] NDC. With the Y-flip in the correction matrix, this should produce correct view-space positions.

**How to verify**: Add a C++ print statement to dump the first few values of `inv_proj`:
```cpp
// In ss_effects.cpp after line 2231:
print_verbose(vformat("RT inv_proj [0][0]=%.4f [1][1]=%.4f [2][2]=%.4f [2][3]=%.4f [3][2]=%.4f [3][3]=%.4f",
    inv_proj.columns[0][0], inv_proj.columns[1][1],
    inv_proj.columns[2][2], inv_proj.columns[2][3],
    inv_proj.columns[3][2], inv_proj.columns[3][3]));
```
For a typical 70-degree FOV camera at aspect 16:9:
- `[0][0]` should be ~0.74 (horizontal FOV factor)
- `[1][1]` should be ~-1.33 (negative due to Y-flip, vertical FOV factor)
- `[3][2]` should be close to the near plane distance

### Step 2.3: Verify inv_view Matrix

The C++ code stores `cam_transform` as the `inv_view` matrix:
```cpp
store_transform(p_view_transform, ubo_data.inv_view);
```
where `p_view_transform = p_render_data->scene_data->cam_transform`.

In Godot, `cam_transform` is the camera's world-space transform (camera-to-world), which IS the inverse view matrix. The `store_transform` function at `servers/rendering/renderer_rd/effects/ss_effects.cpp:69–91` stores it as column-major 4x4.

**Verification**: Print the camera position:
```cpp
// After line 2233:
print_verbose(vformat("RT cam_pos: (%.2f, %.2f, %.2f)",
    p_view_transform.origin.x, p_view_transform.origin.y, p_view_transform.origin.z));
```
This should match the camera position visible in the Godot editor.

### Step 2.4: Compare with SSR Depth Reconstruction

SSR (`screen_space_reflection.glsl`) uses the SAME normal_roughness and depth buffers and works correctly. Compare its reconstruction approach to identify any differences. SSR's `compute_view_pos` function (if present) may use a different method. The key difference: SSR works in view space, while RT works in world space. The world-space transform via `inv_view` is the additional step that could be wrong.

---

## PHASE 3: Normal Decoding Verification

**Goal**: Verify normals are correctly decoded from the normal_roughness buffer.

### Step 3.1: Understand the Encoding Pipeline

The forward pass writes normals at `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl:2957–2966`:
```glsl
normal_roughness_output_buffer = vec4(encode24(normal) * 0.5 + 0.5, roughness);
normal_roughness_output_buffer.w = normal_roughness_output_buffer.w * (127.0 / 255.0);
if (bool(instances.data[instance_index].flags & INSTANCE_FLAGS_DYNAMIC)) {
    normal_roughness_output_buffer.w = 1.0 - normal_roughness_output_buffer.w;
}
```

The `encode24()` function (lines 1185–1202) is a CryEngine3 "best fit normal" encoding that scales the normal by a fitting factor from a lookup texture. The result is NOT a unit vector, but `normalize()` after decode recovers the correct direction.

### Step 3.2: Verify Decode Matches SSR

The RT shader decodes the same way SSR does:
```glsl
vec3 normal = normalize((scene_data.inv_view * vec4(normalize(normal_roughness.xyz * 2.0 - 1.0), 0.0)).xyz);
```

SSR at `servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl:116`:
```glsl
vec3 normal = normalize(normal_roughness.xyz * 2.0 - 1.0);
```

**Critical difference**: SSR works in VIEW SPACE (does not transform by inv_view). The RT shader transforms to WORLD SPACE. This is correct because RT rays are traced in world space (the TLAS uses world-space transforms).

### Step 3.3: Verify Roughness Decode

RT shader at `servers/rendering/renderer_rd/shaders/effects/rt_reflections.glsl:85–89`:
```glsl
float roughness = normal_roughness.w;
if (roughness > 0.5) {
    roughness = 1.0 - roughness;
}
roughness *= 255.0 / 127.0;
```

SSR resolve at `servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_resolve.glsl:28–31`:
```glsl
float sample_roughness = sample_normal_roughness.w;
if (sample_roughness > 0.5) {
    sample_roughness = 1.0 - sample_roughness;
}
sample_roughness /= (127.0 / 255.0);
```

**These are mathematically identical**: `* (255.0 / 127.0)` equals `/ (127.0 / 255.0)`. The roughness decode is correct.

### Step 3.4: Potential Normal Issue — View-Space to World-Space

The normals stored in the normal_roughness buffer are in **view space** (the forward pass applies the model-view transform to the normal). The RT shader must transform them to world space.

The RT shader does:
```glsl
vec3 normal = normalize((scene_data.inv_view * vec4(normalize(normal_roughness.xyz * 2.0 - 1.0), 0.0)).xyz);
```

The `0.0` in the w component is correct for transforming a direction (not a point). The `inv_view` matrix is the camera-to-world transform. This should correctly transform from view space to world space.

**Verification**: Use Test B from Phase 1. A floor at Y=0 should show mostly green (world Y-up normal). If the floor shows red or blue dominant, the normal transform is wrong, possibly due to a row-major vs column-major issue in `store_transform`.

---

## PHASE 4: TLAS and BLAS Geometry Verification

**Goal**: Verify the acceleration structure contains correct geometry at correct world positions.

### Step 4.1: Verify BLAS Vertex Data

The BLAS is created from mesh vertex buffers in `servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp`. The position attribute must be `R32G32B32_SFLOAT` (vec3 float) for Vulkan RT.

**Check**: Add a print in `build_pending_blas_surfaces()` to log vertex count, stride, and format:
```cpp
print_verbose(vformat("BLAS build: mesh=%d surface=%d vertex_count=%d stride=%d",
    mesh.get_id(), i, s->vertex_count, vertex_stride));
```

### Step 4.2: Verify TLAS Instance Transforms

The TLAS instances are filled at `drivers/vulkan/rendering_device_driver_vulkan.cpp:6339–6359`. Each instance gets `instanceCustomIndex = i` (sequential index).

**Check**: In `RTSceneManager::update_tlas()` at `servers/rendering/renderer_rd/environment/rt_scene_manager.cpp:260`, print the transforms being passed:
```cpp
// Before line 260:
for (int i = 0; i < blases.size(); i++) {
    print_verbose(vformat("TLAS instance %d: pos=(%.2f,%.2f,%.2f)",
        i, transforms[i].origin.x, transforms[i].origin.y, transforms[i].origin.z));
}
```

Expected: positions should match the scene's mesh instance positions visible in the editor.

### Step 4.3: Verify instanceCustomIndex Matches SSBO

The closest-hit shader reads `instance_base_colors.colors[gl_InstanceCustomIndexEXT]`. The SSBO is filled with all-white `(1,1,1,1)` entries (Phase B placeholder at `servers/rendering/renderer_rd/effects/ss_effects.cpp:2251–2254`).

Since all entries are white, ANY valid index will produce white. The only way to get non-white from the SSBO is an out-of-bounds read. If the SSBO has `required_count` entries but `gl_InstanceCustomIndexEXT` exceeds that, you get garbage.

**Check**: Verify `p_tlas_instance_count` passed to `rt_screen_reflection()` at line 1640 of `render_forward_clustered.cpp`:
```cpp
// At line 1640:
print_verbose(vformat("RT reflections: tlas_entry_count=%d", rt_scene_manager.get_tlas_entry_count()));
```

This should match the number of BLAS entries (instances × surfaces_per_instance). The SSBO must be at least this large.

### Step 4.4: Check for Mismatched Instance Count

If `get_tlas_entry_count()` returns 0 or an incorrect value, the SSBO will be too small. Check `RTSceneManager` for how `tlas_entry_count` is tracked.

If this method returns `instances_buffer_size` (which is the count of BLAS entries from `update_tlas()`), it should be correct. But verify it is NOT returning `instances.size()` (which counts unique instances, not instance×surface pairs).

---

## PHASE 5: Striped Pattern Root Cause Analysis

**Goal**: Narrow down the specific cause of the diagonal stripe pattern.

### Step 5.1: Stripe Characteristics

Diagonal stripes alternating between white and blue-ish suggest a pattern correlated with `coord.x + coord.y` or `coord.x ^ coord.y`. This is consistent with:

1. **Checkerboard from frame_index modulo**: The ping-pong uses `frame_index % 2`. If the accumulate pass reads from the wrong texture on alternate frames, you get temporal strobing that appears as stripes when the camera is static.

2. **GGX importance sampling noise**: With `pc.quality == 2` (HIGH), the hash `seed = coord.x * 1973 + coord.y * 9277 + frame_index * 26699` creates a per-pixel quasi-random pattern. If the reflect direction is dominated by the noise (because the actual normal or view direction is wrong), you get structured noise patterns.

3. **Velocity buffer issues**: If the velocity buffer is invalid or all zeros (fallback is 1x1 black), the accumulate pass reprojects from the same UV. But if the velocity is garbage, `history_uv` could be out of bounds, forcing `blend=1.0` for some pixels and creating patterns.

### Step 5.2: Test Quality=LOW (Perfect Mirror)

Change the quality parameter at `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:1635`:
```cpp
// Change from:
RendererRD::SSEffects::RT_REFLECTION_QUALITY_HIGH,
// To:
RendererRD::SSEffects::RT_REFLECTION_QUALITY_LOW,
```

At LOW quality, the shader uses perfect mirror reflection (no GGX sampling, line 104–105):
```glsl
if (pc.quality == 0u) {
    reflect_dir = reflect(view_dir, normal);
}
```

**If stripes disappear**: The GGX importance sampling hash is producing degenerate directions. The hash function may need improvement or the roughness values are causing extreme lobe sizes.

**If stripes persist**: The issue is not in GGX sampling; it is in the base reflection direction, depth reconstruction, or TLAS geometry.

### Step 5.3: Test with temporal_blend=1.0

Edit `servers/rendering/renderer_rd/effects/ss_effects.cpp:2317`:
```cpp
// Change from:
accum_pc.temporal_blend = 0.1f;
// To:
accum_pc.temporal_blend = 1.0f;  // No history, current frame only
```

This makes the accumulate pass output = current frame only (no temporal blending). If stripes disappear, the temporal accumulation is the culprit (velocity buffer or history texture).

---

## PHASE 6: TLAS/BLAS Data Integrity Deep Dive

**Goal**: If Phase 1–5 indicate the TLAS geometry or ray directions are wrong.

### Step 6.1: Simple Hit Distance Visualization

In the raygen shader, replace the `traceRayEXT` and output with:
```glsl
hit_color = vec4(0.0);
traceRayEXT(tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
    0xFF, 0, 0, 0, ray_origin, 0.001, reflect_dir, 1000.0, 0);
// Visualize hit distance (white = close hit, black = far/miss)
if (hit_color.a > 0.5) {
    float d = gl_HitTEXT; // only valid in chit, need to pack into payload
    imageStore(output_color, coord, vec4(1.0, 1.0, 1.0, 1.0)); // just show hits as white
} else {
    imageStore(output_color, coord, vec4(0.0, 0.0, 1.0, 1.0)); // misses as blue
}
```

Note: `gl_HitTEXT` is only available in the closest-hit shader, not raygen. To get the distance in raygen, modify the closest-hit to pack it into `hit_color.a`:
```glsl
// In closest-hit:
hit_color = vec4(1.0, 1.0, 1.0, gl_HitTEXT / 1000.0);
```
Then in raygen:
```glsl
float hit_dist = hit_color.a * 1000.0;
imageStore(output_color, coord, vec4(1.0 - hit_dist/100.0, 0.0, hit_dist/100.0, 1.0));
```

### Step 6.2: Shoot Camera-Forward Rays Instead of Reflection Rays

Replace the reflect direction with the camera forward direction to verify the TLAS geometry is at the right places:
```glsl
// Replace reflect_dir computation with:
vec3 reflect_dir = normalize(world_pos - camera_pos); // Just re-shoot the camera ray
```

This should produce a depth-like image of the scene. If this shows correct geometry outlines but reflection direction is wrong, the problem is in the normal or reflect computation.

### Step 6.3: Verify BLAS Geometry Bounds

Use the Vulkan validation layers with `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT` to check for BLAS/TLAS build errors. Enable via environment variable:
```
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
```

Or enable Godot's Vulkan validation:
```
--rendering-driver vulkan --gpu-validation
```

---

## PHASE 7: Accumulate Pass Debugging

**Goal**: If Phase 1 Step 1.1 shows that bypassing accumulate fixes the stripes.

### Step 7.1: Velocity Buffer Format Mismatch

The accumulate shader expects `rg16f` (R16G16_SFLOAT) for the velocity buffer (binding 2). The fallback velocity texture is also `RG16_SFLOAT`.

**Check**: Is the actual velocity buffer `RG16_SFLOAT`? The forward pass writes velocity as motion vectors. If the velocity format doesn't match, the `imageLoad` will read garbage.

Look at `ensure_velocity()` and `get_velocity_buffer()` in `RenderSceneBuffersRD` to verify the format.

### Step 7.2: Velocity Buffer Availability

At `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:1570`:
```cpp
RID velocity = p_render_buffers->has_velocity_buffer(false) ? p_render_buffers->get_velocity_buffer(false, 0) : RID();
```

If the velocity buffer is not available (no motion vectors enabled), `velocity` will be `RID()`, and the fallback 1x1 black texture is used. This is safe but means ALL pixels will use `velocity = (0,0)`, which maps to `history_uv = current_uv` — no reprojection. This should NOT cause stripes.

**However**: If `has_velocity_buffer(false)` returns true but the velocity buffer hasn't been written yet for this frame (because RT effects run in `_pre_opaque_render` BEFORE the color pass writes velocity), the velocity buffer may contain stale data or uninitialized values.

**Check**: Print whether velocity is valid:
```cpp
// After line 1570:
print_verbose(vformat("RT reflections: has_velocity=%d", velocity.is_valid()));
```

### Step 7.3: Ping-Pong Logic

The ping-pong at lines 2263–2266:
```cpp
bool ping = (p_frame_index % 2) == 0;
RID refl_current = ...;  // raygen writes here
RID accum_history = ...(ping ? RB_RT_HISTORY : RB_RT_PING);
RID accum_output = ...(ping ? RB_RT_PING : RB_RT_HISTORY);
```

Then at line 2335–2336:
```cpp
RD::get_singleton()->texture_copy(accum_output, refl_current, ...);
```

This copies the accumulate output back into `refl_current`. On the NEXT frame, the old `accum_output` becomes the new `accum_history` (they swap roles). But `refl_current` is ALWAYS the raygen target AND the forward-pass source.

**Potential issue**: If the `texture_copy` from `accum_output` to `refl_current` doesn't complete before the forward pass reads `refl_current`, you get a race condition. However, Godot's draw graph should serialize this.

**Another potential issue**: The accumulate pass reads `refl_current` via `imageLoad` (binding 0) AND writes `accum_output` via `imageStore` (binding 3). If `refl_current` is the same texture as either `accum_history` or `accum_output` due to a ping-pong bug, you get read-write aliasing.

**Verification**: Add print to check the three RIDs are distinct each frame:
```cpp
print_verbose(vformat("RT refl textures: current=%d history=%d output=%d ping=%d",
    refl_current.get_id(), accum_history.get_id(), accum_output.get_id(), ping));
```

### Step 7.4: Neighborhood Clamping Edge Case

The 3×3 neighborhood clamp in `rt_reflections_accumulate.glsl` (lines 31–44) can collapse the AABB to a single value if all 9 pixels are identical. For the first few frames, history is all black (cleared to 0,0,0,0). The clamped history would be clamped to the neighborhood of the current frame. With `blend = 0.1`, the result is `mix(clamped_history, current, 0.1)` = 90% clamped history + 10% current. If history is all black, the first frames produce very dark values.

**Test**: After startup, wait several seconds (50+ frames) for the accumulation to converge. If stripes persist after 50+ frames of a static camera, this is not a convergence issue.

---

## PHASE 8: RT AO Specific Debugging

**Goal**: Verify RT AO produces correct ambient occlusion values.

### Step 8.1: Visual Verification Setup

RT AO output goes to binding 27 (same slot as SSAO). The `ss_effects_flags` bit 0 must be set. From `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:774–778`:
```cpp
if (!use_ao && rt_scene_manager.is_enabled() && environment_get_rt_ao_enabled(p_render_data->environment)) {
    if (rd.is_valid() && rd->has_texture(RB_SCOPE_RTAO, RB_RT_CURRENT)) {
        use_ao = true;
    }
}
```

**Check**: Verify that SSAO is DISABLED in the environment to avoid confusion. If SSAO is also enabled, it may take priority at binding 27.

The code at lines 3897–3907 shows:
```cpp
u.binding = 27;
// Prefer RT AO result; fall back to SSAO.
RID aot;
if (rb.is_valid() && rb->has_texture(RB_SCOPE_RTAO, RB_RT_CURRENT)) {
    aot = rb->get_texture(RB_SCOPE_RTAO, RB_RT_CURRENT);
} else if (rb.is_valid() && rb->has_texture(RB_SCOPE_SSAO, RB_FINAL)) {
    aot = rb->get_texture(RB_SCOPE_SSAO, RB_FINAL);
}
```

RT AO takes priority over SSAO. Good. But `use_ao` (bit 0 of `ss_effects_flags`) must be set for the shader to read it.

### Step 8.2: Bypass Accumulate for AO

Same as Phase 1, Step 1.1 but for AO. Comment out lines in `rt_ambient_occlusion()` from the accumulate pass (lines 2137–2162) to see raw 1-SPP AO output.

### Step 8.3: Debug AO Output

Add to `rt_ao.glsl` raygen, after the `traceRayEXT`:
```glsl
// DEBUG: write raw AO as red (occluded) or green (unoccluded)
float ao_value = occluded ? 0.0 : 1.0;
imageStore(output_ao, coord, vec4(ao_value, 0.0, 0.0, 0.0));
```

Expected: Black near concave corners/under objects, white on open surfaces. If all black: rays are all hitting (`max_distance` too large, or normal bias too small causing self-intersection). If all white: rays are all missing (TLAS empty, or hemisphere samples pointing wrong way).

### Step 8.4: AO Self-Intersection Check

The normal bias is 0.01 units (line 87 of `rt_ao.glsl`). If the scene uses very small or very large scale, this may be insufficient or excessive.

**Test**: Increase bias to 0.1:
```glsl
vec3 ray_origin = world_pos + normal * 0.1;
```

If AO improves dramatically, the 0.01 bias is too small and rays are self-intersecting.

### Step 8.5: AO Payload Type Concern

The RT AO shader uses `bool` as the ray payload:
```glsl
layout(location = 0) rayPayloadEXT bool occluded;
```

And the miss shader:
```glsl
layout(location = 0) rayPayloadInEXT bool occluded;
```

**Potential issue**: Some Vulkan drivers may not handle `bool` payloads correctly. The SPIR-V spec represents `bool` as a 32-bit value, but some drivers may have quirks. If AO produces all-black or all-white regardless of geometry, try changing the payload to `float`:

Raygen:
```glsl
layout(location = 0) rayPayloadEXT float occluded;
// Initialize: occluded = 1.0;
// After trace: float ao_value = 1.0 - occluded;
```
Miss:
```glsl
layout(location = 0) rayPayloadInEXT float occluded;
void main() { occluded = 0.0; }
```

---

## PHASE 9: RT Shadows Specific Debugging

**Goal**: Verify RT shadows produce correct per-light shadow masks.

### Step 9.1: Verify Light Iteration

The shadow dispatch at `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:1686–1746` iterates ALL lights and filters for shadow casters.

**Check**: Add a counter:
```cpp
int shadow_light_count = 0;
for (int i = 0; i < (int)p_render_data->lights->size(); i++) {
    // ... existing code ...
    shadow_light_count++;
    // ... dispatch ...
}
print_verbose(vformat("RT Shadows: dispatched %d lights", shadow_light_count));
```

Expected: Should match the number of shadow-casting lights in the scene.

### Step 9.2: Verify Light Direction

For directional lights at line 1706:
```cpp
light_dir = -light_xform.basis.get_column(2).normalized();
```

The shadow shader at `servers/rendering/renderer_rd/shaders/effects/rt_shadows.glsl:86` negates this:
```glsl
vec3 base_dir = normalize(-pc.light_direction);
```

So `base_dir = -(-light_xform.basis.get_column(2)) = light_xform.basis.get_column(2)` = the light's -Z direction = the direction the light is pointing.

Analysis: `light_dir = -light_xform.basis.get_column(2)` = negative Z of light = direction light shines (away from light source toward scene). Then `base_dir = -light_dir` = direction FROM the scene TOWARD the light source. This is correct for shadow rays.

**Verify with print**:
```cpp
print_verbose(vformat("RT Shadow light: type=%d dir=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f)",
    (int)rt_type, light_dir.x, light_dir.y, light_dir.z,
    light_pos.x, light_pos.y, light_pos.z));
```

### Step 9.3: Verify Shadow Compositing

The compositing at `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:1749–1811` copies each light's accumulated shadow mask into a `texture2DArray`.

**Critical check** at lines 1807–1809:
```cpp
RD::get_singleton()->texture_copy(entry->accum_hist, p_rb_data->ss_effects_data.rt_shadow_array,
    Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(float(w), float(h), 1.0f),
    0, 0, 0, uint32_t(kv.value));
```

Note it copies from `entry->accum_hist`. But in `rt_shadow_dispatch()` at line 2510:
```cpp
SWAP(entry->accum_hist, entry->accum_out);
```

After the swap, `accum_hist` now points to what was the output of the accumulate pass (the newest accumulated result). The compositing then correctly reads this newest result.

**Ordering**: The compositing happens in `_pre_opaque_render()` at line 1989, AFTER the RT shadow dispatch. The RT shadow dispatch at lines 1958–1961 calls `_process_rt_shadows()`. Then at line 1989 `_prepare_rt_shadow_compositing()` runs. This ordering is correct.

### Step 9.4: Verify Shadow Data Packing

For directional lights, the shader reads `directional_lights.data[i].rt_shadow_slice` (a dedicated int field in the light data struct at `servers/rendering/renderer_rd/shaders/light_data_inc.glsl:70`). The C++ sets it at line 697:
```cpp
light_data.rt_shadow_slice = rt_slice ? *rt_slice : -1;
```

For omni/spot lights, the slice is packed into the upper byte of `bake_mode`:
```cpp
const uint32_t rt_slice_packed = rt_slice ? (uint32_t(*rt_slice + 1) << 8) : 0u;
light_data.bake_mode = (light_data.bake_mode & 0x00FFu) | rt_slice_packed;
```

The shader extracts it at `servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl:626`:
```glsl
shadow = apply_rt_shadow(shadow, int(omni_lights.data[idx].bake_mode >> 8u) - 1, screen_uv);
```

The `>> 8u` extracts the upper byte, `-1` converts from 1-based to 0-based (0 in upper byte means no RT shadow, yielding slice -1 which `apply_rt_shadow` skips).

**Potential issue**: If `p_rt_shadow_mapping` pointer is null (when `rb_data` is null), all lights get `rt_shadow_slice = -1`. Verify that `rb_data` is valid when `update_light_buffers` is called.

### Step 9.5: Debug Shadow Output Visually

Modify the forward shader to output the RT shadow value as the final color:
```glsl
// Temporary in scene_forward_clustered.glsl, at the end of fragment_shader:
if (bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_USE_RT_SHADOWS)) {
    int rt_slice = directional_lights.data[0].rt_shadow_slice;
    if (rt_slice >= 0) {
        float s = textureLod(sampler2DArray(rt_shadow_array, SAMPLER_LINEAR_CLAMP),
            vec3(screen_uv, float(rt_slice)), 0.0).r;
        frag_color = vec4(s, s, s, 1.0);
        return;
    }
}
```

---

## PHASE 10: Forward Pass Integration Verification

**Goal**: Verify the RT reflection/AO/shadow textures are correctly bound and read in the forward pass.

### Step 10.1: Verify ss_effects_flags

Add a print to confirm the flags are set:
```cpp
// In _setup_environment, after line 806:
print_verbose(vformat("ss_effects_flags = 0x%x (AO=%d, RT_REFL=%d, RT_SHADOW=%d)",
    scene_state.ubo.ss_effects_flags,
    (scene_state.ubo.ss_effects_flags >> 0) & 1,
    (scene_state.ubo.ss_effects_flags >> 4) & 1,
    (scene_state.ubo.ss_effects_flags >> 5) & 1));
```

Expected: bit 0 (AO) = 1 when RT AO is enabled, bit 4 (RT reflections) = 1, bit 5 (RT shadows) = 1.

### Step 10.2: Verify Binding 37 (RT Reflections)

At `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:4028–4039`:
```cpp
u.binding = 37;
// ... gets RB_SCOPE_RTREFL / RB_RT_CURRENT ...
```

**Check**: The texture at binding 37 must match what the raygen (and accumulate copy-back) wrote to. If a different texture is bound, the forward pass reads stale data.

### Step 10.3: Verify RT Reflection Blend in Shader

At `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl:2176–2183`:
```glsl
if (bool(implementation_data.ss_effects_flags & SCREEN_SPACE_EFFECTS_FLAGS_USE_RT_REFLECTIONS)) {
    vec4 rt_refl = textureLod(sampler2D(rt_reflections_buffer, SAMPLER_LINEAR_CLAMP), screen_uv, 0.0);
    indirect_specular_light = mix(indirect_specular_light, rt_refl.rgb, rt_refl.a);
}
```

The `rt_refl.a` is 1.0 for geometry hits (from closest-hit) and 0.0 for sky misses (from miss shader). After accumulation, it is blended. So `indirect_specular_light` is fully replaced where rays hit geometry.

**Debug**: Temporarily replace with:
```glsl
// DEBUG: show raw RT reflection texture
vec4 rt_refl = textureLod(sampler2D(rt_reflections_buffer, SAMPLER_LINEAR_CLAMP), screen_uv, 0.0);
frag_color = vec4(rt_refl.rgb, 1.0);
return;
```

This shows exactly what the forward pass reads from the RT reflection texture.

### Step 10.4: Check for SSR Conflict

SSR is processed at line 1944–1946, BEFORE RT effects at lines 1950–1962. The forward shader applies RT reflections at line 2176, BEFORE SSR at line 2186. The RT reflection blend uses `mix()` with alpha, then SSR writes on top. If SSR is also enabled, it may overwrite the RT reflections.

**Check**: Verify SSR is actually disabled in the environment. If `environment_get_ssr_enabled()` returns false, SSR is not processed and bit 2 of `ss_effects_flags` is 0.

---

## PHASE 11: Systematic Debug Instrumentation

### Step 11.1: RenderDoc Integration

Godot supports RenderDoc frame capture. To use:

1. Install RenderDoc
2. Launch the editor from RenderDoc ("Launch Application" → select godot binary)
3. Press F12 in the editor to capture a frame
4. In RenderDoc, find the "RT Screen Reflections" draw label
5. Inspect:
   - Input textures (depth, normal_roughness) at Set 2
   - TLAS at Set 0
   - Output texture at Set 3
   - UBO values at Set 1
   - Push constant values

This is the **MOST POWERFUL** debugging tool. In RenderDoc you can:
- View the depth buffer at the exact moment the RT pass runs
- Verify TLAS geometry by inspecting the acceleration structure
- Check that UBO values match expected matrices
- Step through the shader execution pixel-by-pixel

### Step 11.2: Godot Debug Print Instrumentation Points

Add `print_verbose()` calls at these locations (all guarded by a frame counter to print only once):

```cpp
static int debug_frame = 0;
if (debug_frame < 3) {
    debug_frame++;
    // ... print statements ...
}
```

1. **`_process_rt_reflections()`** (line 1585): Log TLAS validity, screen size, frame_index
2. **`rt_screen_reflection()`** (line 2208): Log UBO matrix values, SSBO size, texture RIDs
3. **`RTSceneManager::update_tlas()`** (line 157): Log BLAS count, transform values, instance count
4. **`_setup_environment()`** (line 770): Log `ss_effects_flags` value

### Step 11.3: Shader Debug Mode

Add a push constant field or use the existing `quality` field to switch the shader into debug mode:
```glsl
// In rt_reflections.glsl raygen:
if (pc.quality == 99u) {  // Debug mode
    // Diagnostic output: encode depth, normal, position, hit info as colors
    float depth = texelFetch(depth_buffer, coord, 0).r;
    vec4 nr = texelFetch(normal_roughness_buffer, coord, 0);

    // Pack multiple diagnostics into RGBA:
    // R = depth (reversed-Z, so near=1.0)
    // G = normal.y (should be ~1.0 for upward-facing floor)
    // B = roughness (decoded)
    // A = 1.0 (mark as valid)

    vec3 view_normal = normalize(nr.xyz * 2.0 - 1.0);
    vec3 world_normal = normalize((scene_data.inv_view * vec4(view_normal, 0.0)).xyz);

    float roughness = nr.w;
    if (roughness > 0.5) roughness = 1.0 - roughness;
    roughness *= 255.0 / 127.0;

    imageStore(output_color, coord, vec4(depth, world_normal.y * 0.5 + 0.5, roughness, 1.0));
    return;
}
```

---

## PHASE 12: Execution Order (Recommended Debug Sequence)

Execute phases in this order, stopping when the root cause is found:

| Step | Action | Effort | Phase Ref |
|------|--------|--------|-----------|
| 1 | Change quality to LOW | 1 line change | 5.2 |
| 2 | Bypass accumulate pass | Comment ~25 lines | 1.1 |
| 3 | Set `temporal_blend=1.0` | 1 line change | 5.3 |
| 4 | Depth visualization (Test A) | 2 lines in shader | 1.2 |
| 5 | Normal visualization (Test B) | 2 lines in shader | 1.2 |
| 6 | UV visualization (Test C) | 2 lines in shader | 1.2 |
| 7 | World position visualization (Test D) | 3 lines in shader | 1.2 |
| 8 | Reflect direction visualization (Test E) | 2 lines in shader | 1.2 |
| 9 | Hit/miss visualization (Test F) | Modify chit + raygen | 1.2 |
| 10 | Camera-forward rays | 1 line in shader | 6.2 |
| 11 | Print `inv_projection` matrix values | C++ print | 2.2 |
| 12 | Print TLAS instance transforms | C++ print | 4.2 |

After RT Reflections are working:

| Step | Action | Effort | Phase Ref |
|------|--------|--------|-----------|
| 13 | Apply equivalent tests to RT AO | Phase 8 | 8 |
| 14 | Apply equivalent tests to RT Shadows | Phase 9 | 9 |

---

## PHASE 13: Known Suspicious Code Patterns

### 13.1: Depth Correction Analysis

In `_process_rt_reflections()` at `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp:1606–1610`:
```cpp
const Projection &projection = p_render_data->scene_data->view_projection[0];

Projection correction;
correction.set_depth_correction(true);
const Projection proj = correction * projection;
```

Then in `rt_screen_reflection()` at `servers/rendering/renderer_rd/effects/ss_effects.cpp:2229–2231`:
```cpp
Projection correction;
correction.set_depth_correction(true);
Projection inv_proj = (correction * p_projection).inverse();
```

**Analysis**: `_process_rt_reflections()` passes `projection` (the raw `view_projection[0]`) to `rt_screen_reflection()`. Inside `rt_screen_reflection()`, depth correction is applied. So `inv_proj` actually uses `correction * view_projection[0]`, which is correct. The `proj` variable (with double correction) at line 1610 is only used for the reprojection matrix, not for the `inv_projection`.

**Conclusion**: No double correction bug here. The correction is applied once in the right place.

### 13.2: store_transform Semantics

The `cam_transform` passed as `p_view_transform` is the camera's world transform. `store_transform()` stores it as a 4×4 matrix. In the shader, this is used as `inv_view` — transforming from view space to world space.

`store_transform()` stores the `Basis` rows transposed into columns:
```cpp
// Column 0
p_array[0] = b.rows[0][0]; // basis.x.x
p_array[1] = b.rows[1][0]; // basis.y.x
p_array[2] = b.rows[2][0]; // basis.z.x
```

This is correct column-major storage matching GLSL's column-major `mat4`. The stored matrix transforms view-space vectors to world-space.

### 13.3: Velocity Buffer Timing

The RT effects run in `_pre_opaque_render()` (before the opaque color pass). The velocity buffer is written during the opaque color pass. So on the first frame, or if velocity isn't available, the fallback 1x1 black texture is used.

On subsequent frames, the velocity buffer from the PREVIOUS frame may be available. But the `has_velocity_buffer(false)` check at line 1570 returns true only if the velocity buffer was allocated this frame. If motion vectors are not enabled in the project settings, the velocity buffer is never allocated.

**Impact**: Without velocity, the accumulate pass always uses `velocity = (0,0)`, meaning `history_uv = current_uv`. This is correct for a static camera but means no proper reprojection for camera movement. This should NOT cause stripes.

### 13.4: Omni Light Range Bug in RT Shadows

In `_process_rt_shadows()` at line 1714:
```cpp
light_range = light_storage->light_get_param(base_light, RS::LIGHT_PARAM_RANGE);
```

In the shader (`rt_shadows.glsl` line 100):
```glsl
vec3 sample_point = pc.light_position + pc.light_range * vec3(sin(phi)*cos(theta), ...);
```

`LIGHT_PARAM_RANGE` is the light's attenuation range (in meters). Using it as the sphere radius for point sampling means the shader samples points ON a sphere at the light's attenuation range. This is wrong — for soft shadow from a point light, you want to sample on a small sphere around the light position (e.g., `light_size` parameter, not `light_range`).

**This is a confirmed bug in RT Shadows for omni lights** but does not affect the stripe pattern in RT reflections.

---

## File Reference Summary

| File | Key Lines | Purpose |
|------|-----------|---------|
| `servers/rendering/renderer_rd/shaders/effects/rt_reflections.glsl` | 71–143 (raygen), 146–171 (chit), 173–186 (miss) | RT Reflections shader |
| `servers/rendering/renderer_rd/shaders/effects/rt_reflections_accumulate.glsl` | 23–62 | Temporal accumulation compute shader |
| `servers/rendering/renderer_rd/shaders/effects/rt_ao.glsl` | 67–108 (raygen), 110–120 (miss) | RT AO shader |
| `servers/rendering/renderer_rd/shaders/effects/rt_ao_accumulate.glsl` | 23–60 | AO temporal accumulation |
| `servers/rendering/renderer_rd/shaders/effects/rt_shadows.glsl` | 62–146 (raygen), 148–158 (miss) | RT Shadows shader |
| `servers/rendering/renderer_rd/shaders/effects/rt_shadows_accumulate.glsl` | 24–61 | Shadow temporal accumulation |
| `servers/rendering/renderer_rd/effects/ss_effects.cpp` | 2073–2165 (AO), 2208–2338 (Refl), 2353–2513 (Shadows) | RT dispatch C++ |
| `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` | 770–806 (flags), 1534–1811 (RT dispatch+compositing), 2168–2173 (gating), 3897–4055 (bindings) | Forward clustered integration |
| `servers/rendering/renderer_rd/environment/rt_scene_manager.cpp` | 62–274 | TLAS/BLAS management |
| `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` | 2174–2183 (RT refl blend), 2637–2644 (RT dir shadow), 2957–2966 (normal encode) | Forward shader integration |
| `servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl` | 449–456 (apply_rt_shadow), 625–627 (omni), 881–883 (spot) | Light shadow sampling |
| `servers/rendering/renderer_rd/shaders/light_data_inc.glsl` | 70–72 | Directional light `rt_shadow_slice` field |
| `servers/rendering/renderer_rd/storage_rd/light_storage.cpp` | 608, 696–697, 901–904 | Light data packing |
| `drivers/vulkan/rendering_device_driver_vulkan.cpp` | 6339–6359 | Vulkan TLAS instance fill |
| `core/math/projection.cpp` | 787–807 | `set_depth_correction` implementation |

---

## Build and Test

After making any shader or C++ changes:
```bash
scons platform=windows d3d12=no
```

The shader `.glsl` files are compiled during the build by `glsl_builders.py`. No separate shader compilation step is needed.

Run the editor with the demo scene that has the reflective floor, torus, and colored cubes.
