/*
 * Copyright © 2009 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *    Junyan He <junyan.he@linux.intel.com>
 *
 */

/** @file glamor_render.c
 *
 * Render acceleration implementation
 */

#include "glamor_priv.h"

#include "mipict.h"
#include "fbpict.h"
#if 0
//#define DEBUGF(str, ...)  do {} while(0)
#define DEBUGF(str, ...) ErrorF(str, ##__VA_ARGS__)
//#define DEBUGRegionPrint(x) do {} while (0)
#define DEBUGRegionPrint RegionPrint
#endif

static struct blendinfo composite_op_info[] = {
    [PictOpClear] = {0, 0, GL_ZERO, GL_ZERO},
    [PictOpSrc] = {0, 0, GL_ONE, GL_ZERO},
    [PictOpDst] = {0, 0, GL_ZERO, GL_ONE},
    [PictOpOver] = {0, 1, GL_ONE, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpOverReverse] = {1, 0, GL_ONE_MINUS_DST_ALPHA, GL_ONE},
    [PictOpIn] = {1, 0, GL_DST_ALPHA, GL_ZERO},
    [PictOpInReverse] = {0, 1, GL_ZERO, GL_SRC_ALPHA},
    [PictOpOut] = {1, 0, GL_ONE_MINUS_DST_ALPHA, GL_ZERO},
    [PictOpOutReverse] = {0, 1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpAtop] = {1, 1, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpAtopReverse] = {1, 1, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA},
    [PictOpXor] = {1, 1, GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpAdd] = {0, 0, GL_ONE, GL_ONE},
};

#define RepeatFix			            10
#define GLAMOR_PI                       3.14159265358979323846
#define GLAMOR_GRADIENT_LUT_SOURCE_UNIT 2
#define GLAMOR_GRADIENT_LUT_MASK_UNIT   3

static inline void
glamor_set_gradient_coords(float *vertices, int stride,
                           int x1, int y1, int x2, int y2)
{
    vertices[0] = (float) x1;
    vertices[1 * stride] = (float) x2;
    vertices[2 * stride] = (float) x2;
    vertices[3 * stride] = (float) x1;
    vertices[1] = (float) y1;
    vertices[1 * stride + 1] = (float) y1;
    vertices[2 * stride + 1] = (float) y2;
    vertices[3 * stride + 1] = (float) y2;
}

static char *
glamor_create_gradient_fetch(const char *varying, const char *prefix,
                             const char *getter)
{
    char *fetch;

    XNFasprintf(&fetch,
                "uniform sampler2D %s_gradient_lut;\n"
                "uniform int %s_gradient_type;\n"
                "uniform int %s_gradient_repeat;\n"
                "uniform mat3 %s_gradient_transform;\n"
                "uniform vec2 %s_gradient_p1;\n"
                "uniform vec2 %s_gradient_p2;\n"
                "uniform vec2 %s_gradient_c1;\n"
                "uniform float %s_gradient_r1;\n"
                "uniform vec2 %s_gradient_c2;\n"
                "uniform float %s_gradient_r2;\n"
                "uniform float %s_gradient_a;\n"
                "uniform vec2 %s_gradient_center;\n"
                "uniform float %s_gradient_angle;\n"
                "in vec2 %s;\n"
                "vec4 %s()\n"
                "{\n"
                "    vec3 tmp = vec3(%s, 1.0);\n"
                "    vec3 pos = %s_gradient_transform * tmp;\n"
                "    float t = 0.0;\n"
                "    pos.xy = pos.xy / pos.z;\n"
                "    if (%s_gradient_type == %d) {\n"
                "        vec2 axis = %s_gradient_p2 - %s_gradient_p1;\n"
                "        float denom = dot(axis, axis);\n"
                "        if (denom < 0.000001)\n"
                "            t = 0.0;\n"
                "        else\n"
                "            t = dot(pos.xy - %s_gradient_p1, axis) / denom;\n"
                "    } else if (%s_gradient_type == %d) {\n"
                "        float b_val = (pos.x - %s_gradient_c1.x) * (%s_gradient_c2.x - %s_gradient_c1.x)\n"
                "                    + (pos.y - %s_gradient_c1.y) * (%s_gradient_c2.y - %s_gradient_c1.y)\n"
                "                    + %s_gradient_r1 * (%s_gradient_r2 - %s_gradient_r1);\n"
                "        float c_val = (pos.x - %s_gradient_c1.x) * (pos.x - %s_gradient_c1.x)\n"
                "                    + (pos.y - %s_gradient_c1.y) * (pos.y - %s_gradient_c1.y)\n"
                "                    - %s_gradient_r1 * %s_gradient_r1;\n"
                "        float sqrt_val = 0.0;\n"
                "        if (abs(%s_gradient_a) < 0.00001) {\n"
                "            if (b_val == 0.0)\n"
                "                return vec4(0.0, 0.0, 0.0, 0.0);\n"
                "            t = 0.5 * c_val / b_val;\n"
                "        } else {\n"
                "            sqrt_val = b_val * b_val - %s_gradient_a * c_val;\n"
                "            if (sqrt_val < 0.0)\n"
                "                return vec4(0.0, 0.0, 0.0, 0.0);\n"
                "            sqrt_val = sqrt(sqrt_val);\n"
                "            t = (b_val + sqrt_val) / %s_gradient_a;\n"
                "        }\n"
                "        if (%s_gradient_repeat == %d) {\n"
                "            if ((t <= 0.0) || (t > 1.0))\n"
                "                t = (b_val - sqrt_val) / %s_gradient_a;\n"
                "            if ((t <= 0.0) || (t > 1.0))\n"
                "                return vec4(0.0, 0.0, 0.0, 0.0);\n"
                "        } else {\n"
                "            if (t * (%s_gradient_r2 - %s_gradient_r1) <= -1.0 * %s_gradient_r1)\n"
                "                t = (b_val - sqrt_val) / %s_gradient_a;\n"
                "            if (t * (%s_gradient_r2 - %s_gradient_r1) <= -1.0 * %s_gradient_r1)\n"
                "                return vec4(0.0, 0.0, 0.0, 0.0);\n"
                "        }\n"
                "    } else {\n"
                "        vec2 grad_d = pos.xy - %s_gradient_center;\n"
                "        float grad_a = atan(grad_d.y, grad_d.x) + %s_gradient_angle;\n"
                "        grad_a = grad_a - %f * floor(grad_a / %f);\n"
                "        t = 1.0 - grad_a / %f;\n"
                "    }\n"
                "    if (%s_gradient_repeat == %d) {\n"
                "        if ((t < 0.0) || (t > 1.0))\n"
                "            return vec4(0.0, 0.0, 0.0, 0.0);\n"
                "    } else if (%s_gradient_repeat == %d) {\n"
                "        t = fract(t);\n"
                "    } else if (%s_gradient_repeat == %d) {\n"
                "        t = abs(fract(t * 0.5 + 0.5) * 2.0 - 1.0);\n"
                "    } else {\n"
                "        t = clamp(t, 0.0, 1.0);\n"
                "    }\n"
                "    return texture(%s_gradient_lut, vec2(t, 0.5));\n"
                "}\n",
                prefix, prefix, prefix, prefix, prefix, prefix,
                prefix, prefix, prefix, prefix, prefix, prefix,
                prefix, varying, getter, varying, prefix,
                prefix, SourcePictTypeLinear, prefix, prefix,
                prefix, prefix, SourcePictTypeRadial,
                prefix, prefix, prefix, prefix, prefix, prefix,
                prefix, prefix, prefix, prefix, prefix, prefix,
                prefix, prefix, prefix, prefix, prefix, prefix,
                prefix, RepeatNone, prefix, prefix,
                prefix, prefix, prefix, prefix, prefix, prefix,
                prefix, prefix,
                2.0 * GLAMOR_PI, 2.0 * GLAMOR_PI, 2.0 * GLAMOR_PI,
                prefix, RepeatNone, prefix, RepeatNormal,
                prefix, RepeatReflect, prefix);

    return fetch;
}

