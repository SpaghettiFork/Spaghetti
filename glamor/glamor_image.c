/*
 * Copyright © 2014 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "glamor_priv.h"
#include "glamor_transfer.h"
#include "glamor_transform.h"
#include "glamor_program.h"

/*
 * ZPixmap PutImage
 */

static Bool
glamor_put_image_zpixmap_gl(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                            int w, int h, int leftPad, int format, char *bits)
{
    ScreenPtr screen = drawable->pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv;
    uint32_t    byte_stride = PixmapBytePad(w, drawable->depth);
    RegionRec   region;
    BoxRec      box;
    int         off_x, off_y;

    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return FALSE;

    if (gc->alu != GXcopy)
        goto bail;

    if (!glamor_pm_is_solid(gc->depth, gc->planemask))
        goto bail;

    x += drawable->x;
    y += drawable->y;
    box.x1 = x;
    box.y1 = y;
    box.x2 = box.x1 + w;
    box.y2 = box.y1 + h;
    RegionInit(&region, &box, 1);
    RegionIntersect(&region, &region, gc->pCompositeClip);

    glamor_get_drawable_deltas(drawable, pixmap, &off_x, &off_y);
    if (off_x || off_y) {
        x += off_x;
        y += off_y;
        RegionTranslate(&region, off_x, off_y);
    }

    glamor_make_current(glamor_priv);

    glamor_upload_region(drawable, &region, x, y, (uint8_t *) bits, byte_stride);

    RegionUninit(&region);
    return TRUE;
bail:
    return FALSE;
}

/*
 * XYBitmap PutImage
 */

static const char vs_vars_put_bitmap[] =
    "in vec4 primitive;\n"
    "in vec2 source;\n"
    "out vec2 image_pos;\n";

static const char vs_exec_put_bitmap[] =
    "       vec2 pos = primitive.zw * vec2(gl_VertexID&1, (gl_VertexID&2)>>1);\n"
    GLAMOR_POS(gl_Position, (primitive.xy + pos))
    "       image_pos = source + pos;\n";

static const char fs_vars_put_bitmap[] =
    "in vec2 image_pos;\n"
    "uniform sampler2D font;\n"
    "uniform vec2 bitmap_size;\n";

static Bool
glamor_put_bitmap_use(DrawablePtr drawable, GCPtr gc, glamor_program *prog, void *arg)
{
    GLint loc;

    if (!glamor_set_solid(drawable, gc, TRUE, prog->fg_uniform))
        return FALSE;

    glamor_set_color(drawable, gc->bgPixel, prog->bg_uniform);

    loc = glGetUniformLocation(prog->prog, "font");
    if (loc >= 0)
        glUniform1i(loc, 1);

    return TRUE;
}

static const char fs_exec_put_bitmap[] =
    "       ivec2 itile_texture = ivec2(image_pos);\n"
#if BITMAP_BIT_ORDER == MSBFirst
    "       int x = 7 - (itile_texture.x & 7);\n"
#else
    "       int x = itile_texture.x & 7;\n"
#endif
    "       itile_texture.x >>= 3;\n"
    "       float byte_val = texture(font, (vec2(itile_texture) + 0.5) / bitmap_size).r;\n"
    "       int texel = int(floor(byte_val * 255.0 + 0.5));\n"
    "       int bit = (texel >> x) & 1;\n"
    "       if (bit == 0)\n"
    "               frag_color = bg;\n"
    "       else\n"
    "               frag_color = fg;\n";

static const glamor_facet glamor_facet_put_bitmap = {
    .name = "put_bitmap",
    .version = 130,
    .vs_vars = vs_vars_put_bitmap,
    .vs_exec = vs_exec_put_bitmap,
    .fs_vars = fs_vars_put_bitmap,
    .fs_exec = fs_exec_put_bitmap,
    .locations = glamor_program_location_fg |
                 glamor_program_location_bg,
    .source_name = "source",
    .use = glamor_put_bitmap_use,
};

