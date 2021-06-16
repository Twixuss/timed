/*!
blend src_color2 inv_src_color2
#blend src_alpha inv_src_alpha
depth_test off
!*/

#ifdef VERTEX_SHADER
#define V2F out
#else
#define V2F in
#endif

uniform sampler2D main_texture;
uniform vec2 position_offset;
uniform vec2 position_scale;
uniform vec4 color;

V2F vec2 vertex_uv;

#ifdef VERTEX_SHADER

layout(location = 0) in vec2 i_position;
layout(location = 1) in vec2 i_uv;

void main() {
	vec2 p = (i_position + position_offset) * position_scale - 1;
	p.y = -p.y;
	gl_Position = vec4(p, 0, 1);
	vertex_uv = i_uv;
}

#endif

#ifdef FRAGMENT_SHADER

layout(location = 0, index = 0) out vec4 fragment_text_color;
layout(location = 0, index = 1) out vec4 fragment_text_mask;

void main() {
	fragment_text_color = color;
	fragment_text_mask = color.a * texture(main_texture, vertex_uv);
}

#endif
