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
 *    Junyan He <junyan.he@linux.intel.com>
 *
 */

/** @file glamor_gradient.c
 *
 * Gradient acceleration implementation
 */

#include "glamor_priv.h"

static void
_glamor_gradient_convert_trans_matrix(PictTransform *from, float to[3][3],
                                      int width, int height, int normalize)
{
    /*
     * Because in the shader program, we normalize all the pixel cood to [0, 1],
     * so with the transform matrix, the correct logic should be:
     * v_s = A*T*v
     * v_s: point vector in shader after normalized.
     * A: The transition matrix from   width X height --> 1.0 X 1.0
     * T: The transform matrix.
     * v: point vector in width X height space.
     *
     * result is OK if we use this formula. But for every point in width X height space,
     * we can just use their normalized point vector in shader, namely we can just
     * use the result of A*v in shader. So we have no chance to insert T in A*v.
     * We can just convert v_s = A*T*v to v_s = A*T*inv(A)*A*v, where inv(A) is the
     * inverse matrix of A. Now, v_s = (A*T*inv(A)) * (A*v)
     * So, to get the correct v_s, we need to cacula1 the matrix: (A*T*inv(A)), and
     * we name this matrix T_s.
     *
     * Firstly, because A is for the scale conversion, we find
     *      --         --
     *      |1/w  0   0 |
     * A =  | 0  1/h  0 |
     *      | 0   0  1.0|
     *      --         --
     * so T_s = A*T*inv(a) and result
     *
     *       --                      --
     *       | t11      h*t12/w  t13/w|
     * T_s = | w*t21/h  t22      t23/h|
     *       | w*t31    h*t32    t33  |
     *       --                      --
     *
     * Because GLES2 cannot do transposed mat by spec, we did transposing inside this function
     * already, and matrix becoming look like this:
     *       --                      --
     *       | t11      w*t21/h  t31*w|
     * T_s = | h*t12/w  t22      t32*h|
     *       | t13/w    t23/h    t33  |
     *       --                      --
     */

    to[0][0] = (float) pixman_fixed_to_double(from->matrix[0][0]);
    to[1][0] = (float) pixman_fixed_to_double(from->matrix[0][1])
        * (normalize ? (((float) height) / ((float) width)) : 1.0);
    to[2][0] = (float) pixman_fixed_to_double(from->matrix[0][2])
        / (normalize ? ((float) width) : 1.0);

    to[0][1] = (float) pixman_fixed_to_double(from->matrix[1][0])
        * (normalize ? (((float) width) / ((float) height)) : 1.0);
    to[1][1] = (float) pixman_fixed_to_double(from->matrix[1][1]);
    to[2][1] = (float) pixman_fixed_to_double(from->matrix[1][2])
        / (normalize ? ((float) height) : 1.0);

    to[0][2] = (float) pixman_fixed_to_double(from->matrix[2][0])
        * (normalize ? ((float) width) : 1.0);
    to[1][2] = (float) pixman_fixed_to_double(from->matrix[2][1])
        * (normalize ? ((float) height) : 1.0);
    to[2][2] = (float) pixman_fixed_to_double(from->matrix[2][2]);

    DEBUGF("the transposed transform matrix is:\n%f\t%f\t%f\n%f\t%f\t%f\n%f\t%f\t%f\n",
           to[0][0], to[0][1], to[0][2],
           to[1][0], to[1][1], to[1][2], to[2][0], to[2][1], to[2][2]);
}

#define GLAMOR_GRADIENT_STOP_EPSILON 0.000001f
#define GLAMOR_GRADIENT_HASH_BASE    2166136261u
#define GLAMOR_GRADIENT_HASH_PRIME   16777619u

static CARD32
_glamor_gradient_hash_stop(CARD32 hash, CARD32 value)
{
    hash ^= value;
    return hash * GLAMOR_GRADIENT_HASH_PRIME;
}