static Bool
glamor_put_image_xybitmap_gl(DrawablePtr drawable, GCPtr gc, int x, int y,
                             int w, int h, int leftPad, char *bits)
{
    ScreenPtr screen = drawable->pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    uint32_t byte_stride = BitmapBytePad(w + leftPad);
    GLuint texture_id;
    glamor_program *prog;
    char *vbo_offset;
    GLshort *v;
    int box_index;
    int off_x, off_y;
    Bool ret = FALSE;

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return FALSE;

    glamor_make_current(glamor_priv);

    prog = &glamor_priv->put_bitmap_prog;
    if (prog->failed)
        goto bail;

    if (!prog->prog) {
        if (!glamor_build_program(screen, prog, &glamor_facet_put_bitmap, NULL, NULL, NULL)) {
            goto bail;
        }
    }

    if (!glamor_use_program(&pixmap->drawable, gc, prog, NULL))
        goto bail;

    /* Upload bitmap as GL_R8 (unsigned normalized) */
    glGenTextures(1, &texture_id);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glamor_priv->suppress_gl_out_of_memory_logging = true;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, byte_stride, h,
                                0, GL_RED, GL_UNSIGNED_BYTE, bits);
    glamor_priv->suppress_gl_out_of_memory_logging = false;
    if (glGetError() == GL_OUT_OF_MEMORY)
        goto bail_tex;

    /* Set bitmap_size uniform for the fragment shader */
    GLint loc = glGetUniformLocation(prog->prog, "bitmap_size");
    if (loc >= 0)
        glUniform2f(loc, (float)byte_stride, (float)h);

    /* Set up vertex buffers for the 4 quad vertices */
    v = glamor_get_vbo_space(screen, (4 * 6 * sizeof(GLshort)), &vbo_offset);

    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);
    glVertexAttribPointer(GLAMOR_VERTEX_POS, 4, GL_SHORT, GL_FALSE,
                          6 * sizeof(GLshort), vbo_offset);

    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_SHORT, GL_FALSE,
                          6 * sizeof(GLshort), vbo_offset + 4 * sizeof(GLshort));

    for (int i = 0; i < 4; i++) {
        v[i * 6 + 0] = x;
        v[i * 6 + 1] = y;
        v[i * 6 + 2] = w;
        v[i * 6 + 3] = h;
        v[i * 6 + 4] = leftPad;
        v[i * 6 + 5] = 0;
    }

    glamor_put_vbo_space(screen);
    glEnable(GL_SCISSOR_TEST);

    glamor_pixmap_loop(pixmap_priv, box_index) {
        BoxPtr box = RegionRects(gc->pCompositeClip);
        int nbox = RegionNumRects(gc->pCompositeClip);

        glamor_set_destination_drawable(drawable, box_index, TRUE, FALSE,
                                        prog->matrix_uniform,
                                        &off_x, &off_y);

        while (nbox--) {
            glScissor(box->x1 + off_x,
                      box->y1 + off_y,
                      box->x2 - box->x1,
                      box->y2 - box->y1);
            box++;
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    glDisable(GL_SCISSOR_TEST);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);

    ret = TRUE;
bail_tex:
    glDeleteTextures(1, &texture_id);
bail:
    return ret;
}

/*
 * XYPixmap PutImage
 *
 * Creates a CPU-only pixmap, lets fbPutImage handle the format
 * conversion, then GPU-accelerated CopyArea to the destination.
 */