static GLuint
glamor_create_composite_fs(glamor_screen_private *glamor_priv, struct shader_key *key, Bool enable_rel_sampler)
{
    const char *repeat_define =
        "#define RepeatNone               	      0\n"
        "#define RepeatNormal                     1\n"
        "#define RepeatPad                        2\n"
        "#define RepeatReflect                    3\n"
        "#define RepeatFix		      	      10\n"
        "uniform int 			source_repeat_mode;\n"
        "uniform int 			mask_repeat_mode;\n";
    const char *relocate_texture =
        "vec2 rel_tex_coord(vec2 texture, vec4 wh, int repeat) \n"
        "{\n"
        "	vec2 rel_tex; \n"
        "	rel_tex = texture * wh.xy; \n"
        "	if (repeat == RepeatFix + RepeatNone)\n"
        "		return rel_tex; \n"
        "	else if (repeat == RepeatFix + RepeatNormal) \n"
        "		rel_tex = floor(rel_tex) + (fract(rel_tex) / wh.xy); \n"
        "	else if (repeat == RepeatFix + RepeatPad) { \n"
        "		if (rel_tex.x >= 1.0) \n"
        "			rel_tex.x = 1.0 - wh.z * wh.x / 2.; \n"
        "		else if (rel_tex.x < 0.0) \n"
        "			rel_tex.x = 0.0; \n"
        "		if (rel_tex.y >= 1.0) \n"
        "			rel_tex.y = 1.0 - wh.w * wh.y / 2.; \n"
        "		else if (rel_tex.y < 0.0) \n"
        "			rel_tex.y = 0.0; \n"
        "		rel_tex = rel_tex / wh.xy; \n"
        "	} else if (repeat == RepeatFix + RepeatReflect) {\n"
        "		if ((1.0 - mod(abs(floor(rel_tex.x)), 2.0)) < 0.001)\n"
        "			rel_tex.x = 2.0 - (1.0 - fract(rel_tex.x)) / wh.x;\n"
        "		else \n"
        "			rel_tex.x = fract(rel_tex.x) / wh.x;\n"
        "		if ((1.0 - mod(abs(floor(rel_tex.y)), 2.0)) < 0.001)\n"
        "			rel_tex.y = 2.0 - (1.0 - fract(rel_tex.y)) / wh.y;\n"
        "		else \n"
        "			rel_tex.y = fract(rel_tex.y) / wh.y;\n"
        "	} \n"
        "	return rel_tex; \n"
        "}\n";
    /* The texture and the pixmap size is not match exactly, so can't sample it directly.
     * rel_sampler will recalculate the texture coords.*/
#define REL_SAMPLER_BODY \
    "	if (repeat >= RepeatFix) {\n" \
    "		tex = rel_tex_coord(tex, wh, repeat);\n" \
    "		if (repeat == RepeatFix + RepeatNone) {\n" \
    "			if (tex.x < 0.0 || tex.x >= 1.0 || \n" \
    "			    tex.y < 0.0 || tex.y >= 1.0)\n" \
    "				return vec4(0.0, 0.0, 0.0, 0.0);\n" \
    "			tex = (fract(tex) / wh.xy);\n" \
    "		}\n" \
    "	}\n"
    const char *rel_sampler =
        " vec4 rel_sampler_rgba(sampler2D tex_image, vec2 tex, vec4 wh, int repeat)\n"
        "{\n"
        REL_SAMPLER_BODY
        "	return texture(tex_image, tex);\n"
        "}\n"
        " vec4 rel_sampler_rgbx(sampler2D tex_image, vec2 tex, vec4 wh, int repeat)\n"
        "{\n"
        REL_SAMPLER_BODY
        "	return vec4(texture(tex_image, tex).rgb, 1.0);\n"
        "}\n";
#undef REL_SAMPLER_BODY
    const char *stub_rel_sampler =
        " vec4 rel_sampler_rgba(sampler2D tex_image, vec2 tex, vec4 wh, int repeat)\n"
        "{\n"
        "	return texture(tex_image, tex);\n"
        "}\n"
        " vec4 rel_sampler_rgbx(sampler2D tex_image, vec2 tex, vec4 wh, int repeat)\n"
        "{\n"
        "	return vec4(texture(tex_image, tex).rgb, 1.0);\n"
        "}\n";

    const char *source_solid_fetch =
        "uniform vec4 source;\n"
        "vec4 get_source()\n"
        "{\n"
        "	return source;\n"
        "}\n";
    const char *source_alpha_pixmap_fetch =
        "in vec2 source_texture;\n"
        "uniform sampler2D source_sampler;\n"
        "uniform vec4 source_wh;"
        "vec4 get_source()\n"
        "{\n"
        "	return rel_sampler_rgba(source_sampler, source_texture,\n"
        "			        source_wh, source_repeat_mode);\n"
        "}\n";
    const char *source_pixmap_fetch =
        "in vec2 source_texture;\n"
        "uniform sampler2D source_sampler;\n"
        "uniform vec4 source_wh;\n"
        "vec4 get_source()\n"
        "{\n"
        "	return rel_sampler_rgbx(source_sampler, source_texture,\n"
        "				source_wh, source_repeat_mode);\n"
        "}\n";
    const char *mask_none =
        "vec4 get_mask()\n"
        "{\n"
        "	return vec4(0.0, 0.0, 0.0, 1.0);\n"
        "}\n";
    const char *mask_solid_fetch =
        "uniform vec4 mask;\n"
        "vec4 get_mask()\n"
        "{\n"
        "	return mask;\n"
        "}\n";
    const char *mask_alpha_pixmap_fetch =
        "in vec2 mask_texture;\n"
        "uniform sampler2D mask_sampler;\n"
        "uniform vec4 mask_wh;\n"
        "vec4 get_mask()\n"
        "{\n"
        "	return rel_sampler_rgba(mask_sampler, mask_texture,\n"
        "			        mask_wh, mask_repeat_mode);\n"
        "}\n";
    const char *mask_pixmap_fetch =
        "in vec2 mask_texture;\n"
        "uniform sampler2D mask_sampler;\n"
        "uniform vec4 mask_wh;\n"
        "vec4 get_mask()\n"
        "{\n"
        "	return rel_sampler_rgbx(mask_sampler, mask_texture,\n"
        "				mask_wh, mask_repeat_mode);\n"
        "}\n";

    const char *dest_swizzle_default =
        "vec4 dest_swizzle(vec4 color)\n"
        "{"
        "	return color;"
        "}";
    const char *dest_swizzle_alpha_to_red =
        "vec4 dest_swizzle(vec4 color)\n"
        "{"
        "	return vec4(color.a, 0.0, 0.0, 0.0);"
        "}";
    const char *dest_swizzle_ignore_alpha =
        "vec4 dest_swizzle(vec4 color)\n"
        "{"
        "	return vec4(color.xyz, 1.0);"
        "}";

    const char *in_normal =
        "void main()\n"
        "{\n"
        "	frag_color = dest_swizzle(get_source() * get_mask().a);\n"
        "}\n";
    const char *in_ca_source =
        "void main()\n"
        "{\n"
        "	frag_color = dest_swizzle(get_source() * get_mask());\n"
        "}\n";
    const char *in_ca_alpha =
        "void main()\n"
        "{\n"
        "	frag_color = dest_swizzle(get_source().a * get_mask());\n"
        "}\n";
    const char *in_ca_dual_blend =
        "out vec4 color0;\n"
        "out vec4 color1;\n"
        "void main()\n"
        "{\n"
        "	color0 = dest_swizzle(get_source() * get_mask());\n"
        "	color1 = dest_swizzle(get_source().a * get_mask());\n"
        "}\n";
    const char *in_ca_dual_blend_gles2 =
        "void main()\n"
        "{\n"
        "	gl_FragColor = dest_swizzle(get_source() * get_mask());\n"
        "	gl_SecondaryFragColorEXT = dest_swizzle(get_source().a * get_mask());\n"
        "}\n";
    const char *header_ca_dual_blend_gles2 =
        "#version 100\n"
        "#extension GL_EXT_blend_func_extended : require\n"
        GLAMOR_COMPAT_DEFINES_FS;

    char *source;
    const char *source_fetch;
    const char *mask_fetch = "";
    char *source_fetch_alloc = NULL;
    char *mask_fetch_alloc = NULL;
    const char *in;
    const char *header;
    const char *header_norm = glamor_priv->glsl_version > 120 ?
        "#version 130\n" :
        glamor_priv->use_gpu_shader4 ?
          "#version 120\n#extension GL_EXT_gpu_shader4 : require\n" GLAMOR_COMPAT_DEFINES_FS :
          "#version 120\n" GLAMOR_COMPAT_DEFINES_FS;
    const char *header_es = glamor_priv->glsl_version > 100 ? "#version 300 es\n" : "#version 100\n" GLAMOR_COMPAT_DEFINES_FS;
    const char *dest_swizzle;
    GLuint prog;

    switch (key->source) {
    case SHADER_SOURCE_SOLID:
        source_fetch = source_solid_fetch;
        break;
    case SHADER_SOURCE_TEXTURE_ALPHA:
        source_fetch = source_alpha_pixmap_fetch;
        break;
    case SHADER_SOURCE_TEXTURE:
        source_fetch = source_pixmap_fetch;
        break;
    case SHADER_SOURCE_GRADIENT:
        source_fetch_alloc =
            glamor_create_gradient_fetch("source_texture", "source",
                                         "get_source");
        source_fetch = source_fetch_alloc;
        break;
    default:
        FatalError("Bad composite shader source");
    }

    switch (key->mask) {
    case SHADER_MASK_NONE:
        mask_fetch = mask_none;
        break;
    case SHADER_MASK_SOLID:
        mask_fetch = mask_solid_fetch;
        break;
    case SHADER_MASK_TEXTURE_ALPHA:
        mask_fetch = mask_alpha_pixmap_fetch;
        break;
    case SHADER_MASK_TEXTURE:
        mask_fetch = mask_pixmap_fetch;
        break;
    case SHADER_MASK_GRADIENT:
        mask_fetch_alloc =
            glamor_create_gradient_fetch("mask_texture", "mask",
                                         "get_mask");
        mask_fetch = mask_fetch_alloc;
        break;
    default:
        FatalError("Bad composite shader mask");
    }

    /* If we're storing to an a8 texture but our texture format is
     * GL_RED because of a core context, then we need to make sure to
     * put the alpha into the red channel.
     */
    switch (key->dest_swizzle) {
    case SHADER_DEST_SWIZZLE_DEFAULT:
        dest_swizzle = dest_swizzle_default;
        break;
    case SHADER_DEST_SWIZZLE_ALPHA_TO_RED:
        dest_swizzle = dest_swizzle_alpha_to_red;
        break;
    case SHADER_DEST_SWIZZLE_IGNORE_ALPHA:
        dest_swizzle = dest_swizzle_ignore_alpha;
        break;
    default:
        FatalError("Bad composite shader dest swizzle");
    }

    header = glamor_priv->is_gles ? header_es : header_norm;
    switch (key->in) {
    case glamor_program_alpha_normal:
        in = in_normal;
        break;
    case glamor_program_alpha_ca_first:
        in = in_ca_source;
        break;
    case glamor_program_alpha_ca_second:
        in = in_ca_alpha;
        break;
    case glamor_program_alpha_dual_blend:
        in = in_ca_dual_blend;
        break;
    case glamor_program_alpha_dual_blend_gles2:
        in = in_ca_dual_blend_gles2;
        header = header_ca_dual_blend_gles2;
        break;
    default:
        FatalError("Bad composite IN type");
    }

    XNFasprintf(&source,
                "%s"
                GLAMOR_DEFAULT_PRECISION
                "%s%s%s%s%s%s%s%s", header, GLAMOR_COMPAT_DEFINES_FS,
                repeat_define, relocate_texture,
                enable_rel_sampler ? rel_sampler : stub_rel_sampler,
                source_fetch, mask_fetch, dest_swizzle, in);

    prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, source);
    free(source);
    free(source_fetch_alloc);
    free(mask_fetch_alloc);

    return prog;
}

