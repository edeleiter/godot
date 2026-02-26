/* rt_reflections.glsl - Hardware ray-traced reflections.
 * Phase B RT effect: uses TLAS from RTSceneManager.
 * Raygen writes raw traced color only -- all temporal accumulation is done
 * by the separate rt_reflections_accumulate.glsl compute shader.
 * Closest-hit uses gl_InstanceCustomIndexEXT (NOT gl_InstanceID) to index
 * into instance_base_colors[] -- the 24-bit custom index is set per TLAS entry
 * in tlas_instances_buffer_fill() and matches the SSBO fill order.
 */

#[raygen]

#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 1) uniform samplerCube sky_texture;

layout(set = 1, binding = 0, std140) uniform SceneData {
	mat4 inv_projection;
	mat4 inv_view;
	mat4 reprojection; // retained for UBO layout compatibility
	vec2 screen_size_inv;
	uint pad0;
	uint pad1;
} scene_data;

layout(set = 2, binding = 0) uniform sampler2D depth_buffer;
layout(set = 2, binding = 1) uniform sampler2D normal_roughness_buffer;

// Set 3: raw output only -- no history/velocity (handled by accumulate pass).
layout(set = 3, binding = 0, rgba16f) uniform writeonly image2D output_color;

layout(set = 4, binding = 0, std430) readonly buffer InstanceBaseColors {
	vec4 colors[];
} instance_base_colors;

layout(push_constant, std430) uniform PushConstant {
	ivec2 screen_size;
	uint quality;       // 0=LOW 1=MEDIUM 2=HIGH
	uint frame_index;
	float roughness_cutoff;
	float temporal_blend; // written by C++ raygen push; unused here (read by accumulate pass)
	float pad[2];
} pc;

layout(location = 0) rayPayloadEXT vec4 hit_color;

// Reconstruct world-space position from depth buffer sample.
vec3 reconstruct_world_pos(vec2 uv, float depth) {
	vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec4 view_pos = scene_data.inv_projection * ndc;
	view_pos /= view_pos.w;
	vec4 world_pos = scene_data.inv_view * view_pos;
	return world_pos.xyz;
}

// Importance-sample GGX lobe for rough reflections (MEDIUM/HIGH quality).
vec3 importance_sample_ggx(vec2 xi, vec3 normal, float roughness) {
	float a = roughness * roughness;
	float phi = 2.0 * 3.14159265 * xi.x;
	float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
	vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
	vec3 up = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
	vec3 tangent = normalize(cross(up, normal));
	vec3 bitangent = cross(normal, tangent);
	return normalize(tangent * h.x + bitangent * h.y + normal * h.z);
}

