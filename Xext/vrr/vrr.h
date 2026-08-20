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

#ifndef _VRR_H_
#define _VRR_H_

#include "dix-config.h"
#include "scrnintstr.h"
#include "windowstr.h"

#define VRR_API_VERSION 1

typedef Bool (*vrr_check_vrr_capable_proc)(ScreenPtr screen);
typedef void (*vrr_set_screen_vrr_proc)(ScreenPtr screen, Bool enabled);

typedef struct _vrr_screen_info {
    uint32_t                    version;
    vrr_check_vrr_capable_proc  check_vrr_capable;
    vrr_set_screen_vrr_proc     set_screen_vrr;
} vrr_screen_info_rec, *vrr_screen_info_ptr;

extern _X_EXPORT Bool
vrr_screen_init(ScreenPtr screen, const vrr_screen_info_rec *info);

extern _X_EXPORT void
vrr_set_flip_window(ScreenPtr screen, WindowPtr window);

#endif /* _VRR_H_ */