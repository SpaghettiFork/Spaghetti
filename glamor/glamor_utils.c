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

void
glamor_solid_boxes(DrawablePtr drawable,
                   BoxPtr box, int nbox, unsigned long fg_pixel)
{
    GCPtr gc;
    xRectangle *rect;
    int n;

    rect = xallocarray(nbox, sizeof(xRectangle));
    if (_X_UNLIKELY(!rect))
        return;

    for (n = 0; n < nbox; n++) {
        rect[n].x = box[n].x1;
        rect[n].y = box[n].y1;
        rect[n].width = box[n].x2 - box[n].x1;
        rect[n].height = box[n].y2 - box[n].y1;
    }

    gc = GetScratchGC(drawable->depth, drawable->pScreen);
    if (gc) {
        ChangeGCVal vals[1];

        vals[0].val = fg_pixel;
        ChangeGC(NullClient, gc, GCForeground, vals);
        ValidateGC(drawable, gc);
        gc->ops->PolyFillRect(drawable, gc, nbox, rect);
        FreeScratchGC(gc);
    }
    free(rect);
}
