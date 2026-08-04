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

#include "vrr_priv.h"

#include <X11/X.h>
#include "extnsionst.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "property.h"
#include "propertyst.h"
#include "callback.h"
#include "opaque.h"

DevPrivateKeyRec vrr_screen_private_key;
DevPrivateKeyRec vrr_window_private_key;

static Bool vrr_callback_registered;

static Bool
vrr_close_screen(ScreenPtr screen)
{
    vrr_screen_priv_ptr screen_priv = vrr_screen_priv(screen);

    unwrap(screen_priv, screen, CloseScreen);

    free(screen_priv);

    return (*screen->CloseScreen)(screen);
}

static void
vrr_property_callback(CallbackListPtr *cbl, void *unused, void *call_data)
{
    PropertyStateRec *rec = call_data;
    ScreenPtr screen = rec->win->drawable.pScreen;
    vrr_screen_priv_ptr screen_priv;
    vrr_window_priv_ptr win_priv;

    if (rec->state != PropertyNewValue && rec->state != PropertyDelete)
        return;

    screen_priv = vrr_screen_priv(screen);
    if (!screen_priv)
        return;

    if (rec->prop->propertyName != screen_priv->vrr_atom)
        return;

    win_priv = vrr_window_priv(rec->win);
    if (!win_priv)
        return;

    if (rec->state == PropertyNewValue) {
        if (rec->prop->format == 32 && rec->prop->size == 1) {
            uint32_t *value = rec->prop->data;
            win_priv->variable_refresh = (*value != 0);
        }
    } else {
        win_priv->variable_refresh = FALSE;
    }
}

Bool
vrr_window_has_variable_refresh(WindowPtr window)
{
    vrr_window_priv_ptr win_priv = vrr_window_priv(window);
    return win_priv ? win_priv->variable_refresh : FALSE;
}

Bool
vrr_screen_init(ScreenPtr screen, const vrr_screen_info_rec *info)
{
    vrr_screen_priv_ptr screen_priv;

    if (!dixRegisterPrivateKey(&vrr_screen_private_key, PRIVATE_SCREEN, 0))
        return FALSE;

    if (!dixRegisterPrivateKey(&vrr_window_private_key, PRIVATE_WINDOW,
                               sizeof(vrr_window_priv_rec)))
        return FALSE;

    if (vrr_screen_priv(screen))
        return TRUE;

    screen_priv = calloc(1, sizeof(vrr_screen_priv_rec));
    if (!screen_priv)
        return FALSE;

    screen_priv->info = info;
    screen_priv->vrr_atom = MakeAtom("_VARIABLE_REFRESH",
                                     strlen("_VARIABLE_REFRESH"), TRUE);

    if (info && info->check_vrr_capable)
        screen_priv->is_vrr_capable = info->check_vrr_capable(screen);

    wrap(screen_priv, screen, CloseScreen, vrr_close_screen);

    dixSetPrivate(&screen->devPrivates, &vrr_screen_private_key, screen_priv);
    return TRUE;
}

void
vrr_extension_init(void)
{
    ExtensionEntry *extension;

    extension = AddExtension(VRR_NAME, 0, 0,
                             NULL, NULL,
                             NULL, StandardMinorOpcode);
    if (!extension)
        goto bail;

    if (!vrr_callback_registered) {
        AddCallback(&PropertyStateCallback, vrr_property_callback, NULL);
        vrr_callback_registered = TRUE;
    }

    return;

bail:
    FatalError("Cannot initialize SPAGHETTI-VRR extension");
}
