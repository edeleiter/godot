#[compute]

#version 450

#VERSION_DEFINES

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D static_depth;
layout(set = 0, binding = 1) uniform sampler2D dynamic_depth;
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D output_depth;

layout(push_constant, std430) uniform Params {
	ivec2 region_offset;
	ivec2 region_size;
}
params;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, params.region_size))) {
		return;
	}

	ivec2 texel = pos + params.region_offset;
	float d_static = texelFetch(static_depth, texel, 0).r;
	float d_dynamic = texelFetch(dynamic_depth, texel, 0).r;
	imageStore(output_depth, texel, vec4(min(d_static, d_dynamic)));
}
