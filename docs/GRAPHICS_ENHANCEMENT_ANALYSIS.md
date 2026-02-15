# Godot 4.7 Graphics Enhancements: Closing the Gap with Unreal Engine 5.7

**Document Type:** Research & Architectural Analysis (no engine code changes)
**Date:** February 2026
**Scope:** Rendering pipeline comparison, gap analysis, and prioritized enhancement roadmap

---

## Executive Summary

Godot's rendering has improved significantly through versions 4.5-4.6 (stencil buffer access, SSR overhaul, octahedral probes, AgX tonemapping). However, Unreal Engine 5.7 (November 2025) continues to push the boundary with production-ready Nanite, Lumen, MegaLights (beta), Substrate materials (production-ready), and Nanite Foliage (experimental). This document identifies the highest-impact, most feasible rendering enhancements for Godot 4.7+ based on deep analysis of both engines' actual implementations.

**Key finding:** Godot already has a complete hardware ray tracing API at the driver level (Vulkan BLAS/TLAS/pipeline/trace_rays) with zero integration into the rendering pipeline. This represents the highest ratio of visual improvement to remaining engineering effort in the entire codebase.

---

## Table of Contents

1. [Current State Comparison](#1-current-state-comparison)
2. [Deep Dive: Godot's Rendering Architecture](#2-deep-dive-godots-rendering-architecture)
3. [Deep Dive: UE 5.7 Rendering Architecture](#3-deep-dive-ue-57-rendering-architecture)
4. [Gap Analysis](#4-gap-analysis)
5. [Prioritized Enhancement Recommendations](#5-prioritized-enhancement-recommendations)
6. [Implementation Roadmap](#6-implementation-roadmap)
7. [Prototyping Strategy](#7-prototyping-strategy)
8. [Sources](#8-sources)

---

## 1. Current State Comparison

| Feature Area | UE 5.7 | Godot 4.6 | Gap Severity |
|---|---|---|---|
| **Geometry streaming** | Nanite (production) + Nanite Foliage (experimental) | Manual LOD only (`lod_count` per surface) | **Critical** |
| **Global illumination** | Lumen (HW RT focus, screen probes + surface cache) | SDFGI + VoxelGI + LightmapGI | Moderate |
| **Many-light rendering** | MegaLights (beta, stochastic sampling, 100s+ shadow lights) | Clustered forward (512 max elements, ~256-512 dynamic lights) | **Large** |
| **Material system** | Substrate (production, modular BSDF slabs, layering) | BaseMaterial3D (monolithic PBR, 19 texture slots, no layering) | **Large** |
| **Hardware ray tracing** | Full pipeline (Lumen, shadows, reflections) | Complete Vulkan API, **zero pipeline integration** | **Critical** |
| **Upscaling** | TSR (temporal, thin geometry detection) + DLSS/FSR | FSR 1.0 (spatial) + FSR 2.0 (temporal) + TAA | Small |
| **GPU-driven rendering** | Full GPU scene, indirect draws, GPU culling | CPU-driven draw submission, CPU frustum/occlusion culling | **Critical** |
| **Virtual Shadow Maps** | Production (tiled, cached, per-light virtual pages) | Shadow atlas (4-quadrant, LRU eviction, 4 cascades max) | Large |
| **Path tracing** | Production-ready DXR | None | Large |
| **Deferred rendering** | Deferred + forward hybrid, visibility buffer (Nanite) | Forward+ clustered only | Moderate |
| **Anti-aliasing** | TAA, TSR, SMAA (experimental) | TAA, MSAA, FSR, SMAA (via post) | Small |

---

## 2. Deep Dive: Godot's Rendering Architecture

### 2.1 Forward Clustered Renderer

**Core files:**
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h/.cpp`
- `servers/rendering/renderer_rd/cluster_builder_rd.h/.cpp`

**Draw call submission (CPU-driven):**

The main render loop in `RenderForwardClustered::_render_scene()` (line 1679) follows this path:

1. **CPU culling:** `renderer_scene_cull.cpp` performs frustum culling, visibility range culling, and optional occlusion culling via `RendererSceneOcclusionCull::HZBuffer` -- all on the CPU with multi-threaded parallelism via `WorkerThreadPool`.

2. **Render list building:** `_fill_render_list()` (line 915) iterates every instance in `p_render_data->instances`, computing depth, setting flags, checking GI mode, and populating `GeometryInstanceSurfaceDataCache` entries. Each instance's per-surface data is written into `scene_state.instance_buffer` on the CPU side.

3. **Instance merging:** Adjacent surfaces with identical sort keys get a `repeat` count (lines 873-882), enabling basic instancing. This is the closest thing to GPU-driven batching -- consecutive identical meshes drawn with `instance_count > 1`.

4. **Draw call emission:** `_render_list_with_draw_list()` calls `RD::get_singleton()->draw_list_draw()` per surface (line 611). For MultiMesh objects, there IS indirect draw support via `draw_list_draw_indirect()` (line 609) using pre-computed command buffers, but this is limited to MultiMesh only.

**Key limitation:** Every unique mesh surface requires a CPU-side draw call setup. There is no GPU scene buffer, no indirect draw for regular meshes, and no GPU-driven culling.

### 2.2 Cluster Builder

**File:** `servers/rendering/renderer_rd/cluster_builder_rd.h`

- **Cluster size:** 32x32 pixels (line 195)
- **Max cluster elements:** 512 (default, configurable) -- shared across lights, decals, and reflection probes
- **Light types tracked:** `ELEMENT_TYPE_OMNI_LIGHT`, `ELEMENT_TYPE_SPOT_LIGHT`, `ELEMENT_TYPE_DECAL`, `ELEMENT_TYPE_REFLECTION_PROBE`
- **Algorithm:** CPU sorts lights into clusters, uploads cluster bitmask buffer to GPU. Each pixel samples its cluster to determine affecting lights.
- **Max directional lights:** 8 (hardcoded in `renderer_scene_render.h:48`)

### 2.3 Global Illumination

**File:** `servers/rendering/renderer_rd/environment/gi.h/.cpp`

#### SDFGI (Signed Distance Field Global Illumination)

- **Cascade grid:** Up to 8 cascades (`SDFGI_MAX_CASCADES = 8`), each 128^3 voxels
- **Probe grid:** 17x17 per cascade (`PROBE_DIVISOR = 16`, axis_count = 17)
- **Dynamic lights:** Max 128 per frame (`MAX_DYNAMIC_LIGHTS = 128`)
- **Static lights:** Max 1024 (`MAX_STATIC_LIGHTS = 1024`)
- **SH storage:** 16 coefficients per probe (`SH_SIZE = 16`)
- **Compute pipeline:** Pre-process scroll -> jump flood SDF -> direct light (static + dynamic) -> integrate with sky -> store
- **Limitations:** Single-frame probe updates can cause banding; convergence requires temporal accumulation; max range limited by cascade count

#### VoxelGI

- **Max instances:** 8 per scene (`MAX_VOXEL_GI_INSTANCES = 8`)
- **Max lights per instance:** 32 (`voxel_gi_max_lights = 32`)
- **Storage:** Octree-based, with mipmap hierarchy for multi-resolution sampling
- **Bounces:** Configurable (default `use_two_bounces = true`)
- **Limitation:** Static-only; requires baking; limited volume coverage

#### LightmapGI

- **Features:** SH directional coefficients, shadow/AO channel, shadowmask modes (NONE, REPLACE, OVERLAY, ONLY)
- **Probe interpolation:** Tetrahedra + BSP tree spatial lookup
- **Max lightmaps per scene:** 8 (`MAX_LIGHTMAPS = 8`)

### 2.4 Shadow System

**File:** `servers/rendering/renderer_rd/storage_rd/light_storage.h`

- **Shadow atlas:** 4-quadrant system with independent subdivision per quadrant
- **Quadrant allocation:** LRU eviction with 500ms realloc tolerance
- **Directional light:** 4 cascade splits, with configurable split distribution and blend
- **Omni lights:** 6-face cubemap packed into atlas slots (flagged with `OMNI_LIGHT_FLAG`)
- **Soft shadows:** Adaptive sampling with `sc_soft_shadow_samples` (0-63) and `sc_penumbra_shadow_samples` (0-63)
- **No caching:** Shadows re-rendered every frame for moving objects; no page-based caching

### 2.5 Material System

**File:** `scene/resources/material.h` (BaseMaterial3D)

- **Texture slots:** 19 total (albedo, metallic, roughness, normal, ORM, emission, rim, clearcoat, anisotropy, detail, AO, bent normal, refraction, SSS, etc.)
- **Feature flags:** 13 toggleable features including clearcoat, anisotropy, SSS, refraction, backlight
- **Diffuse models:** Burley, Lambert, Lambert Wrap, Toon
- **Specular models:** Schlick GGX, Toon, Disabled
- **Transparency modes:** Disabled, Alpha, Alpha Scissor, Alpha Hash, Depth Pre-Pass
- **Shading modes:** Unshaded, Per-Pixel (full PBR), Per-Vertex
- **No layering:** Single material per surface. "Detail" texture provides one extra albedo+normal layer with mask blending, but there is no BSDF stacking, no energy-conserving multi-layer system.

### 2.6 Ray Tracing API (Infrastructure Only)

**Vulkan driver:** `drivers/vulkan/rendering_device_driver_vulkan.h/.cpp`

| API | Status | Location |
|-----|--------|----------|
| `blas_create()` | **Fully implemented** | Line 6098 |
| `tlas_create()` | **Fully implemented** | Line 6237 |
| `command_build_acceleration_structure()` | **Fully implemented** | Line 6319 |
| `raytracing_pipeline_create()` | **Fully implemented** | Line 6359 |
| `command_trace_rays()` | **Fully implemented** | Line 6348 |
| AccelerationStructure barriers | **Fully implemented** | Lines 2867-2893 |

**RenderingDevice API:** `servers/rendering/rendering_device.h/.cpp`

Complete public API with RID-based resource management:
- `blas_create()`, `tlas_create()`, `tlas_instances_buffer_create/fill()`
- `acceleration_structure_build()` with scratch buffer management
- `raytracing_list_begin/bind_pipeline/bind_uniform_set/trace_rays/end()`

**Rendering Device Graph:** Full command recording and execution for RT operations (`rendering_device_graph.h/.cpp`).

**Shader infrastructure:** `ShaderRD::setup_raytracing()` and `version_set_raytracing_code()` support raygen, any-hit, closest-hit, miss, and intersection shaders. **Unused.**

**D3D12:** All RT functions are stubs returning error messages (`"Ray tracing is not currently supported by the D3D12 driver."`).

**Platform support:** Enabled on Linux, Windows, Android (Vulkan). Disabled on macOS/iOS (MoltenVK limitation).

**Critical finding: The entire RT infrastructure is complete but has ZERO integration with the rendering pipeline.** No BLAS/TLAS is ever built for scene geometry. No RT shader is compiled. No rays are traced for GI, reflections, shadows, or AO.

### 2.7 Post-Processing

**File:** `servers/rendering/renderer_rd/effects/`

| Effect | Implementation | Notes |
|--------|---------------|-------|
| **TAA** | `taa.h/.cpp` | Compute-based resolve with history buffer and velocity rejection |
| **FSR 1.0** | `fsr.h/.cpp` | Spatial upscaler with sharpening |
| **FSR 2.0** | `fsr2.h/.cpp` | Temporal upscaler using AMD SDK |
| **SSAO** | `ss_effects.h/.cpp` | Quality levels: Very Low to Ultra; half-res option; 50-300m fadeout |
| **SSIL** | `ss_effects.h/.cpp` | Screen-space indirect light; 4 blur passes |
| **SSR** | `ss_effects.h/.cpp` | Hierarchical Z-based ray marching; half-res option; mipmap chain |
| **Motion vectors** | Built into forward pass | Full per-pixel velocity buffer for TAA/FSR2 |

### 2.8 Missing Features (Not Present in Codebase)

- No mesh shader / meshlet support (no `VK_EXT_mesh_shader` usage anywhere)
- No visibility buffer rendering
- No deferred rendering path
- No path tracing
- No GPU-driven culling or indirect draws (except MultiMesh)
- No virtual shadow maps
- No material layering or BSDF composition

---

## 3. Deep Dive: UE 5.7 Rendering Architecture

### 3.1 Nanite (Production + Foliage Experimental)

**Architecture:**
- Meshes split into clusters of exactly **128 triangles** during import
- Binary tree LOD hierarchy: parent nodes are simplified versions of children
- **GPU-driven cluster selection** based on screen-space error metric
- **Dual rasterization:**
  - **Hardware rasterization** for clusters with screen-space edges > ~32 pixels
  - **Software rasterization** (compute-driven) for sub-pixel clusters -- avoids overdraw overhead
- **Visibility buffer** output: triangle ID + material ID + barycentrics; deferred material evaluation
- **Occlusion culling:** Two-pass HZB (hierarchical Z-buffer) -- first pass tests against previous frame's HZB, second pass validates
- **Streaming:** Geometry pages streamed on-demand like virtual textures; GPU decompression; only visible detail resident in memory

**Nanite Foliage (Experimental in 5.7):**
- **Nanite Assemblies:** Group foliage parts into cohesive units for efficient instance management
- **Nanite Skinning:** Skeletal mesh-driven wind animation via Dynamic Wind plugin
- **Nanite Voxels:** Pixel-sized voxel representation for distant foliage at fraction of original cost
- Targets 60fps on current-gen hardware with lifelike animated vegetation

### 3.2 Lumen (Production)

**Architecture:**
- **Multi-method tracing:** Screen tracing first, then fallback to more reliable method
- **Surface cache:** Pre-computes material properties and direct/indirect lighting at surface positions; updates amortized across frames
- **Screen probes:** Final gather algorithm based on screen-space radiance caching (Siggraph 2021)
- **Far field:** Beyond 1km camera radius, traces against HLOD1 meshes for extended GI at lower cost
- **HW RT focus:** Hardware ray tracing provides higher quality and is enabled by default
- **SW RT deprecated:** Software ray tracing through signed distance fields still available but de-emphasized

### 3.3 MegaLights (Beta)

**Architecture:**
- **Stochastic direct lighting** via importance sampling of lights
- Traces a **fixed number of rays per pixel** towards important light sources (not more rays = more cost)
- **Ray guiding** selects important light sources, sends fewer samples toward likely-occluded lights
- Quality degrades locally at complex lighting areas rather than GPU cost scaling with total light count
- **Unified denoiser** reconstructs clean lighting from noisy stochastic input
- **Shadow methods per-light:** Ray tracing (default, no per-light cost) or Virtual Shadow Maps (rasterized, higher per-light cost but captures Nanite detail)
- **Supported:** Area lights, textured rect lights, spot lights, point lights, Niagara particle lights
- **Requirements:** Hardware ray tracing capable GPU (current-gen consoles, PC)
- **Limitations:** No forward renderer support; no water/clouds/heterogeneous volumes/SSS thickness

### 3.4 Substrate Materials (Production)

**Architecture:**
- Modular, physically-grounded material framework replacing fixed shading models
- **Slab-based BSDF composition:** Each layer is a BSDF slab with energy conservation
- True material layering: combine metal, clear coat, skin, cloth behaviors per pixel
- **Two rendering paths:**
  - **Adaptive GBuffer:** Advanced per-pixel topology and layered closures on modern hardware
  - **Blendable GBuffer:** Simplified fallback for broad platform compatibility
- Enabled by default in UE 5.7 (production-ready)

### 3.5 Virtual Shadow Maps (Production)

- Tiled, clipmap-based virtual pages for directional lights
- Per-light virtual shadow maps for point/spot lights
- **Page caching:** Only re-renders pages where objects have moved
- GPU-driven page allocation and rendering
- Deep integration with Nanite for geometry rendering into shadow maps

### 3.6 GPU-Driven Rendering (Production)

- **GPU scene buffer:** All mesh/material/transform data in persistent GPU-resident buffers
- **Indirect draw calls:** GPU generates draw commands from compute shaders
- **GPU culling:** Frustum, occlusion (HZB), and small-triangle culling on GPU
- Instance merging for identical meshes
- Bindless resource access eliminates per-object descriptor set binding
- Foundation that makes Nanite, MegaLights, and Virtual Shadow Maps possible

### 3.7 TSR (Production)

- Custom temporal upscaler tuned for UE's pipeline
- **Thin geometry detection** (new in 5.7) -- improved handling of foliage/wireframe elements
- Better motion vector integration than generic FSR/DLSS
- Also supports DLSS/FSR as plugin alternatives

---

## 4. Gap Analysis

### 4.1 Critical Gaps (Foundation-Level)

#### GPU-Driven Rendering
**Godot:** Every mesh surface requires CPU-side draw call setup in `_fill_render_list()`. Instance buffer populated per-frame on CPU. Frustum and occlusion culling on CPU.

**UE5:** Full GPU scene with persistent buffers, compute-driven culling, indirect draws.

**Impact:** This is the **single most important architectural gap**. Without GPU-driven rendering, virtual geometry (Nanite-like), MegaLights-style many-light rendering, and virtual shadow maps are impractical.

#### Hardware Ray Tracing Integration
**Godot:** Complete Vulkan RT API (BLAS/TLAS/pipeline/trace_rays) fully implemented but with **zero pipeline integration**. No acceleration structures are ever built. No RT shaders compiled.

**UE5:** RT deeply integrated into Lumen GI, reflections, shadows, and MegaLights.

**Impact:** This is the **highest ROI opportunity** in the codebase. The driver-level work is done. The remaining work is "merely" integrating it into GI, reflections, and shadows.

#### Geometry Streaming
**Godot:** Manual LOD only (`lod_count` per mesh surface, artist-provided). No auto-LOD generation, no streaming, no cluster-based rendering.

**UE5:** Nanite with automatic cluster LOD hierarchy, software+hardware hybrid rasterization, visibility buffer, on-demand streaming.

**Impact:** Very high for open-world and high-fidelity scenes. However, this is the most complex feature and depends on GPU-driven rendering being in place.

### 4.2 Large Gaps (Major Visual Features)

#### Many-Light Rendering
**Godot:** Clustered forward with 512 max cluster elements. Shadow atlas with 4-quadrant LRU allocation. Performance degrades linearly with shadow-casting light count.

**UE5:** MegaLights with stochastic sampling traces fixed rays per pixel. Cost scales with local complexity, not total light count. Hundreds of dynamic shadow-casting lights feasible.

#### Material System
**Godot:** Monolithic `BaseMaterial3D` with 19 texture slots and 13 feature flags. No material layering. Detail texture provides one extra layer only. Advanced effects require custom shaders.

**UE5:** Substrate with composable BSDF slabs, true material layering per pixel, energy-conserving multi-layer system.

#### Virtual Shadow Maps
**Godot:** Traditional shadow atlas with quadrant-based allocation and LRU eviction. No page caching. Shadows re-rendered every frame.

**UE5:** Tiled virtual pages with GPU-driven allocation. Page caching means only moving-object pages re-render.

### 4.3 Moderate Gaps

#### Global Illumination
**Godot's SDFGI** is actually competitive with Lumen's software RT path -- both use signed distance fields, both support multi-bounce, both have probe-based approaches. The gap is that Lumen can fall back to hardware RT for higher accuracy, while Godot cannot leverage its existing RT API at all.

#### Deferred Rendering
**Godot:** Forward+ only. UE5 has deferred + forward hybrid with visibility buffer (Nanite). A visibility buffer would complement virtual geometry and many-light rendering, but forward+ is adequate for Godot's current feature set.

### 4.4 Small Gaps

#### Upscaling/AA
Both engines have TAA and FSR support. UE5's TSR has better integration with its pipeline (especially thin geometry detection for Nanite Foliage), but Godot's FSR 2.0 is competitive. The gap is primarily integration quality rather than fundamental capability.

---

## 5. Prioritized Enhancement Recommendations

### Tier 1: Foundational (Highest Priority -- Enables Everything Else)

#### 1.1 GPU-Driven Rendering Pipeline

**Impact:** Critical | **Effort:** Very High | **Prerequisite for Tiers 2-3**

The single most important architectural change. Currently Godot is CPU-bound on draw submission.

**What to implement:**
- **GPU scene buffer:** Persistent SSBO containing all instance transforms, material IDs, mesh references. Replace per-frame CPU instance buffer population in `_fill_instance_data()` (render_forward_clustered.cpp:823-907).
- **Indirect draw calls:** Replace `draw_list_draw()` per surface with `vkCmdDrawIndexedIndirect` batched commands. MultiMesh already uses `draw_list_draw_indirect()` (line 609) -- extend this pattern to all meshes.
- **GPU frustum culling:** Compute shader that reads GPU scene buffer + camera frustum, outputs draw commands. Replace CPU-side culling in `renderer_scene_cull.cpp`.
- **GPU occlusion culling:** Hi-Z pyramid built from depth buffer; compute shader tests instance AABBs against Hi-Z. Replace CPU `RendererSceneOcclusionCull::HZBuffer`.
- **Instance merging:** GPU compute pass that sorts and merges identical mesh+material draw calls.

**Key files to modify:**
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h/.cpp` -- main render loop
- `servers/rendering/renderer_rd/storage_rd/mesh_storage.h/.cpp` -- mesh buffer management
- `servers/rendering/renderer_scene_cull.h/.cpp` -- move culling to GPU
- New: GPU scene management classes, culling compute shaders

**Why first:** Every subsequent feature (virtual geometry, many-light, virtual shadow maps) depends on the scene being GPU-resident.

---

#### 1.2 Hardware Ray Tracing Integration

**Impact:** Critical | **Effort:** High (infrastructure exists) | **Highest ROI**

The driver-level RT work is complete. The gap is purely in the rendering pipeline.

**Phase A -- Acceleration Structure Management:**
- Auto-build BLAS for all static mesh surfaces on load/change
- Maintain TLAS for the scene, updated per-frame for dynamic objects
- Integrate into `renderer_scene_cull.cpp` or new dedicated RT scene manager
- Key API: `blas_create()` -> `tlas_instances_buffer_fill()` -> `tlas_create()` -> `acceleration_structure_build()`

**Phase B -- RT Reflections (replace/augment SSR):**
- Write raygen + closest-hit + miss shaders for reflection rays
- Use `ShaderRD::setup_raytracing()` (already exists, unused)
- Sample environment at miss, material properties at hit
- Temporal accumulation for noise reduction
- Integrate as quality option alongside existing SSR in `ss_effects.h/.cpp`

**Phase C -- RT Ambient Occlusion (replace/augment SSAO):**
- Short-range rays from screen-space positions
- True contact shadows without screen-space limitations
- Lower sample count than reflections (8-16 rays typically sufficient)

**Phase D -- RT Global Illumination (augment SDFGI):**
- Hybrid approach: SDFGI for far-field, RT for near-field accuracy
- Second-bounce accuracy improvement over pure SDF tracing
- Integrate into `servers/rendering/renderer_rd/environment/gi.h/.cpp`

**Phase E -- RT Shadows:**
- Area light soft shadows without shadow maps
- 1 ray per pixel with temporal accumulation
- Alternative to shadow atlas for high-quality applications

**Key files to modify:**
- `servers/rendering/renderer_rd/environment/gi.h/.cpp` (Phase D)
- `servers/rendering/renderer_rd/effects/ss_effects.h/.cpp` (Phases B, C)
- New: RT scene manager, RT shader files in `servers/rendering/renderer_rd/shaders/`

**Why high priority:** The driver-level work is already done. Phases B and C alone would provide significant visual improvement for RT-capable hardware.

---

### Tier 2: Major Visual Features

#### 2.1 Virtual Shadow Maps

**Impact:** Large | **Effort:** High | **Depends on:** GPU-driven pipeline

Replace the 4-quadrant shadow atlas with tiled, cached virtual shadow maps.

**What to implement:**
- Clipmap-based virtual pages for directional lights (replacing 4 fixed cascades)
- Per-light virtual shadow maps for point/spot lights
- Page caching: only re-render pages where objects moved
- GPU-driven page allocation and rendering
- Page invalidation tracking per object

**Key benefit:** Consistent shadow resolution regardless of distance. No more shadow atlas competition between lights.

**Key files:**
- `servers/rendering/renderer_rd/storage_rd/light_storage.h/.cpp` -- replace ShadowAtlas
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` -- shadow passes

---

#### 2.2 MegaLights-Style Many-Light System

**Impact:** Large | **Effort:** High | **Depends on:** GPU-driven pipeline, virtual shadow maps

Enable hundreds of dynamic shadow-casting lights.

**What to implement:**
- Stochastic light sampling: select N most important lights per pixel via importance sampling
- Temporal denoising of shadow/lighting samples
- ReSTIR-like spatiotemporal importance resampling for light selection
- Upgrade clustered forward to handle 1000+ lights efficiently
- Ray guiding to avoid sampling occluded lights

**Key files:**
- `servers/rendering/renderer_rd/forward_clustered/cluster_builder_rd.h/.cpp` -- extend cluster capacity
- New: stochastic light sampling compute shaders
- New: spatiotemporal denoiser for lighting

---

#### 2.3 Advanced Material System (Substrate-like)

**Impact:** Large | **Effort:** Medium-High | **Independent of GPU-driven pipeline**

Godot's monolithic `BaseMaterial3D` cannot express layered materials without custom shaders.

**What to implement:**
- Material layering: blend multiple BSDFs per pixel with energy conservation
- Composable layers: clear coat, thin-film, sheen, anisotropy as stackable slabs
- Each layer carries its own roughness, color, normal perturbation
- Weight-based blending with proper energy conservation between layers
- Extend shader compiler to generate layered material evaluation code

**Key files:**
- `scene/resources/material.h` -- extend BaseMaterial3D or create new LayeredMaterial3D
- `servers/rendering/renderer_rd/storage_rd/material_storage.h/.cpp` -- multi-slab storage
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h/.cpp` -- shader permutations
- `servers/rendering/shader_compiler.h/.cpp` -- extend for layer composition

**Why medium-high effort instead of very high:** Doesn't require GPU-driven pipeline. Can be implemented as an extension of the existing material system.

---

### Tier 3: Advanced Geometry

#### 3.1 Meshlet Rendering Foundation

**Impact:** High | **Effort:** High | **Depends on:** GPU-driven pipeline

Build the foundation for Nanite-like virtual geometry.

**What to implement:**
- Mesh preprocessing: split meshes into ~128-triangle meshlets during import
- Meshlet culling: GPU compute pass for per-meshlet frustum + backface + occlusion culling
- Mesh shader path (where `VK_EXT_mesh_shader` available) with compute shader fallback
- Meshlet-based LOD hierarchy (simplified parent meshlets)
- GPU-driven meshlet selection based on screen-space error

**Why meshlets first:** This is the foundation for any Nanite-like system. It can provide performance benefits (reduced overdraw, better culling granularity) even without full virtual geometry.

#### 3.2 Visibility Buffer Rendering

**Impact:** Moderate-High | **Effort:** High | **Depends on:** Meshlet rendering

Complement meshlet rendering with deferred material evaluation.

**What to implement:**
- Visibility buffer pass: write triangle ID + instance ID (thin G-buffer)
- Deferred material evaluation from visibility buffer
- Hybrid forward+deferred (forward for translucent, deferred for opaque)

**Why:** Decouples geometry rasterization from material complexity. Essential for Nanite-like software rasterization and efficient many-material rendering.

---

### Tier 4: Polish & Competitive Parity

#### 4.1 Production Path Tracer

**Impact:** Large (offline/preview) | **Effort:** Medium | **Depends on:** RT integration (Tier 1.2)

For cinematic rendering, arch-viz, and ground-truth reference.

- Progressive path tracer using existing RT API
- Multiple importance sampling (MIS)
- Adaptive sampling with Intel OIDN integration
- All BSDFs must be path-traceable

#### 4.2 Temporal Super Resolution (TSR-like)

**Impact:** Medium | **Effort:** Medium

Custom temporal upscaler tuned for Godot's pipeline.

- Better integration with Godot's motion vectors than generic FSR 2.0
- Thin geometry detection (critical for vegetation)
- Quality improvements for particles/alpha-tested geometry
- Could leverage ML upscaling via plugin (DLSS-like)

#### 4.3 Nanite Foliage Equivalent

**Impact:** High (open world) | **Effort:** Very High | **Depends on:** Full meshlet/virtual geometry pipeline

Animated, LOD-streamed vegetation at scale.

- Skeletal mesh-driven wind animation
- Voxel-based distance representations for canopy masses
- Assembly system for instanced foliage management
- **Stretch goal** -- requires Tier 3 completion

---

## 6. Implementation Roadmap

```
Phase 1 (Foundation)
  ├── 1.1 GPU-Driven Rendering Pipeline
  │     ├── GPU scene buffer
  │     ├── Indirect draw calls
  │     ├── GPU frustum culling
  │     └── GPU occlusion culling (Hi-Z)
  │
  └── 1.2 Hardware RT Integration
        ├── A: BLAS/TLAS scene management
        ├── B: RT Reflections
        ├── C: RT Ambient Occlusion
        ├── D: RT-augmented GI (hybrid SDFGI+RT)
        └── E: RT Shadows (area lights)

Phase 2 (Visual Impact)
  ├── 2.1 Virtual Shadow Maps
  ├── 2.2 Many-Light System (stochastic)
  └── 2.3 Advanced Material System (layering)

Phase 3 (Geometry)
  ├── 3.1 Meshlet Rendering Foundation
  └── 3.2 Visibility Buffer Rendering

Phase 4 (Advanced)
  ├── 4.1 Production Path Tracer
  ├── 4.2 Custom TSR
  └── 4.3 Virtual Geometry (Nanite-like)
```

**Critical path:** Phase 1 -> Phase 2 -> Phase 3 -> Phase 4

Phase 1.2 (RT integration) can proceed in parallel with Phase 1.1 (GPU-driven) since it builds on existing driver infrastructure rather than requiring GPU-driven rendering.

Phase 2.3 (material system) can proceed independently of Phases 2.1-2.2.

---

## 7. Prototyping Strategy

### Prototype 1: GPU-Driven Indirect Draws

**Goal:** Prove that indirect draw calls can replace per-object draw calls in the forward clustered renderer.

**Approach:**
1. Modify `_fill_instance_data()` to populate a persistent GPU SSBO instead of per-frame CPU buffer
2. Build indirect draw command buffer from instance data on GPU (compute shader)
3. Replace `draw_list_draw()` calls with single `draw_list_draw_indirect()` batch
4. Benchmark: measure CPU time saved, GPU time overhead, frame time delta

**Success metric:** >30% CPU time reduction on scene with 1000+ objects.

### Prototype 2: BLAS/TLAS Auto-Generation

**Goal:** Prove that scene acceleration structures can be maintained at interactive frame rates.

**Approach:**
1. Hook into mesh loading to auto-generate BLAS per static mesh surface
2. Build TLAS from all scene instances per frame
3. Measure build time vs frame budget
4. Implement simple RT reflection pass (raygen traces reflection vector, samples environment on miss)

**Success metric:** TLAS rebuild < 1ms for 1000-object scene. Visible RT reflections in viewport.

### Prototype 3: RT Reflections

**Goal:** Visual quality comparison with SSR.

**Approach:**
1. Build on Prototype 2's BLAS/TLAS management
2. Write raygen shader that traces reflection rays from G-buffer normals
3. Closest-hit shader samples material albedo + roughness
4. Miss shader samples environment
5. Temporal accumulation (1 ray/pixel/frame, accumulate over 4-8 frames)
6. A/B comparison with SSR at matched performance budget

**Success metric:** Visually superior to SSR for off-screen reflections, curved surfaces, and multi-bounce scenarios.

### Benchmarking Methodology

All prototypes should be benchmarked on:
- **GPU:** NVIDIA RTX 3060 (representative mid-range with RT), AMD RX 7600 (RDNA3)
- **Scenes:** Godot TPS demo, Sponza, custom open-world test scene
- **Metrics:** Frame time (GPU + CPU), VRAM usage, visual quality comparison (PSNR vs ground truth)

---

## 8. Sources

### Unreal Engine 5.7
- [UE 5.7 Release Announcement](https://www.unrealengine.com/en-US/news/unreal-engine-5-7-is-now-available)
- [UE 5.7 Release Notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-release-notes)
- [UE 5.7 Five Key Features (CG Channel)](https://www.cgchannel.com/2025/11/unreal-engine-5-7-five-key-features-for-cg-artists/)
- [UE 5.7 Nanite Foliage & MegaLights (NotebookCheck)](https://www.notebookcheck.net/Unreal-Engine-5-7-brings-Nanite-Foliage-MegaLights-and-major-visual-upgrades-in-tow.1162191.0.html)
- [MegaLights Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine)
- [MegaLights: Stochastic Direct Lighting (SIGGRAPH 2025)](https://advances.realtimerendering.com/s2025/content/MegaLights_Stochastic_Direct_Lighting_2025.pdf)
- [Nanite Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)
- [Nanite Technical Breakdown (Medium)](https://medium.com/@GroundZer0/nanite-epics-practical-implementation-of-virtualized-geometry-e6a9281e7f52)
- [A Macro View of Nanite (The Code Corsair)](https://www.elopezr.com/a-macro-view-of-nanite/)
- [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)
- [Lumen GI and Reflections](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine)
- [Lumen Core Technology (Medium)](https://medium.com/@GroundZer0/lumen-core-technology-how-unreal-handles-real-time-global-illumination-09ed6a0f62be)
- [GPU-Driven Rendering Overview (Vulkan Guide)](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)

### Godot Engine
- [Godot 4.6 Key Features (CG Channel)](https://www.cgchannel.com/2026/01/discover-5-key-features-for-cg-artists-in-godot-4-6/)
- [Godot 4.6 Released (Phoronix)](https://www.phoronix.com/news/Godot-4.6-Released)
- [Godot 4.5 New Features (Digital Production)](https://digitalproduction.com/2025/09/17/godot-4-5-new-buffers-better-access-more-control/)

### Godot Source Code References (this repository)

| Component | Key File | Notable Lines/Constants |
|-----------|----------|------------------------|
| Forward Clustered Renderer | `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp` | `_render_scene()` L1679, `_fill_render_list()` L915, `_fill_instance_data()` L823 |
| Cluster Builder | `servers/rendering/renderer_rd/cluster_builder_rd.h` | `cluster_size = 32` L195, `ELEMENT_TYPE_*` L154-160 |
| Max Cluster Elements | `servers/rendering/renderer_rd/storage_rd/light_storage.h` | `max_cluster_elements = 512` L60 |
| Max Directional Lights | `servers/rendering/renderer_scene_render.h` | `MAX_DIRECTIONAL_LIGHTS = 8` L48 |
| SDFGI | `servers/rendering/renderer_rd/environment/gi.h` | `MAX_CASCADES = 8`, `CASCADE_SIZE = 128`, `MAX_DYNAMIC_LIGHTS = 128` |
| VoxelGI | `servers/rendering/renderer_rd/environment/gi.h` | `MAX_VOXEL_GI_INSTANCES = 8`, `voxel_gi_max_lights = 32` |
| Shadow Atlas | `servers/rendering/renderer_rd/storage_rd/light_storage.h` | 4 quadrants L391-419, LRU eviction |
| BaseMaterial3D | `scene/resources/material.h` | 19 texture slots L147-168, 13 features L209-224 |
| Vulkan RT API | `drivers/vulkan/rendering_device_driver_vulkan.cpp` | `blas_create()` L6098, `tlas_create()` L6237, `command_trace_rays()` L6348 |
| RenderingDevice RT | `servers/rendering/rendering_device.h` | Full public API: blas/tlas/pipeline/trace |
| RT Shader Infrastructure | `servers/rendering/renderer_rd/shader_rd.cpp` | `setup_raytracing()`, `version_set_raytracing_code()` |
| D3D12 RT (stubs) | `drivers/d3d12/rendering_device_driver_d3d12.cpp` | All RT functions return error L5490-5540 |
| Motion Vectors | `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h` | Full velocity buffer support L323-324 |
| TAA | `servers/rendering/renderer_rd/effects/taa.h/.cpp` | Compute resolve with history |
| FSR 2.0 | `servers/rendering/renderer_rd/effects/fsr2.h/.cpp` | AMD SDK integration |
| Scene Culling | `servers/rendering/renderer_scene_cull.cpp` | CPU frustum L100, occlusion L1296, visibility L2720 |
| Draw Calls | `render_forward_clustered.cpp` L607-611 | `draw_list_draw()` per surface, `draw_list_draw_indirect()` for MultiMesh only |
