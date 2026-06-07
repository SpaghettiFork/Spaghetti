/**
 * Spaghetti Display Server
 * Copyright (C) 2026  SpaghettiFork
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
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "compositx.h"
#include "picturestr.h"
#include "dixstruct.h"

static PictFormatPtr
compositXFormatForVisual(ScreenPtr pScreen, int depth, VisualID vid)
{
    for (int i = 0; i < pScreen->numVisuals; i++) {
        if (pScreen->visuals[i].vid == vid)
            return PictureMatchVisual(pScreen, depth, &pScreen->visuals[i]);
    }

    return NULL;
}

static PicturePtr
compositXSolidAlpha(CARD16 alpha)
{
    xRenderColor color = { 0, 0, 0, alpha };
    int error;
    return CreateSolidPicture(0, &color, &error);
}

void
compositXInitOverlay(ScreenPtr pScreen)
{
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    PictFormatPtr pFormat;
    int error;

    if (!cxs->pOverlay)
        return;

    pFormat = compositXFormatForVisual(pScreen, cxs->pOverlay->drawable.depth,
                                       wVisual(cxs->pOverlay));
    if (!pFormat)
        return;

    cxs->pOverlayPicture = CreatePicture(0, &cxs->pOverlay->drawable, pFormat,
                                         0, NULL, serverClient, &error);
}

void
compositXRepaint(ScreenPtr pScreen)
{
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    PicturePtr pClear, pSrc, pMask;
    PictFormatPtr pFormat;
    PixmapPtr pPixmap;
    CompXWindowPtr cxw;
    WindowPtr pChild;
    int error;

    if (!cxs->pOverlayPicture)
        return;

    pClear = compositXSolidAlpha(0);
    CompositePicture(PictOpSrc, pClear, NULL, cxs->pOverlayPicture,
                     0, 0, 0, 0, 0, 0, pScreen->width, pScreen->height);
    FreePicture(pClear, 0);

    for (pChild = pScreen->root->lastChild; pChild; pChild = pChild->prevSib) {
        cxw = GetCompXWindow(pChild);

        if (!pChild->mapped || !GetCompWindow(pChild) || cxw->bypass)
            continue;

        pPixmap = (*pScreen->GetWindowPixmap)(pChild);
        if (!pPixmap)
            continue;

        pFormat = compositXFormatForVisual(pScreen, pChild->drawable.depth,
                                           wVisual(pChild));
        if (!pFormat)
            continue;

        pSrc = CreatePicture(0, &pPixmap->drawable, pFormat,
                             0, NULL, serverClient, &error);
        if (!pSrc)
            continue;

        pMask = (cxw->opacity != COMPOSITX_OPAQUE)
            ? compositXSolidAlpha((CARD16)(cxw->opacity >> 16))
            : NULL;

        CompositePicture(PictOpOver,
                         pSrc, pMask, cxs->pOverlayPicture,
                         0, 0, 0, 0,
                         pChild->drawable.x, pChild->drawable.y,
                         pChild->drawable.width, pChild->drawable.height);

        FreePicture(pSrc, 0);
        if (pMask)
            FreePicture(pMask, 0);
    }
}