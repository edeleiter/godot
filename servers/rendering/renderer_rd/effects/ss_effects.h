/**************************************************************************/
/*  ss_effects.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_ao.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_ao_accumulate.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_reflections.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_reflections_accumulate.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_shadows.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_shadows_accumulate.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_downsample.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_filter.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_hiz.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/screen_space_reflection_resolve.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ss_effects_downsample.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao_blur.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao_importance_map.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssao_interleave.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil_blur.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil_importance_map.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/ssil_interleave.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/subsurface_scattering.glsl.gen.h"
#include "servers/rendering/rendering_server.h"

#define RB_SCOPE_SSLF SNAME("rb_sslf")
#define RB_SCOPE_SSDS SNAME("rb_ssds")
#define RB_SCOPE_SSIL SNAME("rb_ssil")
#define RB_SCOPE_SSAO SNAME("rb_ssao")
#define RB_SCOPE_SSR SNAME("rb_ssr")
#define RB_SCOPE_RTREFL SNAME("rb_rtrefl")
#define RB_SCOPE_RTAO SNAME("rb_rtao")

#define RB_RT_CURRENT SNAME("rt_current")
#define RB_RT_HISTORY SNAME("rt_history")
#define RB_RT_PING SNAME("rt_ping")

#define RB_LINEAR_DEPTH SNAME("linear_depth")
#define RB_FINAL SNAME("final")
#define RB_LAST_FRAME SNAME("last_frame")
#define RB_DEINTERLEAVED SNAME("deinterleaved")
#define RB_DEINTERLEAVED_PONG SNAME("deinterleaved_pong")
#define RB_EDGES SNAME("edges")
#define RB_IMPORTANCE_MAP SNAME("importance_map")
#define RB_IMPORTANCE_PONG SNAME("importance_pong")

#define RB_NORMAL_ROUGHNESS SNAME("normal_roughness")
#define RB_HIZ SNAME("hiz")
#define RB_SSR SNAME("ssr")
#define RB_MIP_LEVEL SNAME("mip_level")

class RenderSceneBuffersRD;

namespace RendererRD {

class CopyEffects;

class SSEffects {
private:
	static SSEffects *singleton;

public:
	static SSEffects *get_singleton() { return singleton; }

	SSEffects();
	~SSEffects();

	/* Last Frame */

	void allocate_last_frame_buffer(Ref<RenderSceneBuffersRD> p_render_buffers, bool p_use_ssil, bool p_use_ssr);
	void copy_internal_texture_to_last_frame(Ref<RenderSceneBuffersRD> p_render_buffers, CopyEffects &p_copy_effects);

	/* SS Downsampler */

	void downsample_depth(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, const Projection &p_projection);

	/* SSIL */
	void ssil_set_quality(RS::EnvironmentSSILQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to);

	struct SSILRenderBuffers {
		bool half_size = false;
		int buffer_width;
		int buffer_height;
		int half_buffer_width;
		int half_buffer_height;
	};

	struct SSILSettings {
		float radius = 1.0;
		float intensity = 2.0;
		float sharpness = 0.98;
		float normal_rejection = 1.0;

		Size2i full_screen_size;
	};

	void ssil_allocate_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, SSILRenderBuffers &p_ssil_buffers, const SSILSettings &p_settings);
	void screen_space_indirect_lighting(Ref<RenderSceneBuffersRD> p_render_buffers, SSILRenderBuffers &p_ssil_buffers, uint32_t p_view, RID p_normal_buffer, const Projection &p_projection, const Projection &p_last_projection, const SSILSettings &p_settings);

	/* SSAO */
	void ssao_set_quality(RS::EnvironmentSSAOQuality p_quality, bool p_half_size, float p_adaptive_target, int p_blur_passes, float p_fadeout_from, float p_fadeout_to);

	struct SSAORenderBuffers {
		bool half_size = false;
		int buffer_width;
		int buffer_height;
		int half_buffer_width;
		int half_buffer_height;
	};

	struct SSAOSettings {
		float radius = 1.0;
		float intensity = 2.0;
		float power = 1.5;
		float detail = 0.5;
		float horizon = 0.06;
		float sharpness = 0.98;

		Size2i full_screen_size;
	};

	void ssao_allocate_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, SSAORenderBuffers &p_ssao_buffers, const SSAOSettings &p_settings);
	void generate_ssao(Ref<RenderSceneBuffersRD> p_render_buffers, SSAORenderBuffers &p_ssao_buffers, uint32_t p_view, RID p_normal_buffer, const Projection &p_projection, const SSAOSettings &p_settings);

	/* Screen Space Reflection */
	void ssr_set_half_size(bool p_half_size);

	struct SSRRenderBuffers {
		Size2i size;
		uint32_t mipmaps = 1;
		bool half_size = false;
	};

	void ssr_allocate_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, SSRRenderBuffers &p_ssr_buffers, const RD::DataFormat p_color_format);
	void screen_space_reflection(Ref<RenderSceneBuffersRD> p_render_buffers, SSRRenderBuffers &p_ssr_buffers, const RID *p_normal_roughness_slices, int p_max_steps, float p_fade_in, float p_fade_out, float p_tolerance, const Projection *p_projections, const Projection *p_reprojections, const Vector3 *p_eye_offsets, RendererRD::CopyEffects &p_copy_effects);

	// TODO: Extract RTReflections, RTAO, RTShadows to a dedicated RTEffects class
	// when this section exceeds ~500 lines.

	/* RT Reflections */

	enum RTReflectionQuality {
		RT_REFLECTION_QUALITY_LOW = 0,
		RT_REFLECTION_QUALITY_MEDIUM,
		RT_REFLECTION_QUALITY_HIGH,
	};

	struct RTReflectionsPushConstant {
		int32_t screen_size[2];
		uint32_t quality;
		uint32_t frame_index;
		float roughness_cutoff;
		float temporal_blend;
		float pad[2];
	};
	static_assert(sizeof(RTReflectionsPushConstant) <= 128);

	struct RTReflectionsAccumulatePushConstant {
		int32_t screen_size[2];
		float temporal_blend;
		float velocity_rejection_threshold;
	};
	static_assert(sizeof(RTReflectionsAccumulatePushConstant) <= 128);

	void rt_reflections_allocate_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_internal_width, uint32_t p_internal_height);
	void rt_screen_reflection(Ref<RenderSceneBuffersRD> p_render_buffers,
			RID p_tlas, RID p_sky_texture, RID p_normal_roughness, RID p_depth, RID p_velocity,
			const Vector<Color> &p_instance_base_colors,
			RTReflectionQuality p_quality,
			const Projection &p_projection, const Projection &p_reprojection,
			const Transform3D &p_view_transform, uint32_t p_frame_index,
			uint32_t p_tlas_instance_count = 0);

	/* RT Ambient Occlusion */

	struct RTAOPushConstant {
		int32_t screen_size[2];
		uint32_t frame_index;
		float max_distance;
		float temporal_blend;
		float pad[3];
	};
	static_assert(sizeof(RTAOPushConstant) <= 128);

	struct RTAOAccumulatePushConstant {
		int32_t screen_size[2];
		float temporal_blend;
		float velocity_rejection_threshold;
	};
	static_assert(sizeof(RTAOAccumulatePushConstant) <= 128);

	void rt_ao_allocate_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_internal_width, uint32_t p_internal_height);
	void rt_ambient_occlusion(Ref<RenderSceneBuffersRD> p_render_buffers,
			RID p_tlas, RID p_normal_roughness, RID p_depth, RID p_velocity,
			float p_max_distance,
			const Projection &p_projection, const Projection &p_reprojection,
			const Transform3D &p_view_transform, uint32_t p_frame_index);

	/* RT Shadows */

	enum RTShadowsLightType {
		RT_SHADOWS_LIGHT_DIRECTIONAL = 0,
		RT_SHADOWS_LIGHT_OMNI = 1,
		RT_SHADOWS_LIGHT_SPOT = 2,
	};

	struct RTShadowsPushConstant {
		int32_t screen_size[2];
		uint32_t light_type;
		uint32_t frame_index;
		float light_direction[3];
		float light_range;
		float light_position[3];
		float sun_disk_angle;
		float spot_angle;
		float temporal_blend;
		float light_size; // physical light radius for soft shadow sphere sampling
	};
	static_assert(sizeof(RTShadowsPushConstant) <= 128);

	struct RTShadowsAccumulatePushConstant {
		int32_t screen_size[2];
		float temporal_blend;
		float velocity_rejection_threshold;
	};
	static_assert(sizeof(RTShadowsAccumulatePushConstant) <= 128);

	struct RTShadowsHistoryEntry {
		RID raw;         // raygen write target (r8, STORAGE+SAMPLING+COPY)
		RID accum_hist;  // TAA history (r8, STORAGE+SAMPLING+COPY)
		RID accum_out;   // accumulate output (r8, STORAGE+SAMPLING+COPY)
		uint64_t last_used_frame = 0;

		void free_gpu_resources();
	};

	struct RTShadowsRenderBuffers {
		// Fixed 16-slot LRU pool: one r8 history texture per light RID.
		// Entries older than EVICTION_GRACE_FRAMES are evicted when pool is full.
		HashMap<RID, RTShadowsHistoryEntry> history_pool;
		static constexpr int MAX_HISTORY_ENTRIES = 16;
		static constexpr uint64_t EVICTION_GRACE_FRAMES = 60;
	};

	void rt_shadow_allocate_buffers(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_internal_width, uint32_t p_internal_height);

	// Named rt_shadow_dispatch() to avoid collision with rt_shadows struct member (M3).
	void rt_shadow_dispatch(Ref<RenderSceneBuffersRD> p_render_buffers,
			RTShadowsRenderBuffers &p_rt_shadow_buffers,
			RID p_tlas, RID p_depth, RID p_normal_roughness, RID p_velocity,
			RID p_light, RTShadowsLightType p_light_type,
			const Vector3 &p_light_direction, const Vector3 &p_light_position,
			float p_light_range, float p_light_size, float p_sun_disk_angle, float p_spot_angle,
			const Projection &p_projection, const Projection &p_reprojection,
			const Transform3D &p_view_transform,
			uint32_t p_frame_index, uint64_t p_current_frame);

	/* subsurface scattering */
	void sss_set_quality(RS::SubSurfaceScatteringQuality p_quality);
	RS::SubSurfaceScatteringQuality sss_get_quality() const;
	void sss_set_scale(float p_scale, float p_depth_scale);

	void sub_surface_scattering(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_diffuse, RID p_depth, const Projection &p_camera, const Size2i &p_screen_size);