static CARD32
_glamor_gradient_hash(PictGradient *gradient)
{
    CARD32 hash = GLAMOR_GRADIENT_HASH_BASE;
    int i;

    hash = _glamor_gradient_hash_stop(hash, (CARD32) gradient->nstops);
    for (i = 0; i < gradient->nstops; i++) {
        hash = _glamor_gradient_hash_stop(hash, (CARD32) gradient->stops[i].x);
        hash =
            _glamor_gradient_hash_stop(hash, gradient->stops[i].color.red);
        hash =
            _glamor_gradient_hash_stop(hash, gradient->stops[i].color.green);
        hash =
            _glamor_gradient_hash_stop(hash, gradient->stops[i].color.blue);
        hash =
            _glamor_gradient_hash_stop(hash, gradient->stops[i].color.alpha);
    }
    return hash;
}

static void
_glamor_gradient_lut_sample(PictGradient *gradient, float t, float *rgba)
{
    int n = gradient->nstops;
    int lo;
    int hi;
    int i;
    float before;
    float delta;
    float f;

    if (t <= (float) pixman_fixed_to_double(gradient->stops[0].x)) {
        lo = 0;
        hi = 0;
        f = 0.0f;
    }
    else if (t >=
             (float) pixman_fixed_to_double(gradient->stops[n - 1].x)) {
        lo = n - 1;
        hi = n - 1;
        f = 0.0f;
    }
    else {
        for (i = 1; i < n; i++) {
            if (t <=
                (float) pixman_fixed_to_double(gradient->stops[i].x))
                break;
        }
        lo = i - 1;
        hi = i;
        before =
            (float) pixman_fixed_to_double(gradient->stops[lo].x);
        delta =
            (float) pixman_fixed_to_double(gradient->stops[hi].x) - before;
        if (delta < GLAMOR_GRADIENT_STOP_EPSILON)
            f = 0.0f;
        else {
            f = (t - before) / delta;
            f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        }
    }

    rgba[0] =
        (gradient->stops[lo].color.red * (1.0f - f) +
         gradient->stops[hi].color.red * f) / 65535.0f;
    rgba[1] =
        (gradient->stops[lo].color.green * (1.0f - f) +
         gradient->stops[hi].color.green * f) / 65535.0f;
    rgba[2] =
        (gradient->stops[lo].color.blue * (1.0f - f) +
         gradient->stops[hi].color.blue * f) / 65535.0f;
    rgba[3] =
        (gradient->stops[lo].color.alpha * (1.0f - f) +
         gradient->stops[hi].color.alpha * f) / 65535.0f;
}

GLuint
glamor_gradient_get_lut(ScreenPtr screen, PictGradientPtr gradient)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    CARD32 hash;
    CARD8 *bytes;
    int i;
    int slot = -1;
    unsigned oldest = 0;
    float rgba[4];
    float t;

    if (gradient == NULL || gradient->nstops < 1 || gradient->stops == NULL)
        return 0;

    hash = _glamor_gradient_hash(gradient);

    for (i = 0; i < GLAMOR_GRADIENT_LUT_CACHE_SIZE; i++) {
        if (glamor_priv->gradient_lut_cache[i].tex == 0) {
            if (slot < 0)
                slot = i;
            continue;
        }
        if (glamor_priv->gradient_lut_cache[i].hash == hash &&
            glamor_priv->gradient_lut_cache[i].nstops == gradient->nstops &&
            memcmp(glamor_priv->gradient_lut_cache[i].stops,
                   gradient->stops,
                   gradient->nstops * sizeof(PictGradientStop)) == 0) {
            glamor_priv->gradient_lut_cache[i].last_used =
                ++glamor_priv->gradient_lut_clock;
            return glamor_priv->gradient_lut_cache[i].tex;
        }
        if (slot < 0 ||
            glamor_priv->gradient_lut_cache[i].last_used < oldest) {
            oldest = glamor_priv->gradient_lut_cache[i].last_used;
            slot = i;
        }
    }

    bytes = xallocarray(GLAMOR_GRADIENT_LUT_SIZE, 4);
    if (_X_UNLIKELY(bytes == NULL))
        return 0;

    for (i = 0; i < GLAMOR_GRADIENT_LUT_SIZE; i++) {
        t = ((float) i + 0.5f) / (float) GLAMOR_GRADIENT_LUT_SIZE;
        _glamor_gradient_lut_sample(gradient, t, rgba);
        bytes[4 * i + 0] = (CARD8) (rgba[0] * rgba[3] * 255.0f + 0.5f);
        bytes[4 * i + 1] = (CARD8) (rgba[1] * rgba[3] * 255.0f + 0.5f);
        bytes[4 * i + 2] = (CARD8) (rgba[2] * rgba[3] * 255.0f + 0.5f);
        bytes[4 * i + 3] = (CARD8) (rgba[3] * 255.0f + 0.5f);
    }

    if (glamor_priv->gradient_lut_cache[slot].tex != 0) {
        glamor_make_current(glamor_priv);
        glDeleteTextures(1, &glamor_priv->gradient_lut_cache[slot].tex);
        free(glamor_priv->gradient_lut_cache[slot].stops);
        glamor_priv->gradient_lut_cache[slot].stops = NULL;
    }
    else {
        glamor_make_current(glamor_priv);
    }

    glGenTextures(1, &glamor_priv->gradient_lut_cache[slot].tex);
    glBindTexture(GL_TEXTURE_2D, glamor_priv->gradient_lut_cache[slot].tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 GLAMOR_GRADIENT_LUT_SIZE, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, bytes);
    free(bytes);

    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &glamor_priv->gradient_lut_cache[slot].tex);
        glamor_priv->gradient_lut_cache[slot].tex = 0;
        glamor_priv->gradient_lut_cache[slot].stops = NULL;
        return 0;
    }

    glamor_priv->gradient_lut_cache[slot].stops =
        xallocarray(gradient->nstops, sizeof(PictGradientStop));
    if (glamor_priv->gradient_lut_cache[slot].stops == NULL) {
        glDeleteTextures(1, &glamor_priv->gradient_lut_cache[slot].tex);
        glamor_priv->gradient_lut_cache[slot].tex = 0;
        glamor_priv->gradient_lut_cache[slot].stops = NULL;
        return 0;
    }

    memcpy(glamor_priv->gradient_lut_cache[slot].stops, gradient->stops,
           gradient->nstops * sizeof(PictGradientStop));
    glamor_priv->gradient_lut_cache[slot].hash = hash;
    glamor_priv->gradient_lut_cache[slot].nstops = gradient->nstops;
    glamor_priv->gradient_lut_cache[slot].last_used =
        ++glamor_priv->gradient_lut_clock;

    return glamor_priv->gradient_lut_cache[slot].tex;
}

void
glamor_gradient_lut_fini(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int i;

    glamor_make_current(glamor_priv);
    for (i = 0; i < GLAMOR_GRADIENT_LUT_CACHE_SIZE; i++) {
        if (glamor_priv->gradient_lut_cache[i].tex != 0) {
            glDeleteTextures(1, &glamor_priv->gradient_lut_cache[i].tex);
            glamor_priv->gradient_lut_cache[i].tex = 0;
        }
        free(glamor_priv->gradient_lut_cache[i].stops);
        glamor_priv->gradient_lut_cache[i].stops = NULL;
    }
}

void
glamor_gradient_get_transform(PicturePtr picture, float transform[3][3])
{
    static const float identity[3][3] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }
    };

    if (picture->transform)
        _glamor_gradient_convert_trans_matrix(picture->transform,
                                              transform, 0, 0, 0);
    else
        memcpy(transform, identity, sizeof(identity));
}
