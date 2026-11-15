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

enum {
    RRLEASE_TEST_CRTC_COUNT = 2,
    RRLEASE_TEST_OUTPUT_COUNT = 2
};

struct rrlease_fixture {
    unsigned terminated;
    int driver_fd;
};

struct rrlease_test {
    ClientRec client;
    ScreenRec screen;
    ScreenRec other;
    RRCrtcRec crtc;
    RROutputRec output;
    RRCrtcPtr crtcs[RRLEASE_TEST_CRTC_COUNT];
    RROutputPtr outputs[RRLEASE_TEST_OUTPUT_COUNT];
    rrScrPrivPtr priv;
    int fd;
};

static struct rrlease_fixture fixture;

static int
create_lease(ScreenPtr screen, RRLeasePtr lease, int *fd)
{
    *fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(*fd >= 0);
    fixture.driver_fd = *fd;
    return Success;
}

static void
terminate_lease(ScreenPtr screen, RRLeasePtr lease)
{
    fixture.terminated++;
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
rrlease_test_init(struct rrlease_test *test)
{
    *test = (struct rrlease_test) {
        .client = { .index = 0 },
        .fd = -1
    };
    fixture = (struct rrlease_fixture) { .driver_fd = -1 };
    test->crtc.pScreen = &test->screen;
    test->output.pScreen = &test->screen;
    test->crtcs[0] = test->crtcs[1] = &test->crtc;
    test->outputs[0] = test->outputs[1] = &test->output;

    serverGeneration = 1;
    serverClient = clients[0] = &test->client;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&test->client.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&test->client));
    assert(dixAllocatePrivates(&test->screen.devPrivates, PRIVATE_SCREEN));
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &test->screen;
    assert(RRScreenInit(&test->screen));
    test->priv = rrGetScrPriv(&test->screen);
    test->priv->crtcs = test->crtcs;
    test->priv->numCrtcs = RRLEASE_TEST_CRTC_COUNT;
    test->priv->outputs = test->outputs;
    test->priv->numOutputs = RRLEASE_TEST_OUTPUT_COUNT;
}

static void
rrlease_test_fini(struct rrlease_test *test)
{
    test->priv->crtcs = NULL;
    test->priv->outputs = NULL;
    test->priv->numCrtcs = test->priv->numOutputs = 0;
    FreeAllResources();
    free(test->priv);
    dixFreePrivates(test->screen.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(test->client.devPrivates, PRIVATE_CLIENT);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    clients[0] = serverClient = NULL;
    dixResetPrivates();
}

static void
rrlease_test_reject_invalid(struct rrlease_test *test)
{
    assert(!RRLeaseCreate(&test->screen, 1, test->crtcs, 1, test->outputs,
                          &test->fd));
    assert(test->fd == -1);
    test->priv->rrCreateLease = create_lease;
    test->priv->rrTerminateLease = terminate_lease;
    assert(!RRLeaseCreate(&test->screen, RRLEASE_TEST_CRTC_COUNT, test->crtcs,
                          1, test->outputs, &test->fd));
    assert(!RRLeaseCreate(&test->screen, 1, test->crtcs,
                          RRLEASE_TEST_OUTPUT_COUNT, test->outputs, &test->fd));
    test->crtc.pScreen = &test->other;
    assert(!RRLeaseCreate(&test->screen, 1, test->crtcs, 1, test->outputs,
                          &test->fd));
    test->crtc.pScreen = &test->screen;
}

static void
rrlease_test_lease(struct rrlease_test *test)
{
    RRLeasePtr lease;
    int rejected_fd;

    lease = RRLeaseCreate(&test->screen, 1, test->crtcs, 1, test->outputs,
                          &test->fd);
    assert(lease && test->fd >= 0);
    assert(lease->state == RRLeaseRunning);
    assert(RRCrtcIsLeased(&test->crtc));
    assert(RROutputIsLeased(&test->output));
    assert(!RRLeaseCreate(&test->screen, 1, test->crtcs, 1, test->outputs,
                          &rejected_fd));
    assert(rejected_fd == -1);
    close(test->fd);
    test->fd = -1;
    RRTerminateLease(lease);
    assert(fixture.terminated == 1);
    assert(!RRCrtcIsLeased(&test->crtc));
    assert(!RROutputIsLeased(&test->output));
}

#ifdef LDWRAP_TESTS
static void
rrlease_test_resource_failure(struct rrlease_test *test)
{
    wrapped_AddResource = fail_resource;
    assert(!RRLeaseCreate(&test->screen, 1, test->crtcs, 1, test->outputs,
                          &test->fd));
    wrapped_AddResource = NULL;
    assert(test->fd == -1);
    assert(fcntl(fixture.driver_fd, F_GETFD) == -1);
    assert(fixture.terminated == 2);
    assert(!RRCrtcIsLeased(&test->crtc));
    assert(!RROutputIsLeased(&test->output));
}
#endif

static void
rrlease_lifetime(void)
{
    struct rrlease_test test;

    rrlease_test_init(&test);
    rrlease_test_reject_invalid(&test);
    rrlease_test_lease(&test);
#ifdef LDWRAP_TESTS
    rrlease_test_resource_failure(&test);
#endif
    rrlease_test_fini(&test);
}

const testfunc_t *
rrlease_test(void)
{
    static const testfunc_t tests[] = { rrlease_lifetime, NULL };
    return tests;
}
