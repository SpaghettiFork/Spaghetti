/*
 * Copyright © 2014 Keith Packard
 * Copyright @ 2022 Raspberry Pi Ltd
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
 *
 * Authors:
 *    Christopher Michael <cmichael@igalia.com>
 */

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include "xf86.h"
#include "driver.h"
#include "dri3.h"
#ifdef XSYNC
# include "misync.h"
# ifdef HAVE_XSHMFENCE
#  include "misyncshm.h"
# endif
# include "misyncstr.h"
# include <epoxy/gl.h>
#endif

#if XSYNC
static DevPrivateKeyRec dri3_sync_fence_key;

struct dri3_sync_fence {
   SyncFenceSetTriggeredFunc set_triggered;
};

static inline struct dri3_sync_fence *
ms_dri3_get_sync_fence(SyncFence *fence)
{
   return (struct dri3_sync_fence *)dixLookupPrivate(&fence->devPrivates, &dri3_sync_fence_key);
}

static void
ms_dri3_sync_fence_set_triggered(SyncFence *fence)
{
   struct dri3_sync_fence *dri3_fence = ms_dri3_get_sync_fence(fence);

   fence->funcs.SetTriggered = dri3_fence->set_triggered;
   fence->funcs.SetTriggered(fence);
   dri3_fence->set_triggered = fence->funcs.SetTriggered;
   fence->funcs.SetTriggered = ms_dri3_sync_fence_set_triggered;
}

static void
ms_dri3_sync_create_fence(ScreenPtr screen, SyncFence *fence, Bool triggered)
{
   ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
   modesettingPtr ms = modesettingPTR(scrn);
   SyncScreenFuncsPtr scrn_funcs = miSyncGetScreenFuncs(screen);
   struct dri3_sync_fence *dri3_fence = ms_dri3_get_sync_fence(fence);

   scrn_funcs->CreateFence = ms->drmmode.sync_funcs.CreateFence;
   scrn_funcs->CreateFence(screen, fence, triggered);
   ms->drmmode.sync_funcs.CreateFence = scrn_funcs->CreateFence;
   scrn_funcs->CreateFence = ms_dri3_sync_create_fence;

   dri3_fence->set_triggered = fence->funcs.SetTriggered;
   fence->funcs.SetTriggered = ms_dri3_sync_fence_set_triggered;
}
#endif

Bool
ms_dri3_sync_init(ScreenPtr screen)
{
#if XSYNC
   ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
   modesettingPtr ms = modesettingPTR(scrn);
   SyncScreenFuncsPtr scrn_funcs;

   if (!dixPrivateKeyRegistered(&dri3_sync_fence_key)) {
      if (!dixRegisterPrivateKey(&dri3_sync_fence_key, PRIVATE_SYNC_FENCE,
                                 sizeof(struct dri3_sync_fence)))
        return FALSE;
   }

#ifdef HAVE_XSHMFENCE
   if (!miSyncShmScreenInit(screen))
     return FALSE;
#else
   if (!miSyncSetup(screen))
     return FALSE;
#endif

   scrn_funcs = miSyncGetScreenFuncs(screen);
   ms->drmmode.sync_funcs.CreateFence = scrn_funcs->CreateFence;
   scrn_funcs->CreateFence = ms_dri3_sync_create_fence;
#endif

   return TRUE;
}

void
ms_dri3_sync_close(ScreenPtr screen)
{
#if XSYNC
   ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
   modesettingPtr ms = modesettingPTR(scrn);
   SyncScreenFuncsPtr scrn_funcs = miSyncGetScreenFuncs(screen);

   if (scrn_funcs)
     scrn_funcs->CreateFence = ms->drmmode.sync_funcs.CreateFence;
#endif
}