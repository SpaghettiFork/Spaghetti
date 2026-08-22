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
#ifndef SOFT2D_H
#define SOFT2D_H

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <X11/X.h>
#include <stdint.h>
#include "screenint.h"
#include "dri3.h"

struct gbm_device;
struct gbm_bo;

_X_EXPORT Bool
soft2d_init(ScreenPtr screen, struct gbm_device *gbm, int fd);

_X_EXPORT int
soft2d_shareable_fd_from_pixmap(ScreenPtr screen, PixmapPtr pixmap,
                                CARD16 *stride, CARD32 *size);

_X_EXPORT Bool
soft2d_back_pixmap_from_fd(PixmapPtr pixmap, int fd,
                           CARD16 width, CARD16 height,
                           CARD16 stride, CARD8 depth, CARD8 bpp);

_X_EXPORT Bool
soft2d_pixmap_from_gbm_bo(PixmapPtr pixmap, struct gbm_bo *bo);

_X_EXPORT struct gbm_bo *
soft2d_gbm_bo_from_pixmap(ScreenPtr screen, PixmapPtr pixmap);

_X_EXPORT PixmapPtr
soft2d_pixmap_from_fds(ScreenPtr screen, CARD8 num,
                       const int *fds,
                       CARD16 width, CARD16 height,
                       const CARD32 *strides,
                       const CARD32 *offsets,
                       CARD8 depth, CARD8 bpp,
                       uint64_t modifier);

_X_EXPORT void
soft2d_sync_close(ScreenPtr screen);

_X_EXPORT void
soft2d_set_drawable_modifiers_func(ScreenPtr screen,
                                   dri3_get_drawable_modifiers_proc func);

_X_EXPORT void
soft2d_set_formats_func(ScreenPtr screen, dri3_get_formats_proc func);

_X_EXPORT void
soft2d_set_modifiers_func(ScreenPtr screen, dri3_get_modifiers_proc func);

#endif /* SOFT2D_H */
