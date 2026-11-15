/* Test relies on assert() */
#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "dixstruct.h"
#include "extinit.h"
#include "pixmapstr.h"
#include "scrnintstr.h"
#include "servermd.h"
#include "shmint.h"
#include "os/busfault.h"
#include "tests-common.h"

#ifdef SHM_FD_PASSING

enum {
    SHM_TEST_WIDTH = 2,
    SHM_TEST_HEIGHT = 2,
    SHM_TEST_DEPTH = 32,
    SHM_TEST_BPP = 32,
    SHM_TEST_STRIDE = 16,
    SHM_TEST_SIZE = SHM_TEST_STRIDE * SHM_TEST_HEIGHT,
    SHM_TEST_LARGE_STRIDE = 1 << 16,
    SHM_TEST_ROW_WORDS = SHM_TEST_STRIDE / sizeof(uint32_t),
    SHM_TEST_IMPORT_COUNT = 2,
    SHM_TEST_FIRST_ROW_SENTINEL = 0x12345678,
    SHM_TEST_SECOND_ROW_SENTINEL = 0x87654321
};

static unsigned destroyed;

struct shm_test {
    ClientRec client;
    ScreenRec screen;
    DepthRec depth;
    int fd;
    uint32_t rows[SHM_TEST_SIZE / sizeof(uint32_t)];
};

static PixmapPtr
create_pixmap(ScreenPtr screen, int width, int height, int depth, unsigned usage)
{
    PixmapPtr pixmap = dixAllocateScreenObjectWithPrivates(screen, PixmapRec,
                                                           PRIVATE_PIXMAP);

    assert(pixmap);
    pixmap->drawable.type = DRAWABLE_PIXMAP;
    pixmap->drawable.pScreen = screen;
    pixmap->refcnt = 1;
    return pixmap;
}

static Bool
destroy_pixmap(PixmapPtr pixmap)
{
    if (--pixmap->refcnt == 0) {
        destroyed++;
        dixFreeObjectWithPrivates(pixmap, PRIVATE_PIXMAP);
    }
    return TRUE;
}

static Bool
modify_pixmap(PixmapPtr pixmap,
              int width, int height,
              int depth, int bpp,
              int stride, void *data)
{
    pixmap->drawable.width = width;
    pixmap->drawable.height = height;
    pixmap->drawable.depth = depth;
    pixmap->drawable.bitsPerPixel = bpp;
    pixmap->devKind = stride;
    pixmap->devPrivate.ptr = data;
    return TRUE;
}

static void
shm_test_init(struct shm_test *test)
{
    char path[] = "/tmp/spaghetti-shm-test-XXXXXX";

    *test = (struct shm_test) {
        .client = { .index = 0 },
        .screen = {
            .CreatePixmap = create_pixmap,
            .DestroyPixmap = destroy_pixmap,
            .ModifyPixmapHeader = modify_pixmap,
            .rootDepth = SHM_TEST_DEPTH
        },
        .depth = { .depth = SHM_TEST_DEPTH },
        .fd = -1,
        .rows = {
            [0] = SHM_TEST_FIRST_ROW_SENTINEL,
            [SHM_TEST_ROW_WORDS] = SHM_TEST_SECOND_ROW_SENTINEL
        }
    };
    destroyed = 0;

    test->fd = mkstemp(path);
    assert(test->fd >= 0);
    assert(unlink(path) == 0);
    assert(write(test->fd, test->rows, sizeof(test->rows)) ==
           sizeof(test->rows));
    serverClient = clients[0] = &test->client;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&test->client.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&test->client));
    assert(dixAllocatePrivates(&test->screen.devPrivates, PRIVATE_SCREEN));
    test->screen.numDepths = 1;
    test->screen.allowedDepths = &test->depth;
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &test->screen;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].bitsPerPixel = SHM_TEST_BPP;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].padRoundUp = 0;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].padPixelsLog2 = 0;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].padBytesLog2 = 2;
    InitAtoms();
    assert(busfault_init());
    ShmRegisterFbFuncs(&test->screen);
    ShmExtensionInit();
}

