/* Test relies on assert() */
#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "dixstruct.h"
#include "randrstr.h"
#include "tests-common.h"

static unsigned terminated;
static int driver_fd;

static int
create_lease(ScreenPtr screen, RRLeasePtr lease, int *fd)
{
    *fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(*fd >= 0);
    driver_fd = *fd;
    return Success;
}

static void
terminate_lease(ScreenPtr screen, RRLeasePtr lease)
{
    terminated++;
    RRLeaseTerminated(lease);
    RRLeaseFree(lease);
}

#ifdef LDWRAP_TESTS
DECLARE_WRAP_FUNCTION(AddResource, Bool, XID id, RESTYPE type, void *value);
extern Bool __real_AddResource(XID id, RESTYPE type, void *value);

static Bool
fail_resource(XID id, RESTYPE type, void *value)
{
    assert(__real_AddResource(id, type, value));
    FreeResource(id, X11_RESTYPE_NONE);
    return FALSE;
}
#endif

static void
rrlease_lifetime(void)
{
    ClientRec client = { .index = 0 };
    ScreenRec screen = { 0 }, other = { 0 };
    RRCrtcRec crtc = { .pScreen = &screen };
    RROutputRec output = { .pScreen = &screen };
    RRCrtcPtr crtcs[] = { &crtc, &crtc };
    RROutputPtr outputs[] = { &output, &output };
    rrScrPrivPtr priv;
    RRLeasePtr lease;
    int fd;

    serverGeneration = 1;
    serverClient = clients[0] = &client;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&client.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&client));
    assert(dixAllocatePrivates(&screen.devPrivates, PRIVATE_SCREEN));
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &screen;
    assert(RRScreenInit(&screen));
    priv = rrGetScrPriv(&screen);
    priv->crtcs = crtcs;
    priv->numCrtcs = 2;
    priv->outputs = outputs;
    priv->numOutputs = 2;
    assert(!RRLeaseCreate(&screen, 1, crtcs, 1, outputs, &fd));
    assert(fd == -1);
    priv->rrCreateLease = create_lease;
    priv->rrTerminateLease = terminate_lease;
    assert(!RRLeaseCreate(&screen, 2, crtcs, 1, outputs, &fd));
    assert(!RRLeaseCreate(&screen, 1, crtcs, 2, outputs, &fd));
    crtc.pScreen = &other;
    assert(!RRLeaseCreate(&screen, 1, crtcs, 1, outputs, &fd));
    crtc.pScreen = &screen;

    lease = RRLeaseCreate(&screen, 1, crtcs, 1, outputs, &fd);
    assert(lease && fd >= 0);
    assert(lease->state == RRLeaseRunning);
    assert(RRCrtcIsLeased(&crtc));
    assert(RROutputIsLeased(&output));
    int rejected_fd;
    assert(!RRLeaseCreate(&screen, 1, crtcs, 1, outputs, &rejected_fd));
    assert(rejected_fd == -1);
    close(fd);
    RRTerminateLease(lease);
    assert(terminated == 1);
    assert(!RRCrtcIsLeased(&crtc));
    assert(!RROutputIsLeased(&output));

#ifdef LDWRAP_TESTS
    wrapped_AddResource = fail_resource;
    assert(!RRLeaseCreate(&screen, 1, crtcs, 1, outputs, &fd));
    wrapped_AddResource = NULL;
    assert(fd == -1);
    assert(fcntl(driver_fd, F_GETFD) == -1);
    assert(terminated == 2);
    assert(!RRCrtcIsLeased(&crtc));
    assert(!RROutputIsLeased(&output));
#endif
    priv->crtcs = NULL;
    priv->outputs = NULL;
    priv->numCrtcs = priv->numOutputs = 0;
    FreeAllResources();
    free(priv);
    dixFreePrivates(screen.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(client.devPrivates, PRIVATE_CLIENT);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    clients[0] = serverClient = NULL;
    dixResetPrivates();
}

const testfunc_t *
rrlease_test(void)
{
    static const testfunc_t tests[] = { rrlease_lifetime, NULL };
    return tests;
}
