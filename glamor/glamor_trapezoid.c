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

/** @file glamor_trapezoid.c
 *
 * Trapezoid acceleration implementation
 */

#include "glamor_priv.h"

#include "mipict.h"
#include "fbpict.h"

/**
 * Creates an appropriate picture for temp mask use.
 */
PicturePtr
glamor_create_mask_picture(ScreenPtr screen,
                           PicturePtr dst,
                           PictFormatPtr pict_format,
                           CARD16 width, CARD16 height)
{
    PixmapPtr pixmap;
    PicturePtr picture;
    int error;

    if (!pict_format) {
        if (dst->polyEdge == PolyEdgeSharp)
            pict_format = PictureMatchFormat(screen, 1, PICT_a1);
        else
            pict_format = PictureMatchFormat(screen, 8, PICT_a8);
        if (!pict_format)
            return 0;
    }

    pixmap = glamor_create_pixmap(screen, 0, 0,
                                  pict_format->depth,
                                  GLAMOR_CREATE_PIXMAP_CPU);

    if (!pixmap)
        return 0;
    picture = CreatePicture(0, &pixmap->drawable, pict_format,
                            0, 0, serverClient, &error);
    glamor_destroy_pixmap(pixmap);
    return picture;
}

static inline Bool
glamor_trapezoid_origin_fits(int origin, int drawable_offset, int shift)
{
    /* Check the original and shifted origins in both coordinate spaces. */
    return origin + MIN(0, drawable_offset) >= MINSHORT &&
           origin + MAX(0, drawable_offset) + shift <= MAXSHORT;
}

static inline Bool
glamor_clip_trapezoid_mask(PicturePtr src, PicturePtr dst, BoxPtr bounds,
                          int *x_rel, int *y_rel)
{
    DrawablePtr drawable = dst->pDrawable;
    RegionPtr clip = dst->pCompositeClip;

    if (src->alphaMap || dst->alphaMap ||
        !glamor_trapezoid_origin_fits(bounds->x1, drawable->x, 0) ||
        !glamor_trapezoid_origin_fits(bounds->y1, drawable->y, 0))
        return TRUE;

    if (RegionNil(clip))
        return FALSE;

    const BoxRec *extent = RegionExtents(clip);
    int left = MAX(bounds->x1, extent->x1 - drawable->x);
    int top = MAX(bounds->y1, extent->y1 - drawable->y);
    int right = MIN(bounds->x2, extent->x2 - drawable->x);
    int bottom = MIN(bounds->y2, extent->y2 - drawable->y);

    if (left >= right || top >= bottom)
        return FALSE;

    int dx = left - bounds->x1;
    int dy = top - bounds->y1;
    int src_x = src->pDrawable ? src->pDrawable->x : 0;
    int src_y = src->pDrawable ? src->pDrawable->y : 0;

    if (!glamor_trapezoid_origin_fits(*x_rel, src_x, dx) ||
        !glamor_trapezoid_origin_fits(*y_rel, src_y, dy))
        return TRUE;

    *bounds = (BoxRec) {left, top, right, bottom};
    *x_rel += dx;
    *y_rel += dy;
    return TRUE;
}

/**
 * glamor_trapezoids will generate trapezoid mask accumulating in
 * system memory.
 */
void
glamor_trapezoids(CARD8 op,
                  PicturePtr src, PicturePtr dst,
                  PictFormatPtr mask_format, INT16 x_src, INT16 y_src,
                  int ntrap, xTrapezoid *traps)
{
    ScreenPtr screen = dst->pDrawable->pScreen;
    BoxRec bounds;
    PicturePtr picture;
    INT16 x_dst, y_dst;
    int x_rel, y_rel;
    int width, height, stride;
    PixmapPtr pixmap;
    pixman_image_t *image = NULL;

    /* If a mask format wasn't provided, we get to choose, but behavior should
     * be as if there was no temporary mask the traps were accumulated into.
     */
    if (!mask_format) {
        if (dst->polyEdge == PolyEdgeSharp)
            mask_format = PictureMatchFormat(screen, 1, PICT_a1);
        else
            mask_format = PictureMatchFormat(screen, 8, PICT_a8);
        for (; ntrap; ntrap--, traps++)
            glamor_trapezoids(op, src, dst, mask_format, x_src,
                              y_src, 1, traps);
        return;
    }

    miTrapezoidBounds(ntrap, traps, &bounds);

    if (bounds.y1 >= bounds.y2 || bounds.x1 >= bounds.x2)
        return;

    x_dst = traps[0].left.p1.x >> 16;
    y_dst = traps[0].left.p1.y >> 16;
    x_rel = bounds.x1 + x_src - x_dst;
    y_rel = bounds.y1 + y_src - y_dst;

    if (!glamor_clip_trapezoid_mask(src, dst, &bounds, &x_rel, &y_rel))
        return;

    width = bounds.x2 - bounds.x1;
    height = bounds.y2 - bounds.y1;
    stride = PixmapBytePad(width, mask_format->depth);

    picture = glamor_create_mask_picture(screen, dst, mask_format,
                                         width, height);
    if (!picture)
        return;

    image = pixman_image_create_bits(picture->format,
                                     width, height, NULL, stride);
    if (!image) {
        FreePicture(picture, 0);
        return;
    }

    for (; ntrap; ntrap--, traps++)
        pixman_rasterize_trapezoid(image,
                                   (pixman_trapezoid_t *) traps,
                                   -bounds.x1, -bounds.y1);

    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);

    screen->ModifyPixmapHeader(pixmap, width, height,
                               mask_format->depth,
                               BitsPerPixel(mask_format->depth),
                               PixmapBytePad(width,
                                             mask_format->depth),
                               pixman_image_get_data(image));

    CompositePicture(op, src, picture, dst,
                     x_rel, y_rel,
                     0, 0,
                     bounds.x1, bounds.y1,
                     bounds.x2 - bounds.x1, bounds.y2 - bounds.y1);

    if (image)
        pixman_image_unref(image);

    FreePicture(picture, 0);
}