static GLuint
glamor_create_composite_vs(glamor_screen_private* priv, struct shader_key *key)
{
    const char *main_opening =
        "in vec4 v_position;\n"
        "in vec4 v_texcoord0;\n"
        "in vec4 v_texcoord1;\n"
        "out vec2 source_texture;\n"
        "out vec2 mask_texture;\n"
        "void main()\n"
        "{\n"
        "	gl_Position = v_position;\n";
    const char *source_coords = "	source_texture = v_texcoord0.xy;\n";
    const char *mask_coords = "	mask_texture = v_texcoord1.xy;\n";
    const char *main_closing = "}\n";
    const char *source_coords_setup = "";
    const char *mask_coords_setup = "";
    const char *version_gles2 = "#version 100\n";
    const char *version_gles3 = "#version 300 es\n";
    const char *version = priv->glsl_version > 120 ? "#version 130\n" : "#version 120\n";
    const char *defines = priv->glsl_version > 120 ? "": GLAMOR_COMPAT_DEFINES_VS;
    char *source;
    GLuint prog;

    if (key->source != SHADER_SOURCE_SOLID)
        source_coords_setup = source_coords;

    if (key->mask != SHADER_MASK_NONE && key->mask != SHADER_MASK_SOLID)
        mask_coords_setup = mask_coords;

    if (priv->is_gles)
        version = version_gles2;

    if (priv->is_gles && priv->glsl_version > 120)
        version = version_gles3;

    XNFasprintf(&source,
                "%s"
                GLAMOR_DEFAULT_PRECISION
                "%s%s%s%s%s",
                version, defines, main_opening, source_coords_setup,
                mask_coords_setup, main_closing);

    prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, source);
    free(source);

    return prog;
}

static void
glamor_init_gradient_uniform_locations(GLuint prog, const char *prefix,
                                       glamor_composite_shader_priv *priv,
                                       int unit)
{
    char name[64];

    snprintf(name, sizeof(name), "%s_gradient_lut", prefix);
    priv->gradient.lut = glGetUniformLocation(prog, name);
    glUniform1i(priv->gradient.lut, unit);
    snprintf(name, sizeof(name), "%s_gradient_type", prefix);
    priv->gradient.type = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_repeat", prefix);
    priv->gradient.repeat = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_transform", prefix);
    priv->gradient.transform = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_p1", prefix);
    priv->gradient.p1 = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_p2", prefix);
    priv->gradient.p2 = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_c1", prefix);
    priv->gradient.c1 = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_r1", prefix);
    priv->gradient.r1 = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_c2", prefix);
    priv->gradient.c2 = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_r2", prefix);
    priv->gradient.r2 = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_a", prefix);
    priv->gradient.radial_a = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_center", prefix);
    priv->gradient.center = glGetUniformLocation(prog, name);
    snprintf(name, sizeof(name), "%s_gradient_angle", prefix);
    priv->gradient.angle = glGetUniformLocation(prog, name);
}

static Bool
glamor_set_composite_gradient(glamor_screen_private *glamor_priv,
                              ScreenPtr screen, PicturePtr picture, int unit,
                              glamor_composite_shader_priv *shader)
{
    SourcePictPtr sp = picture->pSourcePict;
    GLuint lut;
    float transform[3][3];
    float v0[2], v1[2];
    float f0, f1, fa;

    lut = glamor_gradient_get_lut(screen, &sp->gradient);
    if (lut == 0)
        return FALSE;

    glamor_make_current(glamor_priv);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, lut);
    glUniform1i(shader->gradient.lut, unit);
    glUniform1i(shader->gradient.type, sp->type);
    glUniform1i(shader->gradient.repeat, picture->repeatType);

    glamor_gradient_get_transform(picture, transform);
    glUniformMatrix3fv(shader->gradient.transform, 1, GL_FALSE,
                       &transform[0][0]);

    switch (sp->type) {
    case SourcePictTypeLinear:
        v0[0] = (float) pixman_fixed_to_double(sp->linear.p1.x);
        v0[1] = (float) pixman_fixed_to_double(sp->linear.p1.y);
        v1[0] = (float) pixman_fixed_to_double(sp->linear.p2.x);
        v1[1] = (float) pixman_fixed_to_double(sp->linear.p2.y);
        glUniform2fv(shader->gradient.p1, 1, v0);
        glUniform2fv(shader->gradient.p2, 1, v1);
        break;
    case SourcePictTypeRadial:
        v0[0] = (float) pixman_fixed_to_double(sp->radial.c1.x);
        v0[1] = (float) pixman_fixed_to_double(sp->radial.c1.y);
        f0 = (float) pixman_fixed_to_double(sp->radial.c1.radius);
        v1[0] = (float) pixman_fixed_to_double(sp->radial.c2.x);
        v1[1] = (float) pixman_fixed_to_double(sp->radial.c2.y);
        f1 = (float) pixman_fixed_to_double(sp->radial.c2.radius);
        fa = (v1[0] - v0[0]) * (v1[0] - v0[0]) +
            (v1[1] - v0[1]) * (v1[1] - v0[1]) - (f1 - f0) * (f1 - f0);
        glUniform2fv(shader->gradient.c1, 1, v0);
        glUniform1f(shader->gradient.r1, f0);
        glUniform2fv(shader->gradient.c2, 1, v1);
        glUniform1f(shader->gradient.r2, f1);
        glUniform1f(shader->gradient.radial_a, fa);
        break;
    case SourcePictTypeConical:
        v0[0] = (float) pixman_fixed_to_double(sp->conical.center.x);
        v0[1] = (float) pixman_fixed_to_double(sp->conical.center.y);
        f0 = (float) (pixman_fixed_to_double(sp->conical.angle) *
                      GLAMOR_PI / 180.0);
        glUniform2fv(shader->gradient.center, 1, v0);
        glUniform1f(shader->gradient.angle, f0);
        break;
    default:
        break;
    }

    glActiveTexture(GL_TEXTURE0);
    return TRUE;
}

static void
glamor_create_composite_shader(ScreenPtr screen, struct shader_key *key,
                               glamor_composite_shader *shader)
{
    GLuint vs, fs, prog;
    GLint source_sampler_uniform_location, mask_sampler_uniform_location;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    Bool enable_rel_sampler = TRUE;

    glamor_make_current(glamor_priv);
    vs = glamor_create_composite_vs(glamor_priv, key);
    if (vs == 0)
        return;
    fs = glamor_create_composite_fs(glamor_priv, key, enable_rel_sampler);
    if (fs == 0)
        return;

    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glBindAttribLocation(prog, GLAMOR_VERTEX_POS, "v_position");
    glBindAttribLocation(prog, GLAMOR_VERTEX_SOURCE, "v_texcoord0");
    glBindAttribLocation(prog, GLAMOR_VERTEX_MASK, "v_texcoord1");

    if (key->in == glamor_program_alpha_dual_blend) {
        glBindFragDataLocationIndexed(prog, 0, 0, "color0");
        glBindFragDataLocationIndexed(prog, 0, 1, "color1");
    }

    if (!glamor_link_glsl_prog(screen, prog, "composite")) {
        /* Failed to link the shader, try again without rel_sampler. */
        enable_rel_sampler = FALSE;
        glDetachShader(prog, fs);
        fs = glamor_create_composite_fs(glamor_priv, key, enable_rel_sampler);
        if (fs == 0)
            return;
        glAttachShader(prog, fs);
        glDeleteShader(fs);

        if (!glamor_link_glsl_prog(screen, prog, "composite")) {
            glDeleteProgram(prog);
            return;
        }
    }

    shader->prog = prog;

    glUseProgram(prog);

    if (key->source == SHADER_SOURCE_SOLID) {
        shader->source.uniform_location = glGetUniformLocation(prog, "source");
    }
    else if (key->source == SHADER_SOURCE_GRADIENT) {
        glamor_init_gradient_uniform_locations(prog, "source",
                                               &shader->source,
                                               GLAMOR_GRADIENT_LUT_SOURCE_UNIT);
    }
    else {
        source_sampler_uniform_location =
            glGetUniformLocation(prog, "source_sampler");
        glUniform1i(source_sampler_uniform_location, 0);
        shader->source.wh = glGetUniformLocation(prog, "source_wh");
        shader->source.repeat_mode =
            glGetUniformLocation(prog, "source_repeat_mode");
    }

    if (key->mask != SHADER_MASK_NONE) {
        if (key->mask == SHADER_MASK_SOLID) {
            shader->mask.uniform_location = glGetUniformLocation(prog, "mask");
        }
        else if (key->mask == SHADER_MASK_GRADIENT) {
            glamor_init_gradient_uniform_locations(prog, "mask",
                                                   &shader->mask,
                                                   GLAMOR_GRADIENT_LUT_MASK_UNIT);
        }
        else {
            mask_sampler_uniform_location =
                glGetUniformLocation(prog, "mask_sampler");
            glUniform1i(mask_sampler_uniform_location, 1);
            shader->mask.wh = glGetUniformLocation(prog, "mask_wh");
            shader->mask.repeat_mode =
                glGetUniformLocation(prog, "mask_repeat_mode");
        }
    }
}

