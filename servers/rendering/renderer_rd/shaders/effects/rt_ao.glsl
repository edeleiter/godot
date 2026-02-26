/* rt_ao.glsl - Hardware ray-traced ambient occlusion.
 * Uses gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT
 * for efficiency -- any occluder terminates the ray without invoking chit shader.
 * Stratified temporal accumulation: 8 cosine-weighted Halton hemisphere samples,
 * one per frame, rotated by frame_index for temporal coverage.
 */

#[raygen]

#version 460
#extension GL_EXT_ray_tracing : require

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

layout(set = 3, binding = 0, r8) uniform writeonly image2D output_ao;

layout(push_constant, std430) uniform PushConstant {
	ivec2 screen_size;
	uint frame_index; // 0-7: selects the Halton sample for this frame
	float max_distance;
	float temporal_blend; // written by C++ raygen push; unused here (read by accumulate pass)
	float pad[3];
} pc;

layout(location = 0) rayPayloadEXT bool occluded;

// Reconstruct world-space position from depth.
vec3 reconstruct_world_pos(vec2 uv, float depth) {
	vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec4 view_pos = scene_data.inv_projection * ndc;
	view_pos /= view_pos.w;
	return (scene_data.inv_view * view_pos).xyz;
}

// Precomputed 8-sample Halton hemisphere (cosine-weighted).
// Rotated by frame_index for stratified temporal accumulation.
vec3 halton_hemisphere_sample(uint index, vec3 normal) {
	const vec2 halton[8] = vec2[8](
		vec2(0.500, 0.125), vec2(0.250, 0.375),
		vec2(0.750, 0.625), vec2(0.125, 0.875),
		vec2(0.625, 0.250), vec2(0.375, 0.500),
		vec2(0.875, 0.750), vec2(0.0625, 0.03125));
	uint i = index % 8u;
	float phi = halton[i].x * 6.28318530;
	float cos_theta = sqrt(halton[i].y); // Cosine-weighted
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
	// Build TBN from normal.
	vec3 up = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
	vec3 tangent = normalize(cross(up, normal));
	vec3 bitangent = cross(normal, tangent);
	return normalize(tangent * h.x + bitangent * h.y + normal * h.z);
}

void main() {
	ivec2 coord = ivec2(gl_LaunchIDEXT.xy);
	vec2 uv = (vec2(coord) + 0.5) / vec2(pc.screen_size);

	float depth = texelFetch(depth_buffer, coord, 0).r;
	if (depth <= 0.0001) {
		imageStore(output_ao, coord, vec4(1.0)); // Sky is unoccluded.
		return;
	}

	vec4 normal_roughness = texelFetch(normal_roughness_buffer, coord, 0);
	vec3 normal = normalize((scene_data.inv_view * vec4(normalize(normal_roughness.xyz * 2.0 - 1.0), 0.0)).xyz);
	vec3 world_pos = reconstruct_world_pos(uv, depth);

	// Select hemisphere sample for this frame (stratified temporal).
	// Add per-pixel hash to avoid structured noise patterns.
	uint seed = uint(coord.x) * 1973u + uint(coord.y) * 9277u + pc.frame_index * 26699u;
	vec3 ray_dir = halton_hemisphere_sample(pc.frame_index + (seed >> 16u), normal);

	// Bias ray origin along surface normal to prevent self-intersection.
	vec3 ray_origin = world_pos + normal * 0.01;

	occluded = true; // Assume occluded; miss shader clears this.

	traceRayEXT(
		tlas,
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
		0xFF,
		0, 0,
		0,          // miss index
		ray_origin,
		0.001,      // tmin
		ray_dir,
		pc.max_distance,
		0           // payload location 0
	);

	// Write raw AO value. TAA/temporal blending is handled by the separate
	// rt_ao_accumulate.glsl compute pass.
	float ao_value = occluded ? 0.0 : 1.0;
	imageStore(output_ao, coord, vec4(ao_value, 0.0, 0.0, 0.0));
}

#[miss]

#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT bool occluded;

void main() {
	// Ray reached max_distance without hitting anything -- surface is unoccluded.
	occluded = false;
}
