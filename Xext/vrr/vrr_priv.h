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

#ifndef _VRR_PRIV_H_
#define _VRR_PRIV_H_

#include "vrr.h"

#include "dixstruct.h"
#include "property.h"
#include "callback.h"

#define VRR_NAME "SPAGHETTI-VRR"

typedef struct _vrr_window_priv {
    Bool variable_refresh;
} vrr_window_priv_rec, *vrr_window_priv_ptr;

extern DevPrivateKeyRec vrr_screen_private_key;
extern DevPrivateKeyRec vrr_window_private_key;

typedef struct _vrr_screen_priv {
    CloseScreenProcPtr          CloseScreen;

    vrr_screen_info_rec         info;
    Atom                        vrr_atom;
    Bool                        is_vrr_capable;
    WindowPtr                   flip_window;
} vrr_screen_priv_rec, *vrr_screen_priv_ptr;

#define vrr_wrap(priv,real,mem,func) {\
    priv->mem = real->mem; \
    real->mem = func; \
}

#define vrr_unwrap(priv,real,mem) {\
    real->mem = priv->mem; \
}

static inline vrr_screen_priv_ptr
vrr_screen_priv(ScreenPtr screen)
{
    return (vrr_screen_priv_ptr)dixLookupPrivate(&(screen)->devPrivates,
                                                  &vrr_screen_private_key);
}

static inline vrr_window_priv_ptr
vrr_window_priv(WindowPtr window)
{
    return (vrr_window_priv_ptr)dixLookupPrivate(&(window)->devPrivates,
                                                  &vrr_window_private_key);
}

#endif /* _VRR_PRIV_H_ */