static glamor_composite_shader *
glamor_lookup_composite_shader(ScreenPtr screen, struct
                               shader_key
                               *key)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_composite_shader *shader;

    shader = &glamor_priv->composite_shader[key->source][key->mask][key->in][key->dest_swizzle];
    if (shader->prog == 0)
        glamor_create_composite_shader(screen, key, shader);

    return shader;
}

static GLenum
glamor_translate_blend_alpha_to_red(GLenum blend)
{
    switch (blend) {
    case GL_SRC_ALPHA:
        return GL_SRC_COLOR;
    case GL_DST_ALPHA:
        return GL_DST_COLOR;
    case GL_ONE_MINUS_SRC_ALPHA:
        return GL_ONE_MINUS_SRC_COLOR;
    case GL_ONE_MINUS_DST_ALPHA:
        return GL_ONE_MINUS_DST_COLOR;
    default:
        return blend;
    }
}

static Bool
glamor_set_composite_op(ScreenPtr screen,
                        CARD8 op, struct blendinfo *op_info_result,
                        PicturePtr dest, PicturePtr mask,
                        enum ca_state ca_state,
                        struct shader_key *key)
{
    GLenum source_blend, dest_blend;
    struct blendinfo *op_info;

    if (op >= ARRAY_SIZE(composite_op_info)) {
        glamor_fallback("unsupported render op %d \n", op);
        return GL_FALSE;
    }

    op_info = &composite_op_info[op];

    source_blend = op_info->source_blend;
    dest_blend = op_info->dest_blend;

    /* If there's no dst alpha channel, adjust the blend op so that we'll treat
     * it as always 1.
     */
    if (PICT_FORMAT_A(dest->format) == 0 && op_info->dest_alpha) {
        if (source_blend == GL_DST_ALPHA)
            source_blend = GL_ONE;
        else if (source_blend == GL_ONE_MINUS_DST_ALPHA)
            source_blend = GL_ZERO;
    }

    /* Set up the source alpha value for blending in component alpha mode. */
    if (ca_state == CA_DUAL_BLEND) {
        switch (dest_blend) {
        case GL_SRC_ALPHA:
            dest_blend = GL_SRC1_COLOR;
            break;
        case GL_ONE_MINUS_SRC_ALPHA:
            dest_blend = GL_ONE_MINUS_SRC1_COLOR;
            break;
        }
    } else if (mask && mask->componentAlpha
               && PICT_FORMAT_RGB(mask->format) != 0 && op_info->source_alpha) {
        switch (dest_blend) {
        case GL_SRC_ALPHA:
            dest_blend = GL_SRC_COLOR;
            break;
        case GL_ONE_MINUS_SRC_ALPHA:
            dest_blend = GL_ONE_MINUS_SRC_COLOR;
            break;
        }
    }

    /* If we're outputting our alpha to the red channel, then any
     * reads of alpha for blending need to come from the red channel.
     */
    if (key->dest_swizzle == SHADER_DEST_SWIZZLE_ALPHA_TO_RED) {
        source_blend = glamor_translate_blend_alpha_to_red(source_blend);
        dest_blend = glamor_translate_blend_alpha_to_red(dest_blend);
    }

    op_info_result->source_blend = source_blend;
    op_info_result->dest_blend = dest_blend;
    op_info_result->source_alpha = op_info->source_alpha;
    op_info_result->dest_alpha = op_info->dest_alpha;

    return TRUE;
}

static void
glamor_set_composite_texture(glamor_screen_private *glamor_priv, int unit,
                             PicturePtr picture,
                             PixmapPtr pixmap,
                             GLuint wh_location, GLuint repeat_location,
                             glamor_pixmap_private *dest_priv)
{
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    glamor_pixmap_fbo *fbo = pixmap_priv->fbo;
    float wh[4];
    int repeat_type;

    glamor_make_current(glamor_priv);

    /* The red channel swizzling doesn't depend on whether we're using
     * 'fbo' as source or mask as we must have the same answer in case
     * the same fbo is being used for both. That means the mask
     * channel will sometimes get red bits in the R channel, and
     * sometimes get zero bits in the R channel, which is harmless.
     */
    glamor_bind_texture(glamor_priv, GL_TEXTURE0 + unit, fbo,
                        dest_priv->fbo->is_red);
    repeat_type = picture->repeatType;
    switch (picture->repeatType) {
    case RepeatNone:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        break;
    case RepeatNormal:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        break;
    case RepeatPad:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        break;
    case RepeatReflect:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
        break;
    }

    switch (picture->filter) {
    default:
    case PictFilterFast:
    case PictFilterNearest:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        break;
    case PictFilterGood:
    case PictFilterBest:
    case PictFilterBilinear:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        break;
    }

    /* Handle RepeatNone in the shader when the source is missing the
     * alpha channel, as GL will return an alpha for 1 if the texture
     * is RGB (no alpha), which we use for 16bpp textures.
     */
    if (glamor_pixmap_priv_is_large(pixmap_priv) ||
        (!PICT_FORMAT_A(picture->format) &&
         repeat_type == RepeatNone && picture->transform)) {
        glamor_pixmap_fbo_fix_wh_ratio(wh, pixmap, pixmap_priv);
        glUniform4fv(wh_location, 1, wh);

        repeat_type += RepeatFix;
    }

    glUniform1i(repeat_location, repeat_type);
}

static void
glamor_set_composite_solid(float *color, GLint uniform_location)
{
    glUniform4fv(uniform_location, 1, color);
}

static char
glamor_get_picture_location(PicturePtr picture)
{
    if (picture == NULL)
        return ' ';

    if (picture->pDrawable == NULL) {
        switch (picture->pSourcePict->type) {
        case SourcePictTypeSolidFill:
            return 'c';
        case SourcePictTypeLinear:
            return 'l';
        case SourcePictTypeRadial:
            return 'r';
        case SourcePictTypeConical:
            return 'C';
        default:
            return '?';
        }
    }
    return glamor_get_drawable_location(picture->pDrawable);
}

static void *
glamor_setup_composite_vbo(ScreenPtr screen, int n_verts)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int vert_size;
    char *vbo_offset;
    float *vb;

    glamor_priv->render_nr_quads = 0;
    glamor_priv->vb_stride = 2 * sizeof(float);
    if (glamor_priv->has_source_coords)
        glamor_priv->vb_stride += 2 * sizeof(float);
    if (glamor_priv->has_mask_coords)
        glamor_priv->vb_stride += 2 * sizeof(float);

    vert_size = n_verts * glamor_priv->vb_stride;

    glamor_make_current(glamor_priv);

    vb = glamor_get_vbo_space(screen, vert_size, &vbo_offset);
    if (_X_UNLIKELY(vb == 0))
        return NULL;

    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT, GL_FALSE,
                          glamor_priv->vb_stride, vbo_offset);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);

    if (glamor_priv->has_source_coords) {
        glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2,
                              GL_FLOAT, GL_FALSE,
                              glamor_priv->vb_stride,
                              vbo_offset + 2 * sizeof(float));
        glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    }

    if (glamor_priv->has_mask_coords) {
        glVertexAttribPointer(GLAMOR_VERTEX_MASK, 2, GL_FLOAT, GL_FALSE,
                              glamor_priv->vb_stride,
                              vbo_offset + (glamor_priv->has_source_coords ?
                                            4 : 2) * sizeof(float));
        glEnableVertexAttribArray(GLAMOR_VERTEX_MASK);
    }

    return vb;
}

static void
glamor_flush_composite_rects(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_make_current(glamor_priv);

    if (!glamor_priv->render_nr_quads)
        return;

    glamor_glDrawArrays_GL_QUADS(glamor_priv, glamor_priv->render_nr_quads);
}

static const int pict_format_combine_tab[][3] = {
    {PICT_TYPE_ARGB, PICT_TYPE_A, PICT_TYPE_ARGB},
    {PICT_TYPE_ABGR, PICT_TYPE_A, PICT_TYPE_ABGR},
};

static Bool
combine_pict_format(PictFormatShort * des, const PictFormatShort src,
                    const PictFormatShort mask, glamor_program_alpha in_ca)
{
    PictFormatShort new_vis;
    int src_type, mask_type, src_bpp;
    int i;

    if (src == mask) {
        *des = src;
        return TRUE;
    }
    src_bpp = PICT_FORMAT_BPP(src);

    assert(src_bpp == PICT_FORMAT_BPP(mask));

    new_vis = PICT_FORMAT_VIS(src) | PICT_FORMAT_VIS(mask);

    switch (in_ca) {
    case glamor_program_alpha_normal:
        src_type = PICT_FORMAT_TYPE(src);
        mask_type = PICT_TYPE_A;
        break;
    case glamor_program_alpha_ca_first:
        src_type = PICT_FORMAT_TYPE(src);
        mask_type = PICT_FORMAT_TYPE(mask);
        break;
    case glamor_program_alpha_ca_second:
        src_type = PICT_TYPE_A;
        mask_type = PICT_FORMAT_TYPE(mask);
        break;
    case glamor_program_alpha_dual_blend:
    case glamor_program_alpha_dual_blend_gles2:
        src_type = PICT_FORMAT_TYPE(src);
        mask_type = PICT_FORMAT_TYPE(mask);
        break;
    default:
        return FALSE;
    }

    if (src_type == mask_type) {
        *des = PICT_VISFORMAT(src_bpp, src_type, new_vis);
        return TRUE;
    }

    for (i = 0; i < ARRAY_SIZE(pict_format_combine_tab); i++) {
        if ((src_type == pict_format_combine_tab[i][0]
             && mask_type == pict_format_combine_tab[i][1])
            || (src_type == pict_format_combine_tab[i][1]
                && mask_type == pict_format_combine_tab[i][0])) {
            *des = PICT_VISFORMAT(src_bpp, pict_format_combine_tab[i]
                                  [2], new_vis);
            return TRUE;
        }
    }
    return FALSE;
}

