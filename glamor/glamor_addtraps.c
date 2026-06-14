/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
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
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include "glamor_priv.h"
#include "mipict.h"
#include "fbpict.h"

void
glamor_add_traps(PicturePtr pPicture,
                 INT16 x_off, INT16 y_off, int ntrap, xTrap *traps)
{
    ScreenPtr screen = pPicture->pDrawable->pScreen;
    struct glamor_pixmap_private *priv;
    PixmapPtr pixmap;
    BoxRec bounds;
    PicturePtr picture;
    pixman_image_t *image = NULL;
    PictFormatPtr mask_format;
    int i;
    int width, height, stride;

    if (ntrap <= 0)
        return;

    pixmap = glamor_get_drawable_pixmap(pPicture->pDrawable);
    priv = glamor_get_pixmap_private(pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(priv))
        goto fallback;

    if (pPicture->alphaMap)
        goto fallback;

    if (pPicture->polyEdge == PolyEdgeSharp)
        mask_format = PictureMatchFormat(screen, 1, PICT_a1);
    else
        mask_format = PictureMatchFormat(screen, 8, PICT_a8);

    bounds.x1 = bounds.y1 = MAXSHORT;
    bounds.x2 = bounds.y2 = MINSHORT;
    for (i = 0; i < ntrap; i++) {
        INT16 top_y = traps[i].top.y >> 16;
        INT16 bot_y = traps[i].bot.y >> 16;
        INT16 left_x1 = traps[i].top.l >> 16;
        INT16 left_x2 = traps[i].bot.l >> 16;
        INT16 right_x1 = traps[i].top.r >> 16;
        INT16 right_x2 = traps[i].bot.r >> 16;
        INT16 min_x, max_x;

        if (top_y < bounds.y1) bounds.y1 = top_y;
        if (bot_y > bounds.y2) bounds.y2 = bot_y;

        min_x = min(left_x1, left_x2);
        max_x = max(right_x1, right_x2);

        if (min_x < bounds.x1) bounds.x1 = min_x;
        if (max_x > bounds.x2) bounds.x2 = max_x;
    }

    if (bounds.y1 >= bounds.y2 || bounds.x1 >= bounds.x2)
        return;

    width = bounds.x2 - bounds.x1;
    height = bounds.y2 - bounds.y1;
    stride = PixmapBytePad(width, mask_format->depth);

    picture = glamor_create_mask_picture(screen, pPicture,
                                         mask_format, width, height);
    if (!picture)
        goto fallback;

    image = pixman_image_create_bits(picture->format,
                                     width, height, NULL, stride);
    if (!image) {
        FreePicture(picture, 0);
        goto fallback;
    }

    pixman_add_traps(image, -bounds.x1, -bounds.y1,
                     ntrap, (pixman_trap_t *)traps);

    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);
    screen->ModifyPixmapHeader(pixmap, width, height,
                               mask_format->depth,
                               BitsPerPixel(mask_format->depth),
                               PixmapBytePad(width, mask_format->depth),
                               pixman_image_get_data(image));

    CompositePicture(PictOpAdd, picture, NULL, pPicture,
                     0, 0, 0, 0,
                     bounds.x1 + x_off, bounds.y1 + y_off,
                     width, height);

    pixman_image_unref(image);
    FreePicture(picture, 0);
    return;

 fallback:
    if (glamor_prepare_access_picture(pPicture, GLAMOR_ACCESS_RW))
        fbAddTraps(pPicture, x_off, y_off, ntrap, traps);
    glamor_finish_access_picture(pPicture);
}
