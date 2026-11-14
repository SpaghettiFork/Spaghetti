#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "tests-common.h"
#include "windowstr.h"
#include "vrr/vrr_priv.h"

static void
vrr_flip_without_screen_init(void)
{
    ScreenRec screen = {0};
    WindowRec window = {0};

    dixResetPrivates();
    assert(!dixPrivateKeyRegistered(&vrr_screen_private_key));

    vrr_set_flip_window(&screen, &window);
    vrr_set_flip_window(&screen, NULL);

    assert(!dixPrivateKeyRegistered(&vrr_screen_private_key));
}

static void
vrr_flip_without_screen_state(void)
{
    ScreenRec screen = {0};
    WindowRec window = {0};

    dixResetPrivates();
    assert(dixRegisterPrivateKey(&vrr_screen_private_key, PRIVATE_SCREEN, 0));
    assert(dixAllocatePrivates(&screen.devPrivates, PRIVATE_SCREEN));

    vrr_set_flip_window(&screen, &window);
    vrr_set_flip_window(&screen, NULL);

    dixFreePrivates(screen.devPrivates, PRIVATE_SCREEN);
    dixResetPrivates();
}

const testfunc_t *
vrr_test(void)
{
    static const testfunc_t tests[] = {
        vrr_flip_without_screen_init,
        vrr_flip_without_screen_state,
        NULL
    };

    return tests;
}