static void
glamor_set_normalize_tcoords_generic(PixmapPtr pixmap,
                                     glamor_pixmap_private *priv,
                                     int repeat_type,
                                     float *matrix,
                                     float xscale, float yscale,
                                     int x1, int y1, int x2, int y2,
                                     float *texcoords,
                                     int stride)
{
    if (!matrix && repeat_type == RepeatNone)
        glamor_set_normalize_tcoords_ext(priv, xscale, yscale,
                                         x1, y1,
                                         x2, y2, texcoords, stride);
    else if (matrix && repeat_type == RepeatNone)
        glamor_set_transformed_normalize_tcoords_ext(priv, matrix, xscale,
                                                     yscale, x1, y1,
                                                     x2, y2,
                                                     texcoords, stride);
    else if (!matrix && repeat_type != RepeatNone)
        glamor_set_repeat_normalize_tcoords_ext(pixmap, priv, repeat_type,
                                                xscale, yscale,
                                                x1, y1,
                                                x2, y2,
                                                texcoords, stride);
    else if (matrix && repeat_type != RepeatNone)
        glamor_set_repeat_transformed_normalize_tcoords_ext(pixmap, priv, repeat_type,
                                                            matrix, xscale,
                                                            yscale, x1, y1, x2,
                                                            y2,
                                                            texcoords, stride);
}

/**
 * Returns whether the general composite path supports this picture
 * format for a pixmap that is permanently stored in an FBO (as
 * opposed to the dynamic upload path).
 *
 * We could support many more formats by using GL_ARB_texture_view to
 * parse the same bits as different formats.  For now, we only support
 * tweaking whether we sample the alpha bits, or just force them to 1.
 */
static Bool
glamor_render_format_is_supported(PicturePtr picture)
{
    PictFormatShort storage_format;
    glamor_screen_private *glamor_priv;
    struct glamor_format *f;

    /* Source-only pictures should always work */
    if (!picture->pDrawable)
        return TRUE;

    glamor_priv = glamor_get_screen_private(picture->pDrawable->pScreen);
    f = &glamor_priv->formats[glamor_drawable_effective_depth(picture->pDrawable)];

    if (!f->rendering_supported)
        return FALSE;

    storage_format = f->render_format;

    switch (picture->format) {
    case PICT_a2r10g10b10:
        return storage_format == PICT_x2r10g10b10;
    case PICT_a8r8g8b8:
    case PICT_x8r8g8b8:
        return storage_format == PICT_a8r8g8b8 || storage_format == PICT_x8r8g8b8;
    case PICT_a1r5g5b5:
        return storage_format == PICT_x1r5g5b5;
    default:
        return picture->format == storage_format;
    }
}

static Bool
render_op_uses_src_alpha(CARD8 op)
{
    struct blendinfo *info = &composite_op_info[op];

    switch (info->dest_blend) {
    case GL_ONE_MINUS_SRC_ALPHA:
    case GL_SRC_ALPHA:
        return TRUE;
    }

    return FALSE;
}

/**
* Returns whether a picture w/ a drawable can be replaced with a solid block
* of pixels.
*
* This will be true if either of the dimensions are equal to 0.
*/
static Bool
glamor_composite_can_replace_with_solid(PicturePtr picture)
{
    PixmapPtr pixmap = glamor_get_drawable_pixmap(picture->pDrawable);

    return (!pixmap->drawable.width || !pixmap->drawable.height);
}

static Bool
glamor_composite_choose_shader(CARD8 op,
                               PicturePtr source,
                               PicturePtr mask,
                               PicturePtr dest,
                               PixmapPtr source_pixmap,
                               PixmapPtr mask_pixmap,
                               PixmapPtr dest_pixmap,
                               glamor_pixmap_private *source_pixmap_priv,
                               glamor_pixmap_private *mask_pixmap_priv,
                               glamor_pixmap_private *dest_pixmap_priv,
                               struct shader_key *s_key,
                               glamor_composite_shader ** shader,
                               struct blendinfo *op_info,
                               PictFormatShort *psaved_source_format,
                               enum ca_state ca_state)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    Bool source_needs_upload = FALSE;
    Bool mask_needs_upload = FALSE;
    PictFormatShort saved_source_format = 0;
    struct shader_key key;
    GLfloat source_solid_color[4];
    GLfloat mask_solid_color[4];
    Bool ret = FALSE;

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dest_pixmap_priv)) {
        glamor_fallback("dest has no fbo.\n");
        goto fail;
    }

    if (!glamor_render_format_is_supported(dest)) {
        glamor_fallback("Unsupported dest picture format.\n");
        goto fail;
    }

    memset(&key, 0, sizeof(key));
    if (!source) {
        key.source = SHADER_SOURCE_SOLID;
        source_solid_color[0] = 0.0;
        source_solid_color[1] = 0.0;
        source_solid_color[2] = 0.0;
        source_solid_color[3] = 0.0;
    }
    else if (!source->pDrawable) {
        SourcePictPtr sp = source->pSourcePict;
        if (sp->type == SourcePictTypeSolidFill) {
            key.source = SHADER_SOURCE_SOLID;
            glamor_get_rgba_from_color(&sp->solidFill.fullcolor,
                                       source_solid_color);
        }
        else if (sp->type == SourcePictTypeLinear ||
                 sp->type == SourcePictTypeRadial ||
                 sp->type == SourcePictTypeConical) {
            key.source = SHADER_SOURCE_GRADIENT;
        }
        else
            goto fail;
    }
    else if (glamor_composite_can_replace_with_solid(source)) {
        key.source = SHADER_SOURCE_SOLID;
        source_solid_color[0] = 0.0;
        source_solid_color[1] = 0.0;
        source_solid_color[2] = 0.0;
        source_solid_color[3] = 0.0;
    }
    else {
        if (PICT_FORMAT_A(source->format))
            key.source = SHADER_SOURCE_TEXTURE_ALPHA;
        else
            key.source = SHADER_SOURCE_TEXTURE;
    }

    if (mask) {
        if (!mask->pDrawable) {
            SourcePictPtr sp = mask->pSourcePict;
            if (sp->type == SourcePictTypeSolidFill) {
                key.mask = SHADER_MASK_SOLID;
                glamor_get_rgba_from_color(&sp->solidFill.fullcolor,
                                           mask_solid_color);
            }
            else if (sp->type == SourcePictTypeLinear ||
                     sp->type == SourcePictTypeRadial ||
                     sp->type == SourcePictTypeConical) {
                key.mask = SHADER_MASK_GRADIENT;
            }
            else
                goto fail;
        }
        else if (glamor_composite_can_replace_with_solid(mask)) {
            key.mask = SHADER_MASK_NONE;
        }
        else {
            if (PICT_FORMAT_A(mask->format))
                key.mask = SHADER_MASK_TEXTURE_ALPHA;
            else
                key.mask = SHADER_MASK_TEXTURE;
        }

        if (!mask->componentAlpha) {
            key.in = glamor_program_alpha_normal;
        }
        else {
            if (op == PictOpClear)
                key.mask = SHADER_MASK_NONE;
            else if (glamor_priv->has_dual_blend) {
                key.in = glamor_glsl_has_ints(glamor_priv) ?
                    glamor_program_alpha_dual_blend :
                    glamor_program_alpha_dual_blend_gles2;
            }
            else if (op == PictOpSrc || op == PictOpAdd
                     || op == PictOpIn || op == PictOpOut
                     || op == PictOpOverReverse)
                key.in = glamor_program_alpha_ca_second;
            else if (op == PictOpOutReverse || op == PictOpInReverse) {
                key.in = glamor_program_alpha_ca_first;
            }
            else {
                glamor_fallback("Unsupported component alpha op: %d\n", op);
                goto fail;
            }
        }
    }
    else {
        key.mask = SHADER_MASK_NONE;
    }

    if (dest_pixmap->drawable.bitsPerPixel <= 8 &&
        glamor_priv->formats[8].format == GL_RED) {
        key.dest_swizzle = SHADER_DEST_SWIZZLE_ALPHA_TO_RED;
    } else {
        if (dest_pixmap->drawable.depth == 32 &&
            glamor_drawable_effective_depth(dest->pDrawable) == 24 &&
            !render_op_uses_src_alpha(op))
            key.dest_swizzle = SHADER_DEST_SWIZZLE_IGNORE_ALPHA;
        else
            key.dest_swizzle = SHADER_DEST_SWIZZLE_DEFAULT;
    }

    if (source && source->alphaMap) {
        glamor_fallback("source alphaMap\n");
        goto fail;
    }
    if (mask && mask->alphaMap) {
        glamor_fallback("mask alphaMap\n");
        goto fail;
    }

    if (key.source == SHADER_SOURCE_TEXTURE ||
        key.source == SHADER_SOURCE_TEXTURE_ALPHA) {
        if (source_pixmap == dest_pixmap) {
            /* XXX source and the dest share the same texture.
             * Does it need special handle? */
            glamor_fallback("source == dest\n");
        }
        if (source_pixmap_priv->gl_fbo == GLAMOR_FBO_UNATTACHED) {
            source_needs_upload = TRUE;
        }
    }

    if (key.mask == SHADER_MASK_TEXTURE ||
        key.mask == SHADER_MASK_TEXTURE_ALPHA) {
        if (mask_pixmap == dest_pixmap) {
            glamor_fallback("mask == dest\n");
            goto fail;
        }
        if (mask_pixmap_priv->gl_fbo == GLAMOR_FBO_UNATTACHED) {
            mask_needs_upload = TRUE;
        }
    }

    if (source_needs_upload && mask_needs_upload
        && source_pixmap == mask_pixmap) {

        if (source->format != mask->format) {
            saved_source_format = source->format;

            if (!combine_pict_format(&source->format, source->format,
                                     mask->format, key.in)) {
                glamor_fallback("combine source %x mask %x failed.\n",
                                source->format, mask->format);
                goto fail;
            }

            /* XXX
             * By default, glamor_upload_picture_to_texture will wire alpha to 1
             * if one picture doesn't have alpha. So we don't do that again in
             * rendering function. But here is a special case, as source and
             * mask share the same texture but may have different formats. For
             * example, source doesn't have alpha, but mask has alpha. Then the
             * texture will have the alpha value for the mask. And will not wire
             * to 1 for the source. In this case, we have to use different shader
             * to wire the source's alpha to 1.
             *
             * But this may cause a potential problem if the source's repeat mode
             * is REPEAT_NONE, and if the source is smaller than the dest, then
             * for the region not covered by the source may be painted incorrectly.
             * because we wire the alpha to 1.
             *
             **/
            if (!PICT_FORMAT_A(saved_source_format)
                && PICT_FORMAT_A(mask->format))
                key.source = SHADER_SOURCE_TEXTURE;

            if (!PICT_FORMAT_A(mask->format)
                && PICT_FORMAT_A(saved_source_format))
                key.mask = SHADER_MASK_TEXTURE;
        }

        if (!glamor_upload_picture_to_texture(source)) {
            glamor_fallback("Failed to upload source texture.\n");
            goto fail;
        }
        mask_needs_upload = FALSE;
    }
    else {
        if (source_needs_upload) {
            if (!glamor_upload_picture_to_texture(source)) {
                glamor_fallback("Failed to upload source texture.\n");
                goto fail;
            }
        } else {
            if (source && !glamor_render_format_is_supported(source)) {
                glamor_fallback("Unsupported source picture format.\n");
                goto fail;
            }
        }

        if (mask_needs_upload) {
            if (!glamor_upload_picture_to_texture(mask)) {
                glamor_fallback("Failed to upload mask texture.\n");
                goto fail;
            }
        } else if (mask) {
            if (!glamor_render_format_is_supported(mask)) {
                glamor_fallback("Unsupported mask picture format.\n");
                goto fail;
            }
        }
    }

    /* If the source and mask are two differently-formatted views of
     * the same pixmap bits, and the pixmap was already uploaded (so
     * the dynamic code above doesn't apply), then fall back to
     * software.  We should use texture views to fix this properly.
     */
    if (source_pixmap && source_pixmap == mask_pixmap &&
        source->format != mask->format) {
        goto fail;
    }

    if (!glamor_set_composite_op(screen, op, op_info, dest, mask, ca_state,
                                 &key)) {
        goto fail;
    }

    *shader = glamor_lookup_composite_shader(screen, &key);
    if ((*shader)->prog == 0) {
        glamor_fallback("no shader program for this render acccel mode\n");
        goto fail;
    }

    if (key.source == SHADER_SOURCE_SOLID)
        memcpy(&(*shader)->source.solid_color[0],
               source_solid_color, 4 * sizeof(float));
    else {
        (*shader)->source.pict.pixmap = source_pixmap;
        (*shader)->source.pict.picture = source;
    }

    if (key.mask == SHADER_MASK_SOLID)
        memcpy(&(*shader)->mask.solid_color[0],
               mask_solid_color, 4 * sizeof(float));
    else {
        (*shader)->mask.pict.pixmap = mask_pixmap;
        (*shader)->mask.pict.picture = mask;
    }

    ret = TRUE;
    memcpy(s_key, &key, sizeof(key));
    *psaved_source_format = saved_source_format;
    goto done;

 fail:
    if (saved_source_format)
        source->format = saved_source_format;
 done:
    return ret;
}

