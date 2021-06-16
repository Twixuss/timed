/*!
blend src_alpha inv_src_alpha
depth_test off
!*/

#ifdef VERTEX_SHADER
#define V2F out
#else
#define V2F in
#endif

V2F vec4 vertex_color;

#ifdef VERTEX_SHADER

layout(location = 0) in vec2 input_position;
layout(location = 1) in vec4 input_color;

void main() {
	gl_Position = vec4(input_position, 0, 1);
	vertex_color = input_color;
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_color;

void main() {
	fragment_color = vertex_color;
}

#endif