static Bool
glamor_put_image_xy_gl(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                       int w, int h, int leftPad, int format, char *bits)
{
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    ScreenPtr   screen = drawable->pScreen;
    PixmapPtr   temp_pixmap = NULL;
    DrawablePtr temp_drawable;
    GCPtr       temp_gc;
    Bool        ret = FALSE;
    ChangeGCVal gcv[3];

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return FALSE;

    temp_pixmap = (*screen->CreatePixmap)(screen, w, h, drawable->depth,
                                          GLAMOR_CREATE_PIXMAP_CPU);
    if (!temp_pixmap)
        goto bail;

    temp_drawable = &temp_pixmap->drawable;
    temp_gc = GetScratchGC(temp_drawable->depth, screen);
    if (!temp_gc)
        goto bail_pixmap;

    /* copy mode for the first plane to clear all of the other bits */
    gcv[0].val = GXcopy;
    gcv[1].val = gc->fgPixel;
    gcv[2].val = gc->bgPixel;

    ChangeGC(NullClient, temp_gc, GCFunction | GCForeground | GCBackground, gcv);
    ValidateGC(temp_drawable, temp_gc);

    (*temp_gc->ops->PutImage)(temp_drawable, temp_gc, depth, 0, 0, w, h, leftPad, format, bits);
    (*gc->ops->CopyArea)(&temp_pixmap->drawable, drawable, gc, 0, 0, w, h, x, y);

    ret = TRUE;

    FreeScratchGC(temp_gc);
bail_pixmap:
    (*screen->DestroyPixmap)(temp_pixmap);
bail:
    return ret;
}

static void
glamor_put_image_bail(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                      int w, int h, int leftPad, int format, char *bits)
{
    if (glamor_prepare_access_box(drawable, GLAMOR_ACCESS_RW, x, y, w, h))
        fbPutImage(drawable, gc, depth, x, y, w, h, leftPad, format, bits);
    glamor_finish_access(drawable);
}

void
glamor_put_image(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                 int w, int h, int leftPad, int format, char *bits)
{
    switch (format) {
    case ZPixmap:
        if (glamor_put_image_zpixmap_gl(drawable, gc, depth, x, y, w, h, leftPad, format, bits))
            return;
        break;
    case XYPixmap:
        if (glamor_put_image_xy_gl(drawable, gc, depth, x, y, w, h, leftPad, format, bits))
            return;
        break;
    case XYBitmap:
        if (w * h >= 100 * 100) {
            if (glamor_put_image_xybitmap_gl(drawable, gc, x, y, w, h, leftPad, bits))
                return;
        }
        if (glamor_put_image_xy_gl(drawable, gc, depth, x, y, w, h, leftPad, format, bits))
            return;
        break;
    }
    glamor_put_image_bail(drawable, gc, depth, x, y, w, h, leftPad, format, bits);
}

static Bool
glamor_get_image_gl(DrawablePtr drawable, int x, int y, int w, int h,
                    unsigned int format, unsigned long plane_mask, char *d)
{
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv;
    uint32_t    byte_stride = PixmapBytePad(w, drawable->depth);
    BoxRec      box;
    int         off_x, off_y;

    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        goto bail;

    if (format != ZPixmap)
        goto bail;

    glamor_get_drawable_deltas(drawable, pixmap, &off_x, &off_y);
    box.x1 = x;
    box.x2 = x + w;
    box.y1 = y;
    box.y2 = y + h;
    glamor_download_boxes(drawable, &box, 1,
                          drawable->x + off_x, drawable->y + off_y,
                          -x, -y,
                          (uint8_t *) d, byte_stride);

    if (!glamor_pm_is_solid(glamor_drawable_effective_depth(drawable), plane_mask)) {
        FbStip pm = fbReplicatePixel(plane_mask, drawable->bitsPerPixel);
        FbStip *dst = (void *)d;
        uint32_t dstStride = byte_stride / sizeof(FbStip);

        for (int i = 0; i < dstStride * h; i++)
            dst[i] &= pm;
    }

    return TRUE;
bail:
    return FALSE;
}

static void
glamor_get_image_bail(DrawablePtr drawable, int x, int y, int w, int h,
                      unsigned int format, unsigned long plane_mask, char *d)
{
    if (glamor_prepare_access_box(drawable, GLAMOR_ACCESS_RO, x, y, w, h))
        fbGetImage(drawable, x, y, w, h, format, plane_mask, d);
    glamor_finish_access(drawable);
}

void
glamor_get_image(DrawablePtr drawable, int x, int y, int w, int h,
                 unsigned int format, unsigned long plane_mask, char *d)
{
    if (glamor_get_image_gl(drawable, x, y, w, h, format, plane_mask, d))
        return;
    glamor_get_image_bail(drawable, x, y, w, h, format, plane_mask, d);
}