static Bool
glamor_composite_set_shader_blend(ScreenPtr screen,
                                  glamor_screen_private *glamor_priv,
                                  glamor_pixmap_private *dest_priv,
                                  struct shader_key *key,
                                  glamor_composite_shader *shader,
                                  struct blendinfo *op_info)
{
    glamor_make_current(glamor_priv);
    glUseProgram(shader->prog);

    if (key->source == SHADER_SOURCE_SOLID) {
        glamor_set_composite_solid(shader->source.solid_color,
                                   shader->source.uniform_location);
    }
    else if (key->source == SHADER_SOURCE_GRADIENT) {
        if (!glamor_set_composite_gradient(glamor_priv, screen,
                                           shader->source.pict.picture,
                                           GLAMOR_GRADIENT_LUT_SOURCE_UNIT,
                                           &shader->source))
            return FALSE;
    }
    else {
        glamor_set_composite_texture(glamor_priv, 0,
                                     shader->source.pict.picture,
                                     shader->source.pict.pixmap,
                                     shader->source.wh,
                                     shader->source.repeat_mode,
                                     dest_priv);
    }

    if (key->mask != SHADER_MASK_NONE) {
        if (key->mask == SHADER_MASK_SOLID) {
            glamor_set_composite_solid(shader->mask.solid_color,
                                       shader->mask.uniform_location);
        }
        else if (key->mask == SHADER_MASK_GRADIENT) {
            if (!glamor_set_composite_gradient(glamor_priv, screen,
                                               shader->mask.pict.picture,
                                               GLAMOR_GRADIENT_LUT_MASK_UNIT,
                                               &shader->mask))
                return FALSE;
        }
        else {
            glamor_set_composite_texture(glamor_priv, 1,
                                         shader->mask.pict.picture,
                                         shader->mask.pict.pixmap,
                                         shader->mask.wh,
                                         shader->mask.repeat_mode,
                                         dest_priv);
        }
    }

    if (!glamor_priv->is_gles)
        glDisable(GL_COLOR_LOGIC_OP);

    if (op_info->source_blend == GL_ONE && op_info->dest_blend == GL_ZERO) {
        glDisable(GL_BLEND);
    }
    else {
        glEnable(GL_BLEND);
        glBlendFunc(op_info->source_blend, op_info->dest_blend);
    }

    return TRUE;
}

static Bool
glamor_composite_with_shader(CARD8 op,
                             PicturePtr source,
                             PicturePtr mask,
                             PicturePtr dest,
                             PixmapPtr source_pixmap,
                             PixmapPtr mask_pixmap,
                             PixmapPtr dest_pixmap,
                             glamor_pixmap_private *source_pixmap_priv,
                             glamor_pixmap_private *mask_pixmap_priv,
                             glamor_pixmap_private *dest_pixmap_priv,
                             int nrect, glamor_composite_rect_t *rects,
                             enum ca_state ca_state)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    GLfloat dst_xscale, dst_yscale;
    GLfloat mask_xscale = 1, mask_yscale = 1, src_xscale = 1, src_yscale = 1;
    struct shader_key key, key_ca;
    int dest_x_off, dest_y_off;
    int source_x_off, source_y_off;
    int mask_x_off, mask_y_off;
    PictFormatShort saved_source_format = 0;
    float src_matrix[9], mask_matrix[9];
    float *psrc_matrix = NULL, *pmask_matrix = NULL;
    int nrect_max;
    Bool ret = FALSE;
    glamor_composite_shader *shader = NULL, *shader_ca = NULL;
    struct blendinfo op_info, op_info_ca;
    Bool restore_colormask = FALSE;

    if (!glamor_composite_choose_shader(op, source, mask, dest,
                                        source_pixmap, mask_pixmap, dest_pixmap,
                                        source_pixmap_priv, mask_pixmap_priv,
                                        dest_pixmap_priv,
                                        &key, &shader, &op_info,
                                        &saved_source_format, ca_state)) {
        glamor_fallback("glamor_composite_choose_shader failed\n");
        goto fail;
    }
    if (ca_state == CA_TWO_PASS) {
        if (!glamor_composite_choose_shader(PictOpAdd, source, mask, dest,
                                            source_pixmap, mask_pixmap, dest_pixmap,
                                            source_pixmap_priv,
                                            mask_pixmap_priv, dest_pixmap_priv,
                                            &key_ca, &shader_ca, &op_info_ca,
                                            &saved_source_format, ca_state)) {
            glamor_fallback("glamor_composite_choose_shader failed\n");
            goto fail;
        }
    }

    glamor_make_current(glamor_priv);

    if (ca_state != CA_TWO_PASS &&
        key.dest_swizzle == SHADER_DEST_SWIZZLE_DEFAULT &&
        dest_pixmap->drawable.depth == 32 &&
        glamor_drawable_effective_depth(dest->pDrawable) == 24) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        restore_colormask = TRUE;
    }

    glamor_set_destination_pixmap_priv_nc(glamor_priv, dest_pixmap, dest_pixmap_priv);
    if (!glamor_composite_set_shader_blend(screen, glamor_priv,
                                           dest_pixmap_priv, &key, shader,
                                           &op_info)) {
        glamor_fallback("glamor_composite_set_shader_blend failed\n");
        goto fail;
    }
    glamor_set_alu(dest->pDrawable, GXcopy);

    glamor_priv->has_source_coords = key.source != SHADER_SOURCE_SOLID;
    glamor_priv->has_mask_coords = (key.mask != SHADER_MASK_NONE &&
                                    key.mask != SHADER_MASK_SOLID);

    dest_pixmap = glamor_get_drawable_pixmap(dest->pDrawable);
    dest_pixmap_priv = glamor_get_pixmap_private(dest_pixmap);
    glamor_get_drawable_deltas(dest->pDrawable, dest_pixmap,
                               &dest_x_off, &dest_y_off);
    pixmap_priv_get_dest_scale(dest_pixmap, dest_pixmap_priv, &dst_xscale, &dst_yscale);

    if (glamor_priv->has_source_coords) {
        if (key.source == SHADER_SOURCE_GRADIENT) {
            source_x_off = 0;
            source_y_off = 0;
        }
        else {
            glamor_get_drawable_deltas(source->pDrawable,
                                       source_pixmap, &source_x_off,
                                       &source_y_off);
            pixmap_priv_get_scale(source_pixmap_priv, &src_xscale,
                                  &src_yscale);
            if (source->transform) {
                psrc_matrix = src_matrix;
                glamor_picture_get_matrixf(source, psrc_matrix);
            }
        }
    }

    if (glamor_priv->has_mask_coords) {
        if (key.mask == SHADER_MASK_GRADIENT) {
            mask_x_off = 0;
            mask_y_off = 0;
        }
        else {
            glamor_get_drawable_deltas(mask->pDrawable, mask_pixmap,
                                       &mask_x_off, &mask_y_off);
            pixmap_priv_get_scale(mask_pixmap_priv, &mask_xscale,
                                  &mask_yscale);
            if (mask->transform) {
                pmask_matrix = mask_matrix;
                glamor_picture_get_matrixf(mask, pmask_matrix);
            }
        }
    }

    nrect_max = MIN(nrect, GLAMOR_COMPOSITE_VBO_VERT_CNT / 4);

    if (nrect < 100) {
        BoxRec bounds = glamor_start_rendering_bounds();

        for (int i = 0; i < nrect; i++) {
            BoxRec box = {
                .x1 = rects[i].x_dst,
                .y1 = rects[i].y_dst,
                .x2 = rects[i].x_dst + rects[i].width,
                .y2 = rects[i].y_dst + rects[i].height,
            };
            glamor_bounds_union_box(&bounds, &box);
        }

        if (bounds.x1 >= bounds.x2 || bounds.y1 >= bounds.y2)
            goto disable_va;

        glEnable(GL_SCISSOR_TEST);
        glScissor(bounds.x1 + dest_x_off,
                  bounds.y1 + dest_y_off,
                  bounds.x2 - bounds.x1,
                  bounds.y2 - bounds.y1);
    }

    while (nrect) {
        int mrect, rect_processed;
        int vb_stride;
        float *vertices;

        mrect = nrect > nrect_max ? nrect_max : nrect;

        vertices = glamor_setup_composite_vbo(screen, mrect * 4);
        if (_X_UNLIKELY(!vertices))
            goto fail;

        rect_processed = mrect;
        vb_stride = glamor_priv->vb_stride / sizeof(float);
        while (mrect--) {
            INT16 x_source;
            INT16 y_source;
            INT16 x_mask;
            INT16 y_mask;
            INT16 x_dest;
            INT16 y_dest;
            CARD16 width;
            CARD16 height;

            x_dest = rects->x_dst + dest_x_off;
            y_dest = rects->y_dst + dest_y_off;
            x_source = rects->x_src + source_x_off;
            y_source = rects->y_src + source_y_off;
            x_mask = rects->x_mask + mask_x_off;
            y_mask = rects->y_mask + mask_y_off;
            width = rects->width;
            height = rects->height;

            DEBUGF
                ("dest(%d,%d) source(%d %d) mask (%d %d), width %d height %d \n",
                 x_dest, y_dest, x_source, y_source, x_mask, y_mask, width,
                 height);

            glamor_set_normalize_vcoords_ext(dest_pixmap_priv, dst_xscale,
                                             dst_yscale, x_dest, y_dest,
                                             x_dest + width, y_dest + height,
                                             vertices,
                                             vb_stride);
            vertices += 2;
            if (key.source != SHADER_SOURCE_SOLID) {
                if (key.source == SHADER_SOURCE_GRADIENT) {
                    glamor_set_gradient_coords(vertices, vb_stride,
                                               x_source, y_source,
                                               x_source + width,
                                               y_source + height);
                }
                else {
                    glamor_set_normalize_tcoords_generic(source_pixmap,
                                                         source_pixmap_priv,
                                                         source->repeatType,
                                                         psrc_matrix,
                                                         src_xscale,
                                                         src_yscale, x_source,
                                                         y_source,
                                                         x_source + width,
                                                         y_source + height,
                                                         vertices, vb_stride);
                }
                vertices += 2;
            }

            if (key.mask != SHADER_MASK_NONE && key.mask != SHADER_MASK_SOLID) {
                if (key.mask == SHADER_MASK_GRADIENT) {
                    glamor_set_gradient_coords(vertices, vb_stride,
                                               x_mask, y_mask,
                                               x_mask + width,
                                               y_mask + height);
                }
                else {
                    glamor_set_normalize_tcoords_generic(mask_pixmap,
                                                         mask_pixmap_priv,
                                                         mask->repeatType,
                                                         pmask_matrix,
                                                         mask_xscale,
                                                         mask_yscale, x_mask,
                                                         y_mask, x_mask + width,
                                                         y_mask + height,
                                                         vertices, vb_stride);
                }
                vertices += 2;
            }
            glamor_priv->render_nr_quads++;
            rects++;

            /* We've incremented by one of our 4 verts, now do the other 3. */
            vertices += 3 * vb_stride;
        }
        glamor_put_vbo_space(screen);
        glamor_flush_composite_rects(screen);
        nrect -= rect_processed;
        if (ca_state == CA_TWO_PASS) {
            if (!glamor_composite_set_shader_blend(screen, glamor_priv,
                                                   dest_pixmap_priv,
                                                   &key_ca, shader_ca,
                                                   &op_info_ca))
                goto fail;
            glamor_flush_composite_rects(screen);
            if (nrect) {
                if (!glamor_composite_set_shader_blend(screen, glamor_priv,
                                                       dest_pixmap_priv,
                                                       &key, shader,
                                                       &op_info))
                    goto fail;
            }
        }
    }

    glDisable(GL_SCISSOR_TEST);
disable_va:
    if (restore_colormask)
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_MASK);
    glDisable(GL_BLEND);
    DEBUGF("finish rendering.\n");
    if (saved_source_format)
        source->format = saved_source_format;

    ret = TRUE;

fail:
    if (mask_pixmap && glamor_pixmap_is_memory(mask_pixmap))
        glamor_pixmap_destroy_fbo(mask_pixmap);
    if (source_pixmap && glamor_pixmap_is_memory(source_pixmap))
        glamor_pixmap_destroy_fbo(source_pixmap);

    return ret;
}

