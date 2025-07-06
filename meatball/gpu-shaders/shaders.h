/**
 * Spaghetti Display Server
 * Copyright (C) 2025  SpaghettiFork
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

const char* clear_rectangle_fragment_shader = R"(
void
main (void)
{
	gl_FragColor = vec4 (0.0, 0.0, 0.0, 0.0);
}
)";

const char* clear_rectangle_vertex_shader = R"(
attribute vec2 pos;

void
main (void)
{
	gl_Position = vec4 (pos.x, pos.y, 1.0, 1.0);
}
)";

const char* composite_rectangle_vertex_shader = R"(
precision mediump float;
attribute vec2 pos;
attribute vec2 texcoord;
varying vec2 v_texcoord;
uniform mat3 source;

void
main (void)
{
	gl_Position = vec4 (pos.x, pos.y, 1.0, 1.0);
	v_texcoord = (source * vec3 (texcoord, 1.0)).xy;
}
)";

const char* composite_rectangle_fragment_shader_rgba = R"(
precision mediump float;
uniform sampler2D texture;
uniform bool invert_y;
varying vec2 v_texcoord;

void
main (void)
{
	vec2 texcoord;

	texcoord = v_texcoord;

	if (invert_y)
		texcoord = vec2 (texcoord.x, 1.0 - texcoord.y);

	gl_FragColor = texture2D (texture, texcoord);
}
)";

const char* composite_rectangle_fragment_shader_rgbx = R"(
precision mediump float;
uniform sampler2D texture;
uniform mat3 source;
uniform bool invert_y;
varying vec2 v_texcoord;

void
main (void)
{
	vec2 texcoord;

	texcoord = v_texcoord;

	if (invert_y)
		texcoord = vec2 (texcoord.x, 1.0 - texcoord.y);

	gl_FragColor = vec4 (texture2D (texture, texcoord).rgb, 1.0);
}
)";

const char* composite_rectangle_fragment_shader_external = R"(
#extension GL_OES_EGL_image_external : require

precision mediump float;
uniform samplerExternalOES texture;
uniform mat3 source;
uniform bool invert_y;
varying vec2 v_texcoord;

void
main (void)
{
	vec2 texcoord;

	texcoord = v_texcoord;

	if (invert_y)
		texcoord = vec2 (texcoord.x, 1.0 - texcoord.y);

	gl_FragColor = texture2D (texture, texcoord);
}
)";

const char* composite_rectangle_fragment_shader_single_pixel = R"(
#extension GL_OES_EGL_image_external : require

precision mediump float;
uniform vec4 source_color;
uniform mat3 source;
uniform bool invert_y;
varying vec2 v_texcoord;

void
main (void)
{
	vec2 texcoord;

	texcoord = v_texcoord;

	if (invert_y)
		texcoord = vec2 (texcoord.x, 1.0 - texcoord.y);

	if (texcoord.x < 0.0 || texcoord.y < 0.0
		|| texcoord.x > 1.0 || texcoord.y > 1.0)
		gl_FragColor = vec4 (0.0, 0.0, 0.0, 0.0);
	else
		gl_FragColor = source_color;
}
)";