/* rt_ao_accumulate.glsl - TAA accumulation for RT Ambient Occlusion (R8 format).
 * Neighborhood clamping on R8 prevents temporal ghosting on moving geometry.
 * blend_factor = 0.125 (1/8 frames) for stable 8-frame accumulation.
 */

#[compute]

#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, r8) uniform readonly image2D current_frame;
layout(set = 0, binding = 1) uniform sampler2D history_buffer;
layout(set = 0, binding = 2, rg16f) uniform readonly image2D velocity_buffer;
layout(set = 0, binding = 3, r8) uniform writeonly image2D output_buffer;

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

	float current = imageLoad(current_frame, coord).r;

	// 3x3 neighborhood AABB for R8 ghost-free clamping.
	float nb_min = current;
	float nb_max = current;
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			ivec2 nc = clamp(coord + ivec2(dx, dy), ivec2(0), pc.screen_size - 1);
			float nb = imageLoad(current_frame, nc).r;
			nb_min = min(nb_min, nb);
			nb_max = max(nb_max, nb);
		}
	}

	vec2 velocity = imageLoad(velocity_buffer, coord).rg;
	vec2 history_uv = (vec2(coord) + 0.5) / vec2(pc.screen_size) - velocity;
	bool out_of_bounds = any(lessThan(history_uv, vec2(0.0))) || any(greaterThan(history_uv, vec2(1.0)));
	bool large_motion = dot(velocity, velocity) > pc.velocity_rejection_threshold;

	float blend = pc.temporal_blend;
	if (out_of_bounds || large_motion) {
		blend = 1.0;
	}

	float history = texture(history_buffer, history_uv).r;
	float history_clamped = clamp(history, nb_min, nb_max);
	float result = mix(history_clamped, current, blend);
	imageStore(output_buffer, coord, vec4(result, 0.0, 0.0, 0.0));
}
