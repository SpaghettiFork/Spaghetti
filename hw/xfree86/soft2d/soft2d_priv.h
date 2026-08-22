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
#ifndef SOFT2D_PRIV_H
#define SOFT2D_PRIV_H

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <errno.h>
#include "xf86.h"
#include "xf86Crtc.h"
#include "xf86drm.h"
#include <drm.h>
#include "dri3.h"
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <gbm.h>
#include "fb.h"
#include "gcstruct.h"
#include "picturestr.h"
#include "os/xserver_poll.h"
#include <poll.h>

#include "soft2d.h"

#ifdef XSYNC
#include "misync.h"
#ifdef HAVE_XSHMFENCE
#include "misyncshm.h"
#endif
#endif

struct soft2d_pixmap_priv {
    struct gbm_bo *bo;
    size_t bo_map_size;
    void *bo_map;
    Bool use_modifiers;
};

struct soft2d_screen_priv {
    struct gbm_device *gbm;
    int fd;

    dri3_get_formats_proc            get_formats;
    dri3_get_modifiers_proc          get_modifiers;
    dri3_get_drawable_modifiers_proc get_drawable_modifiers;

    CreateGCProcPtr      saved_CreateGC;
    DestroyPixmapProcPtr saved_DestroyPixmap;
    CloseScreenProcPtr   saved_CloseScreen;
    CloseScreenProcPtr   saved_GCCloseScreen;
    GetImageProcPtr      saved_GetImage;
    CompositeProcPtr     saved_Composite;
#ifdef XSYNC
    SyncScreenFuncsRec   saved_sync_funcs;
#endif
};

extern DevPrivateKeyRec soft2d_pixmap_private_key;
extern DevPrivateKeyRec soft2d_screen_private_key;

#define soft2dGetPixmapPriv(p) \
    ((struct soft2d_pixmap_priv *)dixGetPrivateAddr(&(p)->devPrivates, &soft2d_pixmap_private_key))
#define soft2dGetScreenPriv(s) \
    ((struct soft2d_screen_priv *)dixLookupPrivate(&(s)->devPrivates, &soft2d_screen_private_key))

void
soft2d_wait_on_pixmap(PixmapPtr pixmap);

Bool
soft2d_init_gc(ScreenPtr screen);

Bool
soft2d_screen_init(ScreenPtr screen);

Bool
soft2d_sync_init(ScreenPtr screen, struct soft2d_screen_priv *spriv);

Bool
soft2d_destroy_pixmap(PixmapPtr pixmap);

int
soft2d_pixmap_name_get(PixmapPtr pixmap, CARD16 *stride, CARD32 *size);

void
soft2d_buffers_exchange(PixmapPtr front, PixmapPtr back);

#endif /* SOFT2D_PRIV_H */
