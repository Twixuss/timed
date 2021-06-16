/*!
blend src_alpha inv_src_alpha
depth_test off
!*/

#ifdef VERTEX_SHADER
#define V2F out
#else
#define V2F in
#endif

uniform vec4 color;
uniform vec2 position_min;
uniform vec2 position_max;

V2F vec2 vertex_uv;

#ifdef VERTEX_SHADER

void main() {
	vec2 positions[] = vec2[](
		vec2(position_min.x, position_min.y),
		vec2(position_max.x, position_min.y),
		vec2(position_max.x, position_max.y),
		vec2(position_min.x, position_max.y)
	);
	vec2 position = positions[gl_VertexID];
	gl_Position = vec4(position, 0, 1);
	vertex_uv = position * 0.5 + 0.5;
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_color;

void main() {
	fragment_color = color;
}

#endif