Bool
glamor_composite_clipped_region(CARD8 op,
                                PicturePtr source,
                                PicturePtr mask,
                                PicturePtr dest,
                                PixmapPtr source_pixmap,
                                PixmapPtr mask_pixmap,
                                PixmapPtr dest_pixmap,
                                RegionPtr region,
                                int x_source,
                                int y_source,
                                int x_mask, int y_mask, int x_dest, int y_dest)
{
    glamor_pixmap_private *source_pixmap_priv = glamor_get_pixmap_private(source_pixmap);
    glamor_pixmap_private *mask_pixmap_priv = glamor_get_pixmap_private(mask_pixmap);
    glamor_pixmap_private *dest_pixmap_priv = glamor_get_pixmap_private(dest_pixmap);
    glamor_screen_private *glamor_priv = glamor_get_screen_private(dest_pixmap->drawable.pScreen);
    BoxPtr extent;
    glamor_composite_rect_t rect[10];
    glamor_composite_rect_t *prect = rect;
    int prect_size = ARRAY_SIZE(rect);
    int ok = FALSE;
    int i;
    int width;
    int height;
    BoxPtr box;
    int nbox;
    enum ca_state ca_state = CA_NONE;

    extent = RegionExtents(region);
    box = RegionRects(region);
    nbox = RegionNumRects(region);
    width = extent->x2 - extent->x1;
    height = extent->y2 - extent->y1;

    DEBUGF("clipped (%d %d) (%d %d) (%d %d) width %d height %d \n",
           x_source, y_source, x_mask, y_mask, x_dest, y_dest, width, height);

    /* Is the composite operation equivalent to a copy? */
    if (source &&
        !mask && !source->alphaMap && !dest->alphaMap
        && source->pDrawable && !source->transform
        /* CopyArea is only defined with matching depths. */
        && dest->pDrawable->depth == source->pDrawable->depth
        && ((op == PictOpSrc
             && (source->format == dest->format
                 || (PICT_FORMAT_COLOR(dest->format)
                     && PICT_FORMAT_COLOR(source->format)
                     && dest->format == PICT_FORMAT(PICT_FORMAT_BPP(source->format),
                                                    PICT_FORMAT_TYPE(source->format),
                                                    0,
                                                    PICT_FORMAT_R(source->format),
                                                    PICT_FORMAT_G(source->format),
                                                    PICT_FORMAT_B(source->format)))))
            || (op == PictOpOver
                && source->format == dest->format
                && !PICT_FORMAT_A(source->format)))
        && x_source >= 0 && y_source >= 0
        && (x_source + width) <= source->pDrawable->width
        && (y_source + height) <= source->pDrawable->height) {
        x_source += source->pDrawable->x;
        y_source += source->pDrawable->y;
        x_dest += dest->pDrawable->x;
        y_dest += dest->pDrawable->y;
        glamor_copy(source->pDrawable, dest->pDrawable, NULL,
                    box, nbox, x_source - x_dest,
                    y_source - y_dest, FALSE, FALSE, 0, NULL);
        ok = TRUE;
        goto out;
    }

    if (mask && mask->componentAlpha) {
        if (glamor_priv->has_dual_blend) {
            ca_state = CA_DUAL_BLEND;
        } else {
            if (op == PictOpOver) {
                if (glamor_pixmap_is_memory(mask_pixmap)) {
                    glamor_fallback("two pass not supported on memory pximaps\n");
                    goto out;
                }
                ca_state = CA_TWO_PASS;
                op = PictOpOutReverse;
            }
        }
    }

    if (source_pixmap == dest_pixmap) {
        glamor_fallback("source and dest pixmaps are the same\n");
        goto out;
    }
    if (mask_pixmap == dest_pixmap) {
        glamor_fallback("mask and dest pixmaps are the same\n");
        goto out;
    }

    x_dest += dest->pDrawable->x;
    y_dest += dest->pDrawable->y;
    if (source && source->pDrawable) {
        x_source += source->pDrawable->x;
        y_source += source->pDrawable->y;
    }
    if (mask && mask->pDrawable) {
        x_mask += mask->pDrawable->x;
        y_mask += mask->pDrawable->y;
    }

    if (nbox > ARRAY_SIZE(rect)) {
        prect = calloc(nbox, sizeof(*prect));
        if (prect)
            prect_size = nbox;
        else {
            prect = rect;
            prect_size = ARRAY_SIZE(rect);
        }
    }
    while (nbox) {
        int box_cnt;

        box_cnt = nbox > prect_size ? prect_size : nbox;
        for (i = 0; i < box_cnt; i++) {
            prect[i].x_src = box[i].x1 + x_source - x_dest;
            prect[i].y_src = box[i].y1 + y_source - y_dest;
            prect[i].x_mask = box[i].x1 + x_mask - x_dest;
            prect[i].y_mask = box[i].y1 + y_mask - y_dest;
            prect[i].x_dst = box[i].x1;
            prect[i].y_dst = box[i].y1;
            prect[i].width = box[i].x2 - box[i].x1;
            prect[i].height = box[i].y2 - box[i].y1;
            DEBUGF("dest %d %d \n", prect[i].x_dst, prect[i].y_dst);
        }
        ok = glamor_composite_with_shader(op, source, mask, dest,
                                          source_pixmap, mask_pixmap, dest_pixmap,
                                          source_pixmap_priv, mask_pixmap_priv,
                                          dest_pixmap_priv,
                                          box_cnt, prect, ca_state);
        if (!ok)
            break;
        nbox -= box_cnt;
        box += box_cnt;
    }

    if (prect != rect)
        free(prect);
 out:
    return ok;
}

void
glamor_composite(CARD8 op,
                 PicturePtr source,
                 PicturePtr mask,
                 PicturePtr dest,
                 INT16 x_source,
                 INT16 y_source,
                 INT16 x_mask,
                 INT16 y_mask,
                 INT16 x_dest, INT16 y_dest, CARD16 width, CARD16 height)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    PixmapPtr dest_pixmap = glamor_get_drawable_pixmap(dest->pDrawable);
    PixmapPtr source_pixmap = NULL, mask_pixmap = NULL;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    RegionRec region;
    BoxPtr extent;
    int nbox, ok = FALSE;
    int force_clip = 0;

    if (source->pDrawable) {
        source_pixmap = glamor_get_drawable_pixmap(source->pDrawable);
        if (glamor_pixmap_drm_only(source_pixmap))
            goto fail;
    }

    if (mask && mask->pDrawable) {
        mask_pixmap = glamor_get_drawable_pixmap(mask->pDrawable);
        if (glamor_pixmap_drm_only(mask_pixmap))
            goto fail;
    }

    DEBUGF
        ("source pixmap %p (%d %d) mask(%d %d) dest(%d %d) width %d height %d \n",
         source_pixmap, x_source, y_source, x_mask, y_mask, x_dest, y_dest,
         width, height);

    if (!glamor_pixmap_has_fbo(dest_pixmap))
        goto fail;

    if (op >= ARRAY_SIZE(composite_op_info)) {
        glamor_fallback("Unsupported composite op %x\n", op);
        goto fail;
    }

    if (mask && mask->componentAlpha && !glamor_priv->has_dual_blend) {
        if (op == PictOpAtop
            || op == PictOpAtopReverse
            || op == PictOpXor || op >= PictOpSaturate) {
            glamor_fallback("glamor_composite(): component alpha op %x\n", op);
            goto fail;
        }
    }

    if ((source && source->filter >= PictFilterConvolution)
        || (mask && mask->filter >= PictFilterConvolution)) {
        glamor_fallback("glamor_composite(): unsupported filter\n");
        goto fail;
    }

    if (!miComputeCompositeRegion(&region,
                                  source, mask, dest,
                                  x_source +
                                  (source_pixmap ? source->pDrawable->x : 0),
                                  y_source +
                                  (source_pixmap ? source->pDrawable->y : 0),
                                  x_mask +
                                  (mask_pixmap ? mask->pDrawable->x : 0),
                                  y_mask +
                                  (mask_pixmap ? mask->pDrawable->y : 0),
                                  x_dest + dest->pDrawable->x,
                                  y_dest + dest->pDrawable->y, width, height)) {
        return;
    }

    nbox = REGION_NUM_RECTS(&region);
    DEBUGF("first clipped when compositing.\n");
    DEBUGRegionPrint(&region);
    extent = RegionExtents(&region);
    if (nbox == 0)
        return;

    /* If destination is not a large pixmap, but the region is larger
     * than texture size limitation, and source or mask is memory pixmap,
     * then there may be need to load a large memory pixmap to a
     * texture, and this is not permitted. Then we force to clip the
     * destination and make sure latter will not upload a large memory
     * pixmap. */
    if (!glamor_check_fbo_size(glamor_priv,
                               extent->x2 - extent->x1, extent->y2 - extent->y1)
        && glamor_pixmap_is_large(dest_pixmap)
        && ((source_pixmap
             && (glamor_pixmap_is_memory(source_pixmap) ||
                 source->repeatType == RepeatPad))
            || (mask_pixmap &&
                (glamor_pixmap_is_memory(mask_pixmap) ||
                 mask->repeatType == RepeatPad))
            || (!source_pixmap &&
                (source->pSourcePict->type != SourcePictTypeSolidFill))
            || (!mask_pixmap && mask &&
                mask->pSourcePict->type != SourcePictTypeSolidFill)))
        force_clip = 1;

    if (force_clip || glamor_pixmap_is_large(dest_pixmap)
        || (source_pixmap
            && glamor_pixmap_is_large(source_pixmap))
        || (mask_pixmap && glamor_pixmap_is_large(mask_pixmap)))
        ok = glamor_composite_largepixmap_region(op,
                                                 source, mask, dest,
                                                 source_pixmap,
                                                 mask_pixmap,
                                                 dest_pixmap,
                                                 &region, force_clip,
                                                 x_source, y_source,
                                                 x_mask, y_mask,
                                                 x_dest, y_dest, width, height);
    else
        ok = glamor_composite_clipped_region(op, source,
                                             mask, dest,
                                             source_pixmap,
                                             mask_pixmap,
                                             dest_pixmap,
                                             &region,
                                             x_source, y_source,
                                             x_mask, y_mask, x_dest, y_dest);

    REGION_UNINIT(dest->pDrawable->pScreen, &region);

    if (ok)
        return;

 fail:

    glamor_fallback
        ("from picts %p:%p %dx%d / %p:%p %d x %d (%c,%c)  to pict %p:%p %dx%d (%c)\n",
         source, source->pDrawable,
         source->pDrawable ? source->pDrawable->width : 0,
         source->pDrawable ? source->pDrawable->height : 0, mask,
         (!mask) ? NULL : mask->pDrawable,
         (!mask || !mask->pDrawable) ? 0 : mask->pDrawable->width,
         (!mask || !mask->pDrawable) ? 0 : mask->pDrawable->height,
         glamor_get_picture_location(source),
         glamor_get_picture_location(mask),
         dest, dest->pDrawable,
         dest->pDrawable->width, dest->pDrawable->height,
         glamor_get_picture_location(dest));

    if (glamor_prepare_access_picture_box(dest, GLAMOR_ACCESS_RW,
                                          x_dest, y_dest, width, height) &&
        glamor_prepare_access_picture_box(source, GLAMOR_ACCESS_RO,
                                          x_source, y_source, width, height) &&
        glamor_prepare_access_picture_box(mask, GLAMOR_ACCESS_RO,
                                          x_mask, y_mask, width, height))
    {
        fbComposite(op,
                    source, mask, dest,
                    x_source, y_source,
                    x_mask, y_mask, x_dest, y_dest, width, height);
    }
    glamor_finish_access_picture(mask);
    glamor_finish_access_picture(source);
    glamor_finish_access_picture(dest);
}
