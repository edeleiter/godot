# RT Shadows — Phase C: Forward-Pass Compositing Design

## Status

Phase C wires the per-light shadow masks produced by `_process_rt_shadows` (Phase B) into
the forward clustered renderer's lighting loops so every light's shadow term is tightened by
its RT shadow result.

---

## Background

Phase B computes a per-light R8_UNORM "shadow mask" (1.0 = lit, 0.0 = shadowed) using
ray tracing + TAA accumulation. The result sits in `RTShadowsHistoryEntry::accum_out`,
keyed by light instance RID in `RTShadowsRenderBuffers::history_pool`.

The forward pass knows nothing about these masks yet. Phase C makes the forward pass:

1. Sample each light's RT shadow mask at the current fragment's screen UV.
2. Combine it with the rasterized shadow map result via `min(shadow_map, rt_shadow)`.

The `min` blend is correct because both values are in [0, 1] where 0 = fully shadowed.
If either the shadow map *or* the RT shadow says "shadowed", the fragment is shadowed.
Neither source can "un-shadow" what the other has shadowed.

---

## Render-Order Timing (Frame N)

```
_prepare_rt_shadow_compositing()   ← before update_light_buffers
  uses accum_out from frame N-1    ← one-frame latency; acceptable for TAA
  builds texture2DArray            ← one layer per active shadow light (max 16)
  stores RID→slice in rt_shadow_mapping

update_light_buffers()             ← reads rt_shadow_mapping, packs slice index
  omni/spot: bake_mode bits [15:8] = slice+1 (0 = no RT shadow)
  directional: rt_shadow_slice     = slice (-1 = none)

depth pre-pass

_process_rt_shadows()              ← updates accum_out (ready for frame N+1)

main opaque pass                   ← samples rt_shadow_array[slice] at screen_uv
```

The one-frame latency (using `accum_out` from the previous frame) is consistent with how
SSAO, SSR, and RT reflections are composited. TAA temporal accumulation masks the delay.

---

## Data Flow Diagram

```
history_pool[light_rid].accum_out  (per-light R8 2D texture, frame N-1)
         │
         │  texture_copy (per active light)
         ▼
rt_shadow_array  (R8 texture2DArray, 16 layers, screen-sized)
         │
         │  layout(set=1, binding=38)
         ▼
scene_forward_clustered.glsl / scene_forward_lights_inc.glsl
         │
         │  textureLod(sampler2DArray(rt_shadow_array, SAMPLER_LINEAR_CLAMP),
         │             vec3(screen_uv, float(rt_slice)), 0.0).r
         ▼
rt_s ∈ [0,1]
         │
         │  shadow = min(shadow, rt_s)   or   shadow = half(min(float(shadow), rt_s))
         ▼
light_compute(... attenuation * shadow ...)
```

---

## Binding Strategy

### Why binding 38?

Binding 37 is the RT reflections result (Phase B, added in this fork). Binding 38 is the
next free slot. All bindings 34–37 are per-view (either `texture2D` or `texture2DArray`
depending on `#ifdef USE_MULTIVIEW`). Binding 38 is different: its array layers are
*light slots*, not view indices, so it is always a `texture2DArray` regardless of multiview.
It is therefore declared unconditionally outside the `#ifdef USE_MULTIVIEW` block.

### Texture format

- Format: `DATA_FORMAT_R8_UNORM` (1 byte per texel, same as per-light `accum_out`)
- Dimensions: screen resolution × 16 array layers
- Usage: `SAMPLING_BIT | CAN_COPY_TO_BIT` (destination for `texture_copy`, sampled in shaders)
- Unoccupied layers are initialized to white (1.0 = fully lit = no shadow contribution)

---

## Struct Packing — No Size Changes

### `DirectionalLightData` (light_storage.h, size 464 bytes)

Field `uint32_t pad[2]` at offset 64 is repurposed:

```cpp
// Before (8 bytes, wasted):
uint32_t pad[2];

// After (8 bytes, same layout):
int32_t rt_shadow_slice;  // -1 = no RT shadow; 0–15 = texture2DArray slice index
uint32_t pad;
```

