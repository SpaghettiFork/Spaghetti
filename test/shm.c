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
    SHM_TEST_SIZE = SHM_TEST_STRIDE * SHM_TEST_HEIGHT
};

static unsigned destroyed;

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
modify_pixmap(PixmapPtr pixmap, int width, int height, int depth, int bpp,
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
shm_import(void)
{
    ClientRec client = { .index = 0 };
    ScreenRec screen = {
        .CreatePixmap = create_pixmap,
        .DestroyPixmap = destroy_pixmap,
        .ModifyPixmapHeader = modify_pixmap,
        .rootDepth = SHM_TEST_DEPTH
    };
    DepthRec depth = { .depth = SHM_TEST_DEPTH };
    char path[] = "/tmp/spaghetti-shm-test-XXXXXX";
    int fd = mkstemp(path);
    PixmapPtr pixmap;
    uint32_t rows[SHM_TEST_SIZE / sizeof(uint32_t)] = {
        [0] = 0x12345678,
        [SHM_TEST_STRIDE / sizeof(uint32_t)] = 0x87654321
    };

    assert(fd >= 0);
    assert(unlink(path) == 0);
    assert(write(fd, rows, sizeof(rows)) == sizeof(rows));
    serverClient = clients[0] = &client;
    currentMaxClients = 1;
    assert(dixAllocatePrivates(&client.devPrivates, PRIVATE_CLIENT));
    assert(InitClientResources(&client));
    assert(dixAllocatePrivates(&screen.devPrivates, PRIVATE_SCREEN));
    screen.numDepths = 1;
    screen.allowedDepths = &depth;
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &screen;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].bitsPerPixel = SHM_TEST_BPP;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].padRoundUp = 0;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].padPixelsLog2 = 0;
    PixmapWidthPaddingInfo[SHM_TEST_DEPTH].padBytesLog2 = 2;
    InitAtoms();
    assert(busfault_init());
    ShmRegisterFbFuncs(&screen);
    ShmExtensionInit();

    assert(!shm_create_pixmap_from_fd(&screen, fd, 0, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP,
                                      SHM_TEST_STRIDE, 0));
    assert(!shm_create_pixmap_from_fd(&screen, fd, SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP,
                                      SHM_TEST_STRIDE, SHM_TEST_SIZE));
    assert(!shm_create_pixmap_from_fd(&screen, fd, SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP,
                                      sizeof(uint32_t), 0));
    assert(!shm_create_pixmap_from_fd(&screen, fd, SHM_TEST_WIDTH, SHM_TEST_HEIGHT,
                                      SHM_TEST_DEPTH, SHM_TEST_BPP / 2,
                                      SHM_TEST_STRIDE, 0));

    for (unsigned i = 0; i < 2; i++) {
        pixmap = shm_create_pixmap_from_fd(&screen, fd, SHM_TEST_WIDTH,
                                            SHM_TEST_HEIGHT, SHM_TEST_DEPTH,
                                            SHM_TEST_BPP, SHM_TEST_STRIDE, 0);
        assert(pixmap);
        assert(fcntl(fd, F_GETFD) >= 0);
        assert(pixmap->devKind == SHM_TEST_STRIDE);
        assert(*(uint32_t *) pixmap->devPrivate.ptr == rows[0]);
        assert(*(uint32_t *) ((char *) pixmap->devPrivate.ptr + pixmap->devKind)
               == rows[SHM_TEST_STRIDE / sizeof(uint32_t)]);
#ifdef BUSFAULT
        if (i == 1) {
            assert(ftruncate(fd, 0) == 0);
            assert(*(volatile uint32_t *) pixmap->devPrivate.ptr == 0);
            busfault_check();
        }
#endif
        pixmap->refcnt++;
        assert(screen.DestroyPixmap(pixmap));
        assert(destroyed == i);
        assert(screen.DestroyPixmap(pixmap));
        assert(destroyed == i + 1);
    }
    close(fd);
    FreeAllResources();
    CloseDownExtensions();
    assert(screen.CloseScreen(&screen));
    dixFreePrivates(screen.devPrivates, PRIVATE_SCREEN);
    dixFreePrivates(client.devPrivates, PRIVATE_CLIENT);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    clients[0] = serverClient = NULL;
    dixResetPrivates();
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
