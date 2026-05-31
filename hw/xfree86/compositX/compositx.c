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
#include "dixstruct.h"
#include "privates.h"
#include "propertyst.h"
#include "xf86Module.h"

DevPrivateKeyRec compositXScreenPrivateKeyRec;
DevPrivateKeyRec compositXWindowPrivateKeyRec;

static Atom compositXOpacityAtom;
static Atom compositXBypassAtom;

static MODULESETUPPROTO(compositXSetup);

static Bool
compositXIsBypassed(WindowPtr pWindow)
{
    PropertyPtr pProp;

    if (dixLookupProperty(&pProp, pWindow, compositXBypassAtom,
                          serverClient, DixReadAccess) != Success)
        return FALSE;

    return pProp->format == 32 && pProp->size >= 1 &&
           *(CARD32 *) pProp->data == 1;
}

static void
compositXDamageReport(DamagePtr pDamage, RegionPtr pRegion, void *closure)
{
    WindowPtr pWindow = closure;
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    CompXScreenPtr cxs = GetCompXScreen(pScreen);

    RegionUnion(&cxs->pendingDamage, &cxs->pendingDamage, pRegion);
}

static void
compositXWakeupHandler(void *data, int result)
{
}

static void
compositXBlockHandler(void *data, void *pTimeout)
{
    ScreenPtr pScreen = data;
    CompXScreenPtr cxs = GetCompXScreen(pScreen);

    if (!RegionNotEmpty(&cxs->pendingDamage))
        return;

    compositXRepaint(pScreen);
    RegionEmpty(&cxs->pendingDamage);
}

static Bool
compositXCloseScreen(ScreenPtr pScreen)
{
    CompXScreenPtr cxs = GetCompXScreen(pScreen);

    RemoveBlockAndWakeupHandlers(compositXBlockHandler,
                                 compositXWakeupHandler, pScreen);
    RegionUninit(&cxs->pendingDamage);

    if (cxs->pOverlayPicture) {
        FreePicture(cxs->pOverlayPicture, 0);
        cxs->pOverlayPicture = NULL;
    }

    if (cxs->pOverlay) {
        compDestroyOverlayWindow(pScreen);
        cxs->pOverlay = NULL;
    }

    CompXUnwrap(cxs, pScreen, CloseScreen);
    CompXUnwrap(cxs, pScreen, CreateWindow);
    CompXUnwrap(cxs, pScreen, DestroyWindow);
    CompXUnwrap(cxs, pScreen, RealizeWindow);
    CompXUnwrap(cxs, pScreen, UnrealizeWindow);

    return (*pScreen->CloseScreen)(pScreen);
}

static Bool
compositXCreateWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    CompXWindowPtr cxw = GetCompXWindow(pWindow);
    Bool ret;

    CompXUnwrap(cxs, pScreen, CreateWindow);
    ret = (*pScreen->CreateWindow)(pWindow);
    CompXWrap(cxs, pScreen, CreateWindow, compositXCreateWindow);

    if (ret) {
        cxw->opacity = COMPOSITX_OPAQUE;
        cxw->bypass  = FALSE;
        cxw->pDamage = NULL;
    }

    return ret;
}

static Bool
compositXDestroyWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen  = pWindow->drawable.pScreen;;
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    CompXWindowPtr cxw = GetCompXWindow(pWindow);

    CompXUnwrap(cxs, pScreen, DestroyWindow);
    (*pScreen->DestroyWindow)(pWindow);
    CompXWrap(cxs, pScreen, DestroyWindow, compositXDestroyWindow);

    /* damage may still be live if the window is destroyed while mapped */
    if (cxw->pDamage) {
        DamageDestroy(cxw->pDamage);
        cxw->pDamage = NULL;
    }

    return TRUE;
}

static Bool
compositXRealizeWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    CompXWindowPtr cxw = GetCompXWindow(pWindow);
    Bool ret;

    CompXUnwrap(cxs, pScreen, RealizeWindow);
    ret = (*pScreen->RealizeWindow)(pWindow);
    CompXWrap(cxs, pScreen, RealizeWindow, compositXRealizeWindow);

    if (ret && !cxw->pDamage) {
        cxw->bypass = compositXIsBypassed(pWindow);
        if (cxw->bypass) {
            compUnredirectWindow(serverClient, pWindow, CompositeRedirectManual);
        } else {
            cxw->pDamage = DamageCreate(compositXDamageReport,
                                        NULL,
                                        DamageReportRawRegion,
                                        TRUE,
                                        pScreen,
                                        pWindow);
            if (cxw->pDamage)
                DamageRegister(&pWindow->drawable, cxw->pDamage);
        }
    }

    return ret;
}

static Bool
compositXUnrealizeWindow(WindowPtr pWindow)
{
    ScreenPtr pScreen = pWindow->drawable.pScreen;
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    CompXWindowPtr cxw = GetCompXWindow(pWindow);
    Bool ret;

    if (cxw->bypass) {
        compRedirectWindow(serverClient, pWindow, CompositeRedirectManual);
        cxw->bypass = FALSE;
    }

    CompXUnwrap(cxs, pScreen, UnrealizeWindow);
    ret = (*pScreen->UnrealizeWindow)(pWindow);
    CompXWrap(cxs, pScreen, UnrealizeWindow, compositXUnrealizeWindow);

    return ret;
}

