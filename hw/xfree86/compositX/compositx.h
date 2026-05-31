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
#ifndef COMPOSITX_H
#define COMPOSITX_H

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include "scrnintstr.h"
#include "windowstr.h"
#include "damage.h"
#include "compint.h"
#include "picturestr.h"

/* sentinel for windows without _NET_WM_WINDOW_OPACITY */
#define COMPOSITX_OPAQUE 0xffffffffU

typedef struct _CompXScreen {
    CloseScreenProcPtr      CloseScreen;
    CreateWindowProcPtr     CreateWindow;
    DestroyWindowProcPtr    DestroyWindow;
    RealizeWindowProcPtr    RealizeWindow;
    UnrealizeWindowProcPtr  UnrealizeWindow;

    Bool        wrapped;            /* guard against double-wrapping */

    WindowPtr   pOverlay;
    PicturePtr  pOverlayPicture;    /* cached destination; freed in CloseScreen */
    RegionRec   pendingDamage;      /* accumulated dirty region, drained in block handler */
} CompXScreenRec, *CompXScreenPtr;

typedef struct _CompXWindow {
    CARD32      opacity;    /* _NET_WM_WINDOW_OPACITY value; COMPOSITX_OPAQUE if absent */
    Bool        bypass;     /* _NET_WM_BYPASS_COMPOSITOR == 1 */
    DamagePtr   pDamage;
} CompXWindowRec, *CompXWindowPtr;

extern DevPrivateKeyRec compositXScreenPrivateKeyRec;
extern DevPrivateKeyRec compositXWindowPrivateKeyRec;

#define GetCompXScreen(s) \
    ((CompXScreenPtr) dixLookupPrivate(&(s)->devPrivates, \
                                       &compositXScreenPrivateKeyRec))
#define GetCompXWindow(w) \
    ((CompXWindowPtr) dixLookupPrivate(&(w)->devPrivates, \
                                       &compositXWindowPrivateKeyRec))

#define CompXWrap(priv, real, field, func) do { \
    (priv)->field = (real)->field;              \
    (real)->field = (func);                     \
} while (0)

#define CompXUnwrap(priv, real, field) \
    ((real)->field = (priv)->field)

void compositXInitOverlay(ScreenPtr pScreen);
void compositXRepaint(ScreenPtr pScreen);

#endif /* COMPOSITX_H */