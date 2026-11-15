/* Test relies on assert() */
#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include "dixstruct.h"
#include "external.h"
#include "dri3.h"
#include "xfixesint.h"
#include "tests-common.h"

struct dri3_fixture {
    int fd;
    xDRI3OpenReply reply;
    Bool asynchronous;
    Bool reject_fd;
};

struct dri3_test {
    ClientRec server;
    ScreenRec screen;
    ScreenRec unavailable;
    DrawableRec drawable;
    dri3_screen_info_rec info;
    LocalClientCredRec credentials;
    ClientPtr client;
};

enum { DRI3_TEST_DEPTH = 32, DRI3_TEST_BPP = 32 };

static struct dri3_fixture fixture = { .fd = -1 };

static Bool
close_screen(ScreenPtr screen)
{
    return TRUE;
}

static int
open_device(ClientPtr client, ScreenPtr screen, RRProviderPtr provider, int *fd)
{
    if (fixture.asynchronous)
        IgnoreClient(client);
    else {
        *fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        assert(*fd >= 0);
    }
    return Success;
}

static int
write_reply(ClientPtr client, int count, const void *data, void *closure)
{
    assert(count == sizeof(fixture.reply));
    memcpy(&fixture.reply, data, count);
    return count;
}

static int
receive_fd(ClientPtr client, int fd, void *closure)
{
    if (fixture.reject_fd)
        return -1;
    fixture.fd = fd;
    return 0;
}

static int
get_formats(ScreenPtr screen, CARD32 *count, CARD32 **formats)
{
    *count = 1;
    *formats = malloc(sizeof(**formats));
    assert(*formats);
    **formats = DRM_FORMAT_ARGB8888;
    return TRUE;
}

static int
get_modifiers(ScreenPtr screen, uint32_t format, uint32_t *count,
              uint64_t **modifiers)
{
    assert(format == DRM_FORMAT_ARGB8888);
    *count = 1;
    *modifiers = malloc(sizeof(**modifiers));
    assert(*modifiers);
    **modifiers = DRM_FORMAT_MOD_LINEAR;
    return TRUE;
}

static const ExternalClientCallbacks dri3_test_callbacks = {
    .write = write_reply,
    .send_fd = receive_fd
};

static void
dri3_test_init(struct dri3_test *test)
{
    fixture = (struct dri3_fixture) { .fd = -1 };
    *test = (struct dri3_test) {
        .screen.CloseScreen = close_screen,
        .info = {
            .version = DRI3_SCREEN_INFO_VERSION,
            .open_client = open_device,
            .get_formats = get_formats,
            .get_modifiers = get_modifiers
        },
        .credentials = {
            .fieldsSet = LCC_UID_SET | LCC_GID_SET | LCC_PID_SET,
            .euid = getuid(), .egid = getgid(), .pid = getpid()
        }
    };
    test->drawable.pScreen = &test->screen;

    serverClient = clients[0] = &test->server;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&test->server.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&test->server));
    assert(XFixesClientDisconnectInit());
    assert(dixAllocatePrivates(&test->screen.devPrivates, PRIVATE_SCREEN));
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &test->screen;
    assert(dri3_screen_init(&test->screen, &test->info));
    assert(dixAllocatePrivates(&test->unavailable.devPrivates, PRIVATE_SCREEN));
}

static void
dri3_test_fini(struct dri3_test *test)
{
    CloseDownClient(test->client);
    test->client = NULL;
    assert(test->screen.CloseScreen(&test->screen));
    FreeAllResources();
    dixFreePrivates(test->unavailable.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(test->screen.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(test->server.devPrivates, PRIVATE_CLIENT);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    serverClient = clients[0] = NULL;
    dixResetPrivates();
}

static void
dri3_test_client(struct dri3_test *test)
{
    test->client = dixCreateExternalClient(&dri3_test_callbacks,
                                           &test->credentials,
                                           &fixture);
    assert(test->client);
}

static void
dri3_test_open(struct dri3_test *test)
{
    ClientPtr client = test->client;
    int fd;

    assert(dri3_open(client, &test->screen, NULL, &fd) == Success);
    assert(fd >= 0 && fcntl(fd, F_GETFD) >= 0);
    close(fd);

    fixture.asynchronous = TRUE;
    assert(dri3_open(client, &test->screen, NULL, &fd) == Success);
    assert(fd == -1 && client->ignoreCount == 1);
    assert(dri3_open(client, &test->screen, NULL, &fd) == BadAccess);
}

static void
dri3_test_send_reply(struct dri3_test *test)
{
    ClientPtr client = test->client;
    int fd;

    fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    assert(dri3_send_open_reply(client, fd) == Success);
    AttendClient(client);
    assert(fixture.fd == fd);
    assert(fixture.reply.type == X_Reply && fixture.reply.nfd == 1);
    assert(client->ignoreCount == 0);
    close(fixture.fd);
    fixture.fd = -1;

    fixture.reject_fd = TRUE;
    fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    assert(dri3_send_open_reply(client, fd) == BadAlloc);
    assert(fcntl(fd, F_GETFD) == -1);
    assert(fixture.fd == -1);
}

static void
dri3_test_modifiers(struct dri3_test *test)
{
    CARD32 ndrawable = 0, nscreen = 0;
    CARD64 sentinel = 0;
    CARD64 *drawable_modifiers = NULL, *screen_modifiers = NULL;

    assert(dri3_get_supported_modifiers(&test->screen, &test->drawable,
                                        DRI3_TEST_DEPTH, DRI3_TEST_BPP,
                                        &ndrawable, &drawable_modifiers,
                                        &nscreen,
                                        &screen_modifiers) == Success);
    assert(ndrawable == 0 && !drawable_modifiers);
    assert(nscreen == 1 && screen_modifiers[0] == DRM_FORMAT_MOD_LINEAR);
    free(screen_modifiers);

    ndrawable = nscreen = UINT32_MAX;
    drawable_modifiers = screen_modifiers = &sentinel;
    assert(dri3_get_supported_modifiers(&test->screen, &test->drawable,
                                        DRI3_TEST_DEPTH, UINT8_MAX,
                                        &ndrawable, &drawable_modifiers,
                                        &nscreen,
                                        &screen_modifiers) == BadValue);
    assert(!ndrawable && !nscreen && !drawable_modifiers && !screen_modifiers);
}

static void
dri3_test_bad_match(struct dri3_test *test)
{
    int fd;

    assert(dri3_open(test->client, NULL, NULL, &fd) == BadMatch);
    assert(fd == -1);
    assert(dri3_open(test->client, &test->unavailable, NULL, &fd) == BadMatch);
    assert(fd == -1);
}

static void
dri3_native_access(void)
{
    struct dri3_test test;

    dri3_test_init(&test);
    dri3_test_client(&test);
    dri3_test_open(&test);
    dri3_test_send_reply(&test);
    dri3_test_modifiers(&test);
    dri3_test_bad_match(&test);
    dri3_test_fini(&test);
}

const testfunc_t *
dri3_test(void)
{
    static const testfunc_t tests[] = { dri3_native_access, NULL };
    return tests;
}