static void
compositXPropertyCallback(CallbackListPtr *pcbl, void *unused, void *data)
{
    PropertyStateRec *rec = data;
    CompXWindowPtr cxw = GetCompXWindow(rec->win);
    Bool bypass;

    if (rec->prop->propertyName == compositXOpacityAtom) {
        if (rec->state == PropertyNewValue && rec->prop->size >= 1)
            cxw->opacity = *(CARD32 *) rec->prop->data;
        else
            cxw->opacity = COMPOSITX_OPAQUE;
        return;
    }

    if (rec->prop->propertyName == compositXBypassAtom) {
        bypass = (rec->state == PropertyNewValue &&
                  rec->prop->size >= 1 &&
                  *(CARD32 *) rec->prop->data == 1);

        if (bypass == cxw->bypass || !rec->win->mapped)
            return;

        cxw->bypass = bypass;

        if (bypass) {
            if (cxw->pDamage) {
                DamageUnregister(cxw->pDamage);
                DamageDestroy(cxw->pDamage);
                cxw->pDamage = NULL;
            }
            compUnredirectWindow(serverClient, rec->win, CompositeRedirectManual);
        } else {
            ScreenPtr pScreen = rec->win->drawable.pScreen;

            compRedirectWindow(serverClient, rec->win, CompositeRedirectManual);
            cxw->pDamage = DamageCreate(compositXDamageReport, NULL,
                                        DamageReportRawRegion, TRUE,
                                        pScreen, rec->win);
            if (cxw->pDamage)
                DamageRegister(&rec->win->drawable, cxw->pDamage);
        }
    }
}

static void
compositXScreenSetup(ScreenPtr pScreen)
{
    CompXScreenPtr cxs = GetCompXScreen(pScreen);
    CompScreenPtr cs = GetCompScreen(pScreen);
    BoxRec screenBox = {0, 0, pScreen->width, pScreen->height};

    if (!cs)
        return;

    if (cs->pOverlayWin == NULL)
        if (!compCreateOverlayWindow(pScreen))
            return;

    if (cxs->wrapped)
        return;

    cxs->wrapped = TRUE;
    cxs->pOverlay = cs->pOverlayWin;
    compositXInitOverlay(pScreen);

    /* manual: the Composite extension will not auto-paint redirected windows */
    compRedirectSubwindows(serverClient, pScreen->root, CompositeRedirectManual);

    RegionInit(&cxs->pendingDamage, &screenBox, 1);
    RegisterBlockAndWakeupHandlers(compositXBlockHandler,
                                   compositXWakeupHandler, pScreen);

    CompXWrap(cxs, pScreen, CloseScreen,     compositXCloseScreen);
    CompXWrap(cxs, pScreen, CreateWindow,    compositXCreateWindow);
    CompXWrap(cxs, pScreen, DestroyWindow,   compositXDestroyWindow);
    CompXWrap(cxs, pScreen, RealizeWindow,   compositXRealizeWindow);
    CompXWrap(cxs, pScreen, UnrealizeWindow, compositXUnrealizeWindow);
}

static void
compositXInitBlockHandler(void *data, void *pTimeout)
{
    RemoveBlockAndWakeupHandlers(compositXInitBlockHandler,
                                 compositXWakeupHandler, NULL);

    for (int i = 0; i < screenInfo.numScreens; i++)
        compositXScreenSetup(screenInfo.screens[i]);
}

static void*
compositXSetup(void* module, void* opts, int *errmaj, int *errmin)
{
    static Bool initialized = FALSE;

    if (initialized) {
        if (errmaj) *errmaj = LDR_ONCEONLY;
        return NULL;
    }
    initialized = TRUE;

    if (!dixRegisterPrivateKey(&compositXScreenPrivateKeyRec,
                               PRIVATE_SCREEN,
                               sizeof(CompXScreenRec)) ||
        !dixRegisterPrivateKey(&compositXWindowPrivateKeyRec,
                               PRIVATE_WINDOW,
                               sizeof(CompXWindowRec))) {
        if (errmaj) *errmaj = LDR_NOMEM;
        return NULL;
    }

    compositXOpacityAtom = MakeAtom("_NET_WM_WINDOW_OPACITY",
                                     sizeof("_NET_WM_WINDOW_OPACITY") - 1,
                                     TRUE);
    compositXBypassAtom  = MakeAtom("_NET_WM_BYPASS_COMPOSITOR",
                                     sizeof("_NET_WM_BYPASS_COMPOSITOR") - 1,
                                     TRUE);

    RegisterBlockAndWakeupHandlers(compositXInitBlockHandler,
                                   compositXWakeupHandler, NULL);
    AddCallback(&PropertyStateCallback, compositXPropertyCallback, NULL);

    return module;
}

static XF86ModuleVersionInfo compositXVersRec = {
    "compositX",
    "Spaghetti Fork",
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    1, 0, 0,
    ABI_CLASS_EXTENSION,
    ABI_EXTENSION_VERSION,
    MOD_CLASS_EXTENSION,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData compositxModuleData = {
    &compositXVersRec,
    compositXSetup,
    NULL
};