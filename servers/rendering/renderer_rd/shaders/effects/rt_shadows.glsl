/* rt_shadows.glsl - Hardware ray-traced shadows for directional, omni, and spot lights.
 * Single raygen/miss shader with light_type discriminator in push constant.
 * Using one shader avoids SBT complexity; branch divergence is acceptable at Phase B scale.
 * If branch divergence becomes a measurable concern in Phase C, convert to specialization constants.
 */

#[raygen]

#version 460
#extension GL_EXT_ray_tracing : require

#define LIGHT_TYPE_DIRECTIONAL 0u
#define LIGHT_TYPE_OMNI        1u
#define LIGHT_TYPE_SPOT        2u

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;

layout(set = 1, binding = 0, std140) uniform SceneData {
	mat4 inv_projection;
	mat4 inv_view;
	mat4 reprojection;
	vec2 screen_size_inv;
	uint pad0;
	uint pad1;
} scene_data;

layout(set = 2, binding = 0) uniform sampler2D depth_buffer;
layout(set = 2, binding = 1) uniform sampler2D normal_roughness_buffer;

layout(set = 3, binding = 0, r8) uniform writeonly image2D shadow_mask;

layout(push_constant, std430) uniform PushConstant {
	ivec2 screen_size;
	uint light_type;
	uint frame_index;
	vec3 light_direction; // Normalized; directional = toward light
	float light_range;
	vec3 light_position;
	float sun_disk_angle; // Angular radius in radians (0.5 degrees = 0.008726646)
	float spot_angle;     // Spotlight cone half-angle in radians
	float temporal_blend; // written by C++ raygen push; unused here (read by accumulate pass)
	float pad[1];
} pc;

layout(location = 0) rayPayloadEXT bool in_shadow;

vec3 reconstruct_world_pos(vec2 uv, float depth) {
	vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec4 view_pos = scene_data.inv_projection * ndc;
	view_pos /= view_pos.w;
	return (scene_data.inv_view * view_pos).xyz;
}

// Simple random float from uint seed.
float rand_float(uint seed) {
	seed = seed ^ (seed << 13u);
	seed = seed ^ (seed >> 17u);
	seed = seed ^ (seed << 5u);
	return float(seed) / 4294967295.0;
}

void main() {
	ivec2 coord = ivec2(gl_LaunchIDEXT.xy);
	vec2 uv = (vec2(coord) + 0.5) / vec2(pc.screen_size);

	float depth = texelFetch(depth_buffer, coord, 0).r;
	if (depth <= 0.0001) {
		// Sky pixels are never in shadow.
		imageStore(shadow_mask, coord, vec4(1.0));
		return;
	}

	vec3 world_pos = reconstruct_world_pos(uv, depth);
	// Read surface normal to bias ray origin and prevent self-intersection (shadow acne).
	vec4 nr = texelFetch(normal_roughness_buffer, coord, 0);
	vec3 normal = normalize((scene_data.inv_view * vec4(normalize(nr.xyz * 2.0 - 1.0), 0.0)).xyz);

	uint seed = uint(coord.x) * 1973u + uint(coord.y) * 9277u + pc.frame_index * 26699u;

	vec3 shadow_ray_origin = world_pos + normal * 0.005;
	vec3 shadow_ray_dir;
	float shadow_tmax;

	if (pc.light_type == LIGHT_TYPE_DIRECTIONAL) {
		// Directional: shoot toward light direction with jitter within sun_disk_angle cone.
		vec3 base_dir = normalize(-pc.light_direction);
		float jitter_x = (rand_float(seed) - 0.5) * 2.0 * pc.sun_disk_angle;
		float jitter_y = (rand_float(seed + 1u) - 0.5) * 2.0 * pc.sun_disk_angle;
		vec3 up = abs(base_dir.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
		vec3 right = normalize(cross(up, base_dir));
		vec3 up2 = cross(base_dir, right);
		shadow_ray_dir = normalize(base_dir + right * jitter_x + up2 * jitter_y);
		shadow_tmax = 10000.0;

	} else if (pc.light_type == LIGHT_TYPE_OMNI) {
		// Omni: sample a random point on the light sphere.
		float theta = rand_float(seed) * 6.28318530;
		float phi = acos(1.0 - 2.0 * rand_float(seed + 1u));
		// light_range stores the light sphere radius.
		vec3 sample_point = pc.light_position + pc.light_range * vec3(
				sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
		vec3 to_light = sample_point - world_pos;
		shadow_tmax = length(to_light);
		shadow_ray_dir = to_light / shadow_tmax;
		shadow_tmax -= 0.001; // Don't hit the light itself.

	} else { // LIGHT_TYPE_SPOT
		// Spot: sample a random point within the spotlight cone.
		vec3 spot_dir = normalize(-pc.light_direction);
		float cos_half_angle = cos(pc.spot_angle);
		float r1 = rand_float(seed);
		float r2 = rand_float(seed + 1u);
		float cos_theta = mix(cos_half_angle, 1.0, r1);
		float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
		float phi = r2 * 6.28318530;
		vec3 up = abs(spot_dir.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
		vec3 right = normalize(cross(up, spot_dir));
		vec3 up2 = cross(spot_dir, right);
		vec3 cone_dir = normalize(spot_dir * cos_theta + right * sin_theta * cos(phi) + up2 * sin_theta * sin(phi));
		float dist = rand_float(seed + 2u) * pc.light_range;
		vec3 sample_point = pc.light_position + cone_dir * dist;
		vec3 to_light = sample_point - world_pos;
		shadow_tmax = length(to_light);
		shadow_ray_dir = to_light / shadow_tmax;
		shadow_tmax = min(shadow_tmax - 0.001, pc.light_range);
	}

	in_shadow = true; // Assume shadowed; miss shader clears this.

	traceRayEXT(
		tlas,
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
		0xFF,
		0, 0,
		0,
		shadow_ray_origin,
		0.001,
		shadow_ray_dir,
		shadow_tmax,
		0);

	// Write raw shadow value. TAA/temporal blending is handled by the separate
	// rt_shadows_accumulate.glsl compute pass.
	float shadow_value = in_shadow ? 0.0 : 1.0;
	imageStore(shadow_mask, coord, vec4(shadow_value, 0.0, 0.0, 0.0));
}

#[miss]

#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT bool in_shadow;

void main() {
	// Ray reached tmax without hitting any occluder -- surface is lit.
	in_shadow = false;
}
