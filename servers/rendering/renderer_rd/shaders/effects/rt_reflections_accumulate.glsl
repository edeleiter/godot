/* rt_reflections_accumulate.glsl - TAA accumulation for RT Reflections.
 * 3x3 neighborhood clamping prevents ghosting from dynamic objects.
 * blend_factor = temporal_blend (~0.1) for stable accumulation over ~8 frames.
 */

#[compute]

#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform readonly image2D current_frame;
layout(set = 0, binding = 1) uniform sampler2D history_buffer;
layout(set = 0, binding = 2, rg16f) uniform readonly image2D velocity_buffer;
layout(set = 0, binding = 3, rgba16f) uniform writeonly image2D output_buffer;

layout(push_constant, std430) uniform PushConstant {
	ivec2 screen_size;
	float temporal_blend;
	float velocity_rejection_threshold;
} pc;

void main() {
	ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(coord, pc.screen_size))) {
		return;
	}

	vec4 current = imageLoad(current_frame, coord);

	// Build 3x3 neighborhood AABB for ghost-free clamping.
	vec4 nb_min = current;
	vec4 nb_max = current;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			ivec2 nc = clamp(coord + ivec2(dx, dy), ivec2(0), pc.screen_size - 1);
			vec4 nb = imageLoad(current_frame, nc);
			nb_min = min(nb_min, nb);
			nb_max = max(nb_max, nb);
		}
	}

	// Reproject and sample history.
	vec2 velocity = imageLoad(velocity_buffer, coord).rg;
	vec2 history_uv = (vec2(coord) + 0.5) / vec2(pc.screen_size) - velocity;
	bool out_of_bounds = any(lessThan(history_uv, vec2(0.0))) || any(greaterThan(history_uv, vec2(1.0)));
	bool large_motion = dot(velocity, velocity) > pc.velocity_rejection_threshold;

	float blend = pc.temporal_blend;
	if (out_of_bounds || large_motion) {
		blend = 1.0;
	}

	vec4 history = texture(history_buffer, history_uv);
	// Clamp history to neighborhood AABB (ghost-free TAA).
	vec4 history_clamped = clamp(history, nb_min, nb_max);
	vec4 result = mix(history_clamped, current, blend);
	imageStore(output_buffer, coord, result);
}