GLSL mirror (`light_data_inc.glsl`, `DirectionalLightData`):

```glsl
// Before:
uvec2 pad;

// After:
int rt_shadow_slice;
uint pad;
```

No struct size change. The uniform buffer upload is unaffected.

### `LightData` (light_storage.h, size 192 bytes — fully packed, no free fields)

Upper byte of `bake_mode` (bits [15:8]) carries the RT shadow slice:

```
bake_mode bits [1:0]  = RS::LightBakeMode (0–3)
bake_mode bits [7:2]  = reserved / unused
bake_mode bits [15:8] = rt_shadow_slice + 1   (0 = no RT shadow, 1–16 = slice 0–15)
```

In C++ (fill loop):
```cpp
const uint32_t rt_slice_packed = slice ? uint32_t(*slice + 1) << 8 : 0u;
light_data.bake_mode = (light_data.bake_mode & 0x00FFu) | rt_slice_packed;
```

In GLSL (light loop):
```glsl
int rt_slice = int(omni_lights.data[idx].bake_mode >> 8) - 1;
// rt_slice == -1 means no RT shadow
```

---

## Eight Implementation Steps

| Step | File(s) Changed | What |
|------|----------------|------|
| 1 | `light_storage.h`, `light_data_inc.glsl` | Replace `pad[2]`/`uvec2 pad` with `rt_shadow_slice + pad` |
| 2 | `render_forward_clustered.h` | Add `rt_shadow_mapping`, `rt_shadow_array` to `ss_effects_data`; declare `_prepare_rt_shadow_compositing` |
| 3 | `render_forward_clustered.cpp` | Implement `_prepare_rt_shadow_compositing` |
| 4 | `light_storage.h`, `light_storage.cpp` | Add `p_rt_shadow_mapping` param to `update_light_buffers`; pack slice indices in fill loops |
| 5 | `render_forward_clustered.cpp` | Add binding 38 in `_setup_render_pass_uniform_set` |
| 6 | `scene_forward_clustered_inc.glsl` | Declare `SCREEN_SPACE_EFFECTS_FLAGS_USE_RT_SHADOWS (1<<5)` and `layout(binding=38) uniform texture2DArray rt_shadow_array` |
| 7 | `scene_forward_lights_inc.glsl`, `scene_forward_clustered.glsl` | Sample and min-blend RT shadow in omni, spot, and directional light loops |
| 8 | `render_forward_clustered.cpp` | Set `(1<<5)` flag in `_setup_environment`; uncomment `_process_rt_shadows` dispatch |

---

## Fallback Behaviour

- **No RT shadow for a light**: slice index is -1 (directional) or upper byte = 0 (omni/spot).
  The GLSL guard `if (rt_slice >= 0)` skips the RT shadow sample entirely; shadow map result
  is used unmodified.
- **RT shadow array not yet created**: binding 38 is bound to
  `DEFAULT_RD_TEXTURE_2D_ARRAY_WHITE` (all 1.0 = fully lit). The `min` blend is a no-op.
- **RT unavailable (no TLAS, non-Vulkan hardware)**: `_process_rt_shadows` early-returns;
  `accum_out` is never written; the compositing step copies stale white data → same no-op.

---

## Max Lights

`RTShadowsRenderBuffers::MAX_HISTORY_ENTRIES = 16`. The texture array has 16 layers.
Lights beyond slot 15 in a frame are silently unshadowed via RT (they still receive
rasterized shadow maps). The LRU pool evicts least-recently-used entries when full.

---

## Why `min()` and Not Multiplication?

A product `shadow_map * rt_shadow` would be mathematically equivalent when both sources are
perfectly correlated (same caster, same receiver). But in practice:

- Shadow maps have bias artifacts that lighten near-contact regions (acne mitigation).
  `min` lets the RT shadow override those biased-bright regions.
- RT shadows can miss thin-geometry casters the rasterized map resolves precisely.
  `min` lets the rasterized map win in those cases.

`min` is the conservative choice: always use the *more shadowed* estimate.