private:
	/* Settings */

	RS::EnvironmentSSAOQuality ssao_quality = RS::ENV_SSAO_QUALITY_MEDIUM;
	bool ssao_half_size = false;
	float ssao_adaptive_target = 0.5;
	int ssao_blur_passes = 2;
	float ssao_fadeout_from = 50.0;
	float ssao_fadeout_to = 300.0;

	RS::EnvironmentSSILQuality ssil_quality = RS::ENV_SSIL_QUALITY_MEDIUM;
	bool ssil_half_size = false;
	float ssil_adaptive_target = 0.5;
	int ssil_blur_passes = 4;
	float ssil_fadeout_from = 50.0;
	float ssil_fadeout_to = 300.0;

	bool ssr_half_size = false;

	RS::SubSurfaceScatteringQuality sss_quality = RS::SUB_SURFACE_SCATTERING_QUALITY_MEDIUM;
	float sss_scale = 0.05;
	float sss_depth_scale = 0.01;

	/* RT Scene Data UBO — shared by all three RT effects. Matches the GLSL SceneData layout. */
	struct RTSceneDataUBO {
		float inv_projection[16]; // mat4
		float inv_view[16]; // mat4
		float reprojection[16]; // mat4
		float screen_size_inv[2]; // vec2
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
	};
	static_assert(sizeof(RTSceneDataUBO) == 208,
			"RTSceneDataUBO must match the GPU UBO layout in rt_*.glsl shaders.");

	/* SS Downsampler */

	struct SSEffectsDownsamplePushConstant {
		float pixel_size[2];
		float z_far;
		float z_near;
		uint32_t orthogonal;
		float radius_sq;
		uint32_t pad[2];
	};

	enum SSEffectsMode {
		SS_EFFECTS_DOWNSAMPLE,
		SS_EFFECTS_DOWNSAMPLE_HALF_RES,
		SS_EFFECTS_DOWNSAMPLE_MIPMAP,
		SS_EFFECTS_DOWNSAMPLE_MIPMAP_HALF_RES,
		SS_EFFECTS_DOWNSAMPLE_HALF,
		SS_EFFECTS_DOWNSAMPLE_HALF_RES_HALF,
		SS_EFFECTS_DOWNSAMPLE_FULL_MIPS,
		SS_EFFECTS_MAX
	};

	struct SSEffectsGatherConstants {
		float rotation_matrices[80]; //5 vec4s * 4
	};

	struct SSEffectsShader {
		SSEffectsDownsamplePushConstant downsample_push_constant;
		SsEffectsDownsampleShaderRD downsample_shader;
		RID downsample_shader_version;
		bool used_half_size_last_frame = false;
		bool used_mips_last_frame = false;
		bool used_full_mips_last_frame = false;

		RID gather_constants_buffer;

		RID mirror_sampler;

		PipelineDeferredRD pipelines[SS_EFFECTS_MAX];
	} ss_effects;

	/* SSIL */

	enum SSILMode {
		SSIL_GATHER,
		SSIL_GATHER_BASE,
		SSIL_GATHER_ADAPTIVE,
		SSIL_GENERATE_IMPORTANCE_MAP,
		SSIL_PROCESS_IMPORTANCE_MAPA,
		SSIL_PROCESS_IMPORTANCE_MAPB,
		SSIL_BLUR_PASS,
		SSIL_BLUR_PASS_SMART,
		SSIL_BLUR_PASS_WIDE,
		SSIL_INTERLEAVE,
		SSIL_INTERLEAVE_SMART,
		SSIL_INTERLEAVE_HALF,
		SSIL_MAX
	};

	struct SSILGatherPushConstant {
		int32_t screen_size[2];
		int pass;
		int quality;

		float half_screen_pixel_size[2];
		float half_screen_pixel_size_x025[2];

		float NDC_to_view_mul[2];
		float NDC_to_view_add[2];

		float pad2[2];
		float z_near;
		float z_far;

		float radius;
		float intensity;
		int size_multiplier;
		int pad;

		float fade_out_mul;
		float fade_out_add;
		float normal_rejection_amount;
		float inv_radius_near_limit;

		uint32_t is_orthogonal;
		float neg_inv_radius;
		float load_counter_avg_div;
		float adaptive_sample_limit;

		int32_t pass_coord_offset[2];
		float pass_uv_offset[2];
	};

	struct SSILImportanceMapPushConstant {
		float half_screen_pixel_size[2];
		float intensity;
		float pad;
	};

	struct SSILBlurPushConstant {
		float edge_sharpness;
		float pad;
		float half_screen_pixel_size[2];
	};

	struct SSILInterleavePushConstant {
		float inv_sharpness;
		uint32_t size_modifier;
		float pixel_size[2];
	};

	struct SSILProjectionUniforms {
		float inv_last_frame_projection_matrix[16];
	};

	struct SSIL {
		SSILGatherPushConstant gather_push_constant;
		SsilShaderRD gather_shader;
		RID gather_shader_version;
		RID projection_uniform_buffer;

		SSILImportanceMapPushConstant importance_map_push_constant;
		SsilImportanceMapShaderRD importance_map_shader;
		RID importance_map_shader_version;
		RID importance_map_load_counter;
		RID counter_uniform_set;

		SSILBlurPushConstant blur_push_constant;
		SsilBlurShaderRD blur_shader;
		RID blur_shader_version;

		SSILInterleavePushConstant interleave_push_constant;
		SsilInterleaveShaderRD interleave_shader;
		RID interleave_shader_version;

		PipelineDeferredRD pipelines[SSIL_MAX];
	} ssil;

	void gather_ssil(RD::ComputeListID p_compute_list, const RID *p_ssil_slices, const RID *p_edges_slices, const SSILSettings &p_settings, bool p_adaptive_base_pass, RID p_gather_uniform_set, RID p_importance_map_uniform_set, RID p_projection_uniform_set);

	/* SSAO */

	enum SSAOMode {
		SSAO_GATHER,
		SSAO_GATHER_BASE,
		SSAO_GATHER_ADAPTIVE,
		SSAO_GENERATE_IMPORTANCE_MAP,
		SSAO_PROCESS_IMPORTANCE_MAPA,
		SSAO_PROCESS_IMPORTANCE_MAPB,
		SSAO_BLUR_PASS,
		SSAO_BLUR_PASS_SMART,
		SSAO_BLUR_PASS_WIDE,
		SSAO_INTERLEAVE,
		SSAO_INTERLEAVE_SMART,
		SSAO_INTERLEAVE_HALF,
		SSAO_MAX
	};

	struct SSAOGatherPushConstant {
		int32_t screen_size[2];
		int pass;
		int quality;

		float half_screen_pixel_size[2];
		int size_multiplier;
		float detail_intensity;

		float NDC_to_view_mul[2];
		float NDC_to_view_add[2];

		float pad[2];
		float half_screen_pixel_size_x025[2];

		float radius;
		float intensity;
		float shadow_power;
		float shadow_clamp;

		float fade_out_mul;
		float fade_out_add;
		float horizon_angle_threshold;
		float inv_radius_near_limit;

		uint32_t is_orthogonal;
		float neg_inv_radius;
		float load_counter_avg_div;
		float adaptive_sample_limit;

		int32_t pass_coord_offset[2];
		float pass_uv_offset[2];
	};

	struct SSAOImportanceMapPushConstant {
		float half_screen_pixel_size[2];
		float intensity;
		float power;
	};

	struct SSAOBlurPushConstant {
		float edge_sharpness;
		float pad;
		float half_screen_pixel_size[2];
	};

	struct SSAOInterleavePushConstant {
		float inv_sharpness;
		uint32_t size_modifier;
		float pixel_size[2];
	};

	struct SSAO {
		SSAOGatherPushConstant gather_push_constant;
		SsaoShaderRD gather_shader;
		RID gather_shader_version;

		SSAOImportanceMapPushConstant importance_map_push_constant;
		SsaoImportanceMapShaderRD importance_map_shader;
		RID importance_map_shader_version;
		RID importance_map_load_counter;
		RID counter_uniform_set;

		SSAOBlurPushConstant blur_push_constant;
		SsaoBlurShaderRD blur_shader;
		RID blur_shader_version;

		SSAOInterleavePushConstant interleave_push_constant;
		SsaoInterleaveShaderRD interleave_shader;
		RID interleave_shader_version;

		PipelineDeferredRD pipelines[SSAO_MAX];
	} ssao;

	void gather_ssao(RD::ComputeListID p_compute_list, const RID *p_ao_slices, const SSAOSettings &p_settings, bool p_adaptive_base_pass, RID p_gather_uniform_set, RID p_importance_map_uniform_set);

	/* Screen Space Reflection */

	enum ScreenSpaceReflectionDownsampleMode {
		SCREEN_SPACE_REFLECTION_DOWNSAMPLE_DEFAULT,
		SCREEN_SPACE_REFLECTION_DOWNSAMPLE_ODD_WIDTH,
		SCREEN_SPACE_REFLECTION_DOWNSAMPLE_ODD_HEIGHT,
		SCREEN_SPACE_REFLECTION_DOWNSAMPLE_ODD_WIDTH_AND_HEIGHT,
		SCREEN_SPACE_REFLECTION_DOWNSAMPLE_MAX
	};

	struct ScreenSpaceReflectionDownsamplePushConstant {
		int32_t screen_size[2];
		int32_t pad[2];
	};

	enum ScreenSpaceReflectionHizMode {
		SCREEN_SPACE_REFLECTION_HIZ_DEFAULT,
		SCREEN_SPACE_REFLECTION_HIZ_ODD_WIDTH,
		SCREEN_SPACE_REFLECTION_HIZ_ODD_HEIGHT,
		SCREEN_SPACE_REFLECTION_HIZ_ODD_WIDTH_AND_HEIGHT,
		SCREEN_SPACE_REFLECTION_HIZ_MAX
	};

	struct ScreenSpaceReflectionHizPushConstant {
		int32_t screen_size[2];
		int32_t pad[2];
	};

	struct ScreenSpaceReflectionSceneData {
		float projection[2][16];
		float inv_projection[2][16];
		float reprojection[2][16];
		float eye_offset[2][4];
	};

	struct ScreenSpaceReflectionPushConstant {
		int32_t screen_size[2];
		int32_t mipmaps;
		int32_t num_steps;
		float distance_fade;
		float curve_fade_in;
		float depth_tolerance;
		int32_t orthogonal;
		uint32_t view_index;
		int32_t pad[3];
	};

	struct ScreenSpaceReflectionFilterPushConstant {
		int32_t screen_size[2];
		uint32_t mip_level;
		int32_t pad;
	};

	struct ScreenSpaceReflectionResolvePushConstant {
		int32_t screen_size[2];
		int32_t pad[2];
	};

	struct ScreenSpaceReflection {
		ScreenSpaceReflectionDownsampleShaderRD downsample_shader;
		RID downsample_shader_version;
		PipelineDeferredRD downsample_pipelines[SCREEN_SPACE_REFLECTION_DOWNSAMPLE_MAX];

		ScreenSpaceReflectionHizShaderRD hiz_shader;
		RID hiz_shader_version;
		PipelineDeferredRD hiz_pipelines[SCREEN_SPACE_REFLECTION_HIZ_MAX];

		ScreenSpaceReflectionShaderRD ssr_shader;
		RID ssr_shader_version;
		PipelineDeferredRD ssr_pipeline;
		RID ubo;

		ScreenSpaceReflectionFilterShaderRD filter_shader;
		RID filter_shader_version;
		PipelineDeferredRD filter_pipeline;

		ScreenSpaceReflectionResolveShaderRD resolve_shader;
		RID resolve_shader_version;
		PipelineDeferredRD resolve_pipeline;
	} ssr;

	/* Subsurface scattering */

	enum SSSMode {
		SUBSURFACE_SCATTERING_MODE_LOW_QUALITY,
		SUBSURFACE_SCATTERING_MODE_MEDIUM_QUALITY,
		SUBSURFACE_SCATTERING_MODE_HIGH_QUALITY,
		SUBSURFACE_SCATTERING_MODE_MAX
	};

	struct SubSurfaceScatteringPushConstant {
		int32_t screen_size[2];
		float camera_z_far;
		float camera_z_near;

		uint32_t vertical;
		uint32_t orthogonal;
		float unit_size;
		float scale;

		float depth_scale;
		uint32_t pad[3];
	};

	struct SubSurfaceScattering {
		SubSurfaceScatteringPushConstant push_constant;
		SubsurfaceScatteringShaderRD shader;
		RID shader_version;
		PipelineDeferredRD pipelines[SUBSURFACE_SCATTERING_MODE_MAX];
	} sss;

	/* Set to true only when RT shaders compiled successfully (RT hardware present). */
	bool rt_shaders_valid = false;

	/* Lazy RT init: all RT pipeline/texture creation is deferred to first use. */
	bool rt_initialized = false;
	void _ensure_rt_initialized();

	/* RT Reflections private backing */
	struct RTReflectionsData {
		RtReflectionsShaderRD shader;
		RID shader_version;
		RID pipeline;
		RtReflectionsAccumulateShaderRD accumulate_shader;
		RID accumulate_shader_version;
		RID accumulate_pipeline;
		RID instance_color_buffer; // per-TLAS-entry base color SSBO (vec4[])
		uint32_t instance_color_buffer_count = 0;
		RID scene_data_ubo; // RTSceneDataUBO: inv_projection + inv_view + reprojection + screen_size_inv
	} rt_reflections;

	/* RT AO private backing */
	struct RTAOData {
		RtAoShaderRD shader;
		RID shader_version;
		RID pipeline;
		RtAoAccumulateShaderRD accumulate_shader;
		RID accumulate_shader_version;
		RID accumulate_pipeline;
		RID scene_data_ubo; // RTSceneDataUBO: inv_projection + inv_view + reprojection + screen_size_inv
	} rt_ao;

	/* RT Shadows private backing */
	struct RTShadowsData {
		RtShadowsShaderRD shader;
		RID shader_version;
		RID pipeline;
		RtShadowsAccumulateShaderRD accumulate_shader;
		RID accumulate_shader_version;
		RID accumulate_pipeline;
		RID scene_data_ubo; // RTSceneDataUBO: inv_projection + inv_view + reprojection + screen_size_inv
	} rt_shadows;

	// 1x1 black R16G16_SFLOAT STORAGE texture used as a safe fallback when no
	// velocity buffer is available. Prevents Vulkan validation errors from
	// passing RID() to a UNIFORM_TYPE_IMAGE binding.
	RID rt_velocity_fallback;

	// Engine-default black cubemap RID, cached from TextureStorage on first RT init.
	// Used as a safe fallback sky texture when no sky is configured.
	RID rt_sky_fallback_tex;
};

} // namespace RendererRD