void main() {
	ivec2 coord = ivec2(gl_LaunchIDEXT.xy);
	vec2 uv = (vec2(coord) + 0.5) / vec2(pc.screen_size);

	float depth = texelFetch(depth_buffer, coord, 0).r;
	// Skip sky pixels (no geometry to reflect from).
	if (depth <= 0.0001) {
		imageStore(output_color, coord, vec4(0.0));
		return;
	}

	vec4 normal_roughness = texelFetch(normal_roughness_buffer, coord, 0);
	// Decode roughness: the normal_roughness buffer encodes a dynamic/static flag.
	// Static: roughness * 127/255 (range 0..0.498). Dynamic: 1.0 - roughness * 127/255 (0.502..1.0).
	float roughness = normal_roughness.w;
	if (roughness > 0.5) {
		roughness = 1.0 - roughness;
	}
	roughness *= 255.0 / 127.0; // Recover original [0,1] roughness.

	// Skip surfaces rougher than the cutoff (they use IBL instead).
	if (roughness > pc.roughness_cutoff) {
		imageStore(output_color, coord, vec4(0.0));
		return;
	}

	// === DEBUG TEST A: Depth buffer visualization ===
	// Expected: Smooth grayscale. Near=white (reversed-Z), far=dark, sky=black.
	// If fragmented -> depth buffer is wrong or misbound.
	imageStore(output_color, coord, vec4(depth, depth, depth, 1.0));
	return;
	// === END DEBUG ===

	vec3 world_pos = reconstruct_world_pos(uv, depth);
	vec3 normal = normalize((scene_data.inv_view * vec4(normalize(normal_roughness.xyz * 2.0 - 1.0), 0.0)).xyz);
	vec3 camera_pos = (scene_data.inv_view * vec4(0, 0, 0, 1)).xyz;
	vec3 view_dir = normalize(world_pos - camera_pos);

	vec3 reflect_dir;
	if (pc.quality == 0u) {
		// LOW: perfect mirror reflection.
		reflect_dir = reflect(view_dir, normal);
	} else {
		// MEDIUM/HIGH: importance-sample GGX lobe.
		// Use frame_index + pixel hash for stratified sampling.
		// 2D hash with XOR mixing to avoid linear iso-line bias from additive seeds.
		// The old linear seed (coord.x * 1973 + coord.y * 9277) created diagonal
		// stripe artifacts because constant-sum iso-lines form parallel lines.
		uint seed = uint(coord.x) ^ (uint(coord.y) * 0x1f1f1f1fu);
		seed += pc.frame_index * 26699u;
		seed ^= seed >> 16u;
		seed *= 0x45d9f3bu;
		seed ^= seed >> 16u;
		seed *= 0x45d9f3bu;
		seed ^= seed >> 16u;
		float xi_x = float(seed & 0xFFFFu) / 65535.0;
		seed *= 0x119de1f3u;
		seed ^= seed >> 16u;
		float xi_y = float(seed & 0xFFFFu) / 65535.0;
		vec3 h = importance_sample_ggx(vec2(xi_x, xi_y), normal, roughness);
		reflect_dir = reflect(view_dir, h);
	}

	// Ensure reflection doesn't go below the surface.
	if (dot(reflect_dir, normal) < 0.001) {
		reflect_dir = reflect(view_dir, normal);
	}

	// Bias ray origin along surface normal to prevent self-intersection.
	// At grazing angles the depth-reconstructed world_pos can sit exactly on
	// (or slightly inside) the surface; tmin alone is insufficient because the
	// reflected ray is nearly parallel to the surface.
	vec3 ray_origin = world_pos + normal * 0.01;

	hit_color = vec4(0.0);
	traceRayEXT(
		tlas,
		gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
		0xFF,    // cull mask: all instances
		0,       // sbt record offset (hit group 0)
		0,       // sbt record stride
		0,       // miss index
		ray_origin,
		0.001,   // tmin
		reflect_dir,
		1000.0,  // tmax
		0        // payload location 0
	);

	// Write raw traced color. TAA/temporal blending is done by accumulate pass.
	imageStore(output_color, coord, hit_color);
}

#[closest_hit]

#version 460
#extension GL_EXT_ray_tracing : require

// CRITICAL (C3): Use gl_InstanceCustomIndexEXT, NOT gl_InstanceID.
// gl_InstanceCustomIndexEXT is the 24-bit user index set per TLAS entry in
// tlas_instances_buffer_fill(). It indexes into instance_base_colors[]
// in the same fill order (instances x surfaces).
layout(location = 0) rayPayloadInEXT vec4 hit_color;
hitAttributeEXT vec2 bary_coords;

layout(set = 4, binding = 0, std430) readonly buffer InstanceBaseColors {
	vec4 colors[];
} instance_base_colors;

void main() {
	// Look up the base color for this TLAS entry using the stable custom index.
	uint instance_idx = gl_InstanceCustomIndexEXT;
	vec4 base_color = instance_base_colors.colors[instance_idx];

	// Simple distance-based falloff approximation for Phase B.
	// Phase C: replace with proper NdotL from hit normal via barycentrics.
	float ndotl = max(0.2, 1.0 - gl_HitTEXT / 1000.0);
	hit_color = vec4(base_color.rgb * ndotl, 1.0);
}

#[miss]

#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 hit_color;
layout(set = 0, binding = 1) uniform samplerCube sky_texture;

void main() {
	// No geometry hit -- sample sky/IBL.
	// alpha = 0.0 signals the raygen that this was a sky sample.
	vec3 sky_sample = texture(sky_texture, gl_WorldRayDirectionEXT).rgb;
	hit_color = vec4(sky_sample, 0.0);
}