static void
shm_test_fini(struct shm_test *test)
{
    close(test->fd);
    FreeAllResources();
    CloseDownExtensions();
    assert(test->screen.CloseScreen(&test->screen));
    dixFreePrivates(test->screen.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(test->client.devPrivates, PRIVATE_CLIENT);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    clients[0] = serverClient = NULL;
    dixResetPrivates();
}

static void
shm_test_reject_imports(struct shm_test *test)
{
    assert(!shm_create_pixmap_from_fd(&test->screen, test->fd, 0,
                                      SHM_TEST_HEIGHT, SHM_TEST_DEPTH,
                                      SHM_TEST_BPP, SHM_TEST_STRIDE, 0));
    assert(!shm_create_pixmap_from_fd(&test->screen, test->fd,
                                      SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP,
                                      SHM_TEST_STRIDE, SHM_TEST_SIZE));
    assert(!shm_create_pixmap_from_fd(&test->screen, test->fd,
                                      SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP,
                                      sizeof(uint32_t), 0));
    assert(!shm_create_pixmap_from_fd(&test->screen, test->fd,
                                      SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP / 2,
                                      SHM_TEST_STRIDE, 0));
    assert(!shm_create_pixmap_from_fd(&test->screen, test->fd,
                                      SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP,
                                      UINT32_MAX, 0));
}

static void
shm_test_import_large(struct shm_test *test)
{
    PixmapPtr pixmap;

    assert(ftruncate(test->fd, SHM_TEST_LARGE_STRIDE * SHM_TEST_HEIGHT) == 0);
    assert(pwrite(test->fd, &test->rows[SHM_TEST_ROW_WORDS],
                  sizeof(uint32_t),
                  SHM_TEST_LARGE_STRIDE) == sizeof(uint32_t));
    pixmap = shm_create_pixmap_from_fd(&test->screen, test->fd,
                                       SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                       SHM_TEST_DEPTH, SHM_TEST_BPP,
                                       SHM_TEST_LARGE_STRIDE, 0);
    assert(pixmap);
    assert(pixmap->devKind == SHM_TEST_LARGE_STRIDE);
    assert(*(uint32_t *) pixmap->devPrivate.ptr == test->rows[0]);
    assert(*(uint32_t *) ((char *) pixmap->devPrivate.ptr + pixmap->devKind)
           == test->rows[SHM_TEST_ROW_WORDS]);
    assert(test->screen.DestroyPixmap(pixmap));
    assert(destroyed == 1);
    assert(ftruncate(test->fd, sizeof(test->rows)) == 0);
    assert(pwrite(test->fd, test->rows, sizeof(test->rows), 0) ==
           sizeof(test->rows));
}

static PixmapPtr
shm_test_import(struct shm_test *test, unsigned index)
{
    PixmapPtr pixmap =
        shm_create_pixmap_from_fd(&test->screen, test->fd, SHM_TEST_WIDTH,
                                  SHM_TEST_HEIGHT, SHM_TEST_DEPTH,
                                  SHM_TEST_BPP, SHM_TEST_STRIDE, 0);

    assert(pixmap);
    assert(fcntl(test->fd, F_GETFD) >= 0);
    assert(pixmap->devKind == SHM_TEST_STRIDE);
    assert(*(uint32_t *) pixmap->devPrivate.ptr == test->rows[0]);
    assert(*(uint32_t *) ((char *) pixmap->devPrivate.ptr + pixmap->devKind)
           == test->rows[SHM_TEST_ROW_WORDS]);
#ifdef BUSFAULT
    if (index == SHM_TEST_IMPORT_COUNT - 1) {
        assert(ftruncate(test->fd, 0) == 0);
        assert(*(volatile uint32_t *) pixmap->devPrivate.ptr == 0);
        busfault_check();
    }
#endif
    return pixmap;
}

static void
shm_import(void)
{
    struct shm_test test;
    unsigned previous_destroyed;

    shm_test_init(&test);
    shm_test_reject_imports(&test);
    shm_test_import_large(&test);
    previous_destroyed = destroyed;
    for (unsigned i = 0; i < SHM_TEST_IMPORT_COUNT; i++) {
        PixmapPtr pixmap = shm_test_import(&test, i);

        pixmap->refcnt++;
        assert(test.screen.DestroyPixmap(pixmap));
        assert(destroyed == previous_destroyed + i);
        assert(test.screen.DestroyPixmap(pixmap));
        assert(destroyed == previous_destroyed + i + 1);
    }
    shm_test_fini(&test);
}
#endif

const testfunc_t *
shm_test(void)
{
    static const testfunc_t tests[] = {
#ifdef SHM_FD_PASSING
        shm_import,
#endif
        NULL
    };
    return tests;
}
