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

static void
dri3_native_access(void)
{
    ClientRec server = { .index = 0 };
    ScreenRec screen = { .CloseScreen = close_screen }, unavailable = { 0 };
    DrawableRec drawable = { .pScreen = &screen };
    dri3_screen_info_rec info = {
        .version = DRI3_SCREEN_INFO_VERSION,
        .open_client = open_device,
        .get_formats = get_formats,
        .get_modifiers = get_modifiers
    };
    ExternalClientCallbacks callbacks = { .write = write_reply,
                                           .send_fd = receive_fd };
    LocalClientCredRec credentials = {
        .fieldsSet = LCC_UID_SET | LCC_GID_SET | LCC_PID_SET,
        .euid = getuid(), .egid = getgid(), .pid = getpid()
    };
    ClientPtr client;
    CARD32 ndrawable, nscreen;
    CARD64 *drawable_modifiers, *screen_modifiers;
    int fd;

    serverClient = clients[0] = &server;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&server.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&server));
    assert(XFixesClientDisconnectInit());
    assert(dixAllocatePrivates(&screen.devPrivates, PRIVATE_SCREEN));
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &screen;
    assert(dri3_screen_init(&screen, &info));
    assert(dixAllocatePrivates(&unavailable.devPrivates, PRIVATE_SCREEN));
    client = dixCreateExternalClient(&callbacks, &credentials, NULL);
    assert(client);
    assert(dri3_open(client, &screen, NULL, &fd) == Success);
    assert(fd >= 0 && fcntl(fd, F_GETFD) >= 0);
    close(fd);
    fixture.asynchronous = TRUE;
    assert(dri3_open(client, &screen, NULL, &fd) == Success);
    assert(fd == -1 && client->ignoreCount == 1);
    assert(dri3_open(client, &screen, NULL, &fd) == BadAccess);
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

    assert(dri3_get_supported_modifiers(&screen, &drawable, DRI3_TEST_DEPTH, DRI3_TEST_BPP, &ndrawable,
                                         &drawable_modifiers, &nscreen,
                                         &screen_modifiers) == Success);
    assert(ndrawable == 0 && !drawable_modifiers);
    assert(nscreen == 1 && screen_modifiers[0] == DRM_FORMAT_MOD_LINEAR);
    free(screen_modifiers);
    assert(dri3_get_supported_modifiers(&screen, &drawable, DRI3_TEST_DEPTH, UINT8_MAX,
                                         &ndrawable, &drawable_modifiers,
                                         &nscreen, &screen_modifiers) == BadValue);
    assert(!ndrawable && !nscreen && !drawable_modifiers && !screen_modifiers);
    assert(dri3_open(client, NULL, NULL, &fd) == BadMatch);
    assert(fd == -1);
    assert(dri3_open(client, &unavailable, NULL, &fd) == BadMatch);
    assert(fd == -1);
    CloseDownClient(client);
    assert(screen.CloseScreen(&screen));
    FreeAllResources();
    dixFreePrivates(unavailable.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(screen.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(server.devPrivates, PRIVATE_CLIENT);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    serverClient = clients[0] = NULL;
    dixResetPrivates();
}

const testfunc_t *
dri3_test(void)
{
    static const testfunc_t tests[] = { dri3_native_access, NULL };
    return tests;
}
