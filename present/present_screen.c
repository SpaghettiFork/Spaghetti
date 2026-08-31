/*
 * Copyright © 2013 Keith Packard
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
 */
#include <dix-config.h>

#include "present_priv.h"

int present_request;
DevPrivateKeyRec present_screen_private_key;
DevPrivateKeyRec present_window_private_key;

/*
 * Get a pointer to a present window private, creating if necessary
 */
present_window_priv_ptr
present_get_window_priv(WindowPtr window, Bool create)
{
    present_window_priv_ptr window_priv = present_window_priv(window);

    if (!create || window_priv != NULL)
        return window_priv;
    window_priv = calloc (1, sizeof (present_window_priv_rec));
    if (!window_priv)
        return NULL;
    xorg_list_init(&window_priv->vblank);
    xorg_list_init(&window_priv->notifies);

    window_priv->window = window;
    window_priv->crtc = PresentCrtcNeverSet;
    dixSetPrivate(&window->devPrivates, &present_window_private_key, window_priv);
    return window_priv;
}

/*
 * Hook the close screen function to clean up our screen private
 */
static Bool
present_close_screen(ScreenPtr screen)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_crtc_priv_ptr crtc_priv, tmp;

    if (screen_priv->flip_destroy)
        screen_priv->flip_destroy(screen);

    unwrap(screen_priv, screen, BlockHandler);
    xorg_list_for_each_entry_safe(crtc_priv, tmp, &screen_priv->crtcs, list) {
        xorg_list_del(&crtc_priv->list);
        free(crtc_priv);
    }

    unwrap(screen_priv, screen, CloseScreen);
    (*screen->CloseScreen) (screen);
    free(screen_priv);
    return TRUE;
}

/*
 * Free any queued presentations for this window
 */
static void
present_free_window_vblank(WindowPtr window)
{
    ScreenPtr                   screen = window->drawable.pScreen;
    present_screen_priv_ptr     screen_priv = present_screen_priv(screen);
    present_window_priv_ptr     window_priv = present_window_priv(window);
    present_vblank_ptr          vblank, tmp;

    xorg_list_for_each_entry_safe(vblank, tmp, &window_priv->vblank, window_list) {
        screen_priv->abort_vblank(window->drawable.pScreen, window, vblank->crtc, vblank->event_id, vblank->target_msc);
        present_vblank_destroy(vblank);
    }
}

/*
 * Hook the close window function to clean up our window private
 */
static Bool
present_destroy_window(WindowPtr window)
{
    Bool ret;
    ScreenPtr screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_window_priv_ptr window_priv = present_window_priv(window);

    present_send_config_notify(window,
                               window->drawable.x,
                               window->drawable.y,
                               window->drawable.width,
                               window->drawable.height,
                               window->borderWidth,
                               window->nextSib,
                               PresentWindowDestroyed);

    if (window_priv) {
        present_clear_window_notifies(window);
        present_free_events(window);
        present_free_window_vblank(window);

        screen_priv->clear_window_flip(window);

        free(window_priv);
    }
    unwrap(screen_priv, screen, DestroyWindow);
    if (screen->DestroyWindow)
        ret = screen->DestroyWindow (window);
    else
        ret = TRUE;
    wrap(screen_priv, screen, DestroyWindow, present_destroy_window);
    return ret;
}

/*
 * Hook the config notify screen function to deliver present config notify events
 */
static int
present_config_notify(WindowPtr window,
                   int x, int y, int w, int h, int bw,
                   WindowPtr sibling)
{
    int ret;
    ScreenPtr screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    present_send_config_notify(window, x, y, w, h, bw, sibling, 0);

    unwrap(screen_priv, screen, ConfigNotify);
    if (screen->ConfigNotify)
        ret = screen->ConfigNotify (window, x, y, w, h, bw, sibling);
    else
        ret = 0;
    wrap(screen_priv, screen, ConfigNotify, present_config_notify);
    return ret;
}

/*
 * Hook the clip notify screen function to un-flip as necessary
 */

static void
present_clip_notify(WindowPtr window, int dx, int dy)
{
    ScreenPtr screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    screen_priv->check_flip_window(window);
    unwrap(screen_priv, screen, ClipNotify)
    if (screen->ClipNotify)
        screen->ClipNotify (window, dx, dy);
    wrap(screen_priv, screen, ClipNotify, present_clip_notify);
}

Bool
present_screen_register_priv_keys(void)
{
    if (!dixRegisterPrivateKey(&present_screen_private_key, PRIVATE_SCREEN, 0))
        return FALSE;

    if (!dixRegisterPrivateKey(&present_window_private_key, PRIVATE_WINDOW, 0))
        return FALSE;

    return TRUE;
}

present_crtc_priv_ptr
present_get_crtc_priv(ScreenPtr screen, RRCrtcPtr crtc, Bool create)
{
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);
    present_crtc_priv_ptr crtc_priv;

    if (!screen_priv)
        return NULL;

    if (crtc)
        screen = crtc->pScreen;

    screen_priv = present_screen_priv(screen);
    if (!screen_priv)
        return NULL;

    xorg_list_for_each_entry(crtc_priv, &screen_priv->crtcs, list) {
        if (crtc_priv->crtc == crtc)
            return crtc_priv;
    }

    if (!create)
        return NULL;

    crtc_priv = calloc(1, sizeof(present_crtc_priv_rec));
    if (!crtc_priv)
        return NULL;

    crtc_priv->crtc = crtc;
    xorg_list_init(&crtc_priv->exec_queue);
    xorg_list_init(&crtc_priv->flip_queue);
    xorg_list_append(&crtc_priv->list, &screen_priv->crtcs);

    return crtc_priv;
}

present_crtc_priv_ptr
present_crtc_priv_for_crtc(RRCrtcPtr crtc, Bool create)
{
    ScreenPtr screen;

    if (!crtc)
        return NULL;

    screen = crtc->pScreen;
    return present_get_crtc_priv(screen, crtc, create);
}

void
present_free_crtc_priv(present_crtc_priv_ptr crtc_priv)
{
    if (!crtc_priv)
        return;
    xorg_list_del(&crtc_priv->list);
    free(crtc_priv);
}

present_screen_priv_ptr
present_screen_priv_init(ScreenPtr screen)
{
    present_screen_priv_ptr screen_priv;

    screen_priv = calloc(1, sizeof (present_screen_priv_rec));
    if (!screen_priv)
        return NULL;

    xorg_list_init(&screen_priv->crtcs);

    wrap(screen_priv, screen, CloseScreen, present_close_screen);
    wrap(screen_priv, screen, DestroyWindow, present_destroy_window);
    wrap(screen_priv, screen, ConfigNotify, present_config_notify);
    wrap(screen_priv, screen, ClipNotify, present_clip_notify);
    wrap(screen_priv, screen, BlockHandler, present_scmd_block_handler);

    dixSetPrivate(&screen->devPrivates, &present_screen_private_key, screen_priv);
    screen_priv->pScreen = screen;

    return screen_priv;
}

static int
check_flip_visit(WindowPtr window, void *data)
{
    ScreenPtr screen = window->drawable.pScreen;
    present_screen_priv_ptr screen_priv = present_screen_priv(screen);

    if (!screen_priv)
        return WT_DONTWALKCHILDREN;

    screen_priv->check_flip_window(window);

    return WT_WALKCHILDREN;
}

void
present_check_flips(WindowPtr window)
{
    TraverseTree(window, check_flip_visit, NULL);
}

/*
 * Initialize a screen for use with present in default screen flip mode (scmd)
 */
int
present_screen_init(ScreenPtr screen, present_screen_info_ptr info)
{
    if (!present_screen_register_priv_keys())
        return FALSE;

    if (!present_screen_priv(screen)) {
        present_screen_priv_ptr screen_priv = present_screen_priv_init(screen);
        if (!screen_priv)
            return FALSE;

        screen_priv->info = info;

        present_scmd_init_mode_hooks(screen_priv);
        present_scmd_init_driver_flip(screen_priv);
        present_fake_screen_init(screen);
    }

    return TRUE;
}

/*
 * Initialize the present extension
 */
void
present_extension_init(void)
{
    ExtensionEntry *extension;
    int i;

#ifdef PANORAMIX
    if (!noPanoramiXExtension)
        return;
#endif

    extension = AddExtension(PRESENT_NAME, PresentNumberEvents, PresentNumberErrors,
                             proc_present_dispatch, sproc_present_dispatch,
                             NULL, StandardMinorOpcode);
    if (!extension)
        goto bail;

    present_request = extension->base;

    if (!present_init())
        goto bail;

    if (!present_event_init())
        goto bail;

    for (i = 0; i < screenInfo.numScreens; i++) {
        if (!present_screen_init(screenInfo.screens[i], NULL))
            goto bail;
    }
    return;

bail:
    FatalError("Cannot initialize Present extension");
}
