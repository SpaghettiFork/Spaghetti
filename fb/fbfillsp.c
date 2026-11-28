/*
 * Copyright © 1998 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "fb.h"

void
fbFillSpans(DrawablePtr pDrawable,
            GCPtr pGC, int n, DDXPointPtr ppt, int *pwidth, int fSorted)
{
    RegionPtr pClip = fbGetCompositeClip(pGC);
    BoxPtr pextent;
    BoxPtr first = RegionRects(pClip);
    int nbox = RegionNumRects(pClip);
    BoxPtr end = first + nbox;
    int extentX1, extentX2, extentY1, extentY2;
    int fullX1, fullX2, fullY1;
    int partX1, partX2;

    pextent = RegionExtents(pClip);
    extentX1 = pextent->x1;
    extentY1 = pextent->y1;
    extentX2 = pextent->x2;
    extentY2 = pextent->y2;
    while (n--) {
        fullX1 = ppt->x;
        fullY1 = ppt->y;
        fullX2 = fullX1 + (int) *pwidth;
        ppt++;
        pwidth++;

        if (fullY1 < extentY1 || extentY2 <= fullY1)
            continue;

        if (fullX1 < extentX1)
            fullX1 = extentX1;

        if (fullX2 > extentX2)
            fullX2 = extentX2;

        if (fullX1 >= fullX2)
            continue;

        if (nbox == 1) {
            fbFill(pDrawable, pGC, fullX1, fullY1, fullX2 - fullX1, 1);
        }
        else {
            /* Sorted spans cannot intersect clip bands we have passed. */
            if (fSorted)
                while (first != end && first->y2 <= fullY1)
                    first++;

            for (BoxPtr pbox = first; pbox != end && pbox->y1 <= fullY1; pbox++) {
                if (fullY1 >= pbox->y2)
                    continue;

                partX1 = pbox->x1 > fullX1 ? pbox->x1 : fullX1;
                partX2 = pbox->x2 < fullX2 ? pbox->x2 : fullX2;
                if (partX2 > partX1)
                    fbFill(pDrawable, pGC,
                           partX1, fullY1, partX2 - partX1, 1);
            }
        }
    }
}
