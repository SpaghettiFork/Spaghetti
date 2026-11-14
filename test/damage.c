/* Test relies on assert() */
#undef NDEBUG

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <assert.h>
#include <string.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "picturestr.h"
#include "damage.h"
#include "tests-common.h"

#define DAMAGE_TEST_SIZE 128

struct damage_fixture {
    ScreenRec screen;
    PictureScreenRec picture_screen;
    PixmapPtr pixmap;
    PictureRec picture;
    RegionRec clip;
    DamagePtr damage;
    unsigned calls;
};

static Bool
destroy_window(WindowPtr window)
{
    return TRUE;
}

static void
set_window_pixmap(WindowPtr window, PixmapPtr pixmap)
{
}

static Bool
close_screen(ScreenPtr screen)
{
    assert(screen->DestroyWindow == destroy_window);
    assert(screen->SetWindowPixmap == set_window_pixmap);
    return TRUE;
}

static void
add_traps(PicturePtr picture, INT16 x, INT16 y, int count, xTrap *traps)
{
}

static void
validate_gc(GCPtr gc, unsigned long changes, DrawablePtr drawable)
{
}

static void
poly_rectangle(DrawablePtr drawable, GCPtr gc, int count, xRectangle *rectangles)
{
}

static const GCFuncs gc_funcs = { .ValidateGC = validate_gc };
static const GCOps gc_ops = { .PolyRectangle = poly_rectangle };

static Bool
create_gc(GCPtr gc)
{
    gc->funcs = &gc_funcs;
    gc->ops = &gc_ops;
    return TRUE;
}

static void
report_damage(DamagePtr damage, RegionPtr region, void *closure)
{
    unsigned *calls = closure;

    ++*calls;
}

static void
fixture_init(struct damage_fixture *fixture)
{
    BoxRec bounds = { 0, 0, DAMAGE_TEST_SIZE, DAMAGE_TEST_SIZE };

    memset(fixture, 0, sizeof(*fixture));
    assert(dixRegisterPrivateKey(PictureScreenPrivateKey, PRIVATE_SCREEN, 0));
    assert(dixAllocatePrivates(&fixture->screen.devPrivates, PRIVATE_SCREEN));
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &fixture->screen;
    SetPictureScreen(&fixture->screen, &fixture->picture_screen);
    fixture->screen.CloseScreen = close_screen;
    fixture->screen.DestroyWindow = destroy_window;
    fixture->screen.SetWindowPixmap = set_window_pixmap;
    fixture->screen.CreateGC = create_gc;
    fixture->picture_screen.AddTraps = add_traps;
    assert(DamageSetup(&fixture->screen));
    fixture->pixmap = dixAllocateScreenObjectWithPrivates(&fixture->screen,
                                                           PixmapRec, PRIVATE_PIXMAP);
    assert(fixture->pixmap);
    fixture->pixmap->drawable.type = DRAWABLE_PIXMAP;
    fixture->pixmap->drawable.pScreen = &fixture->screen;
    fixture->pixmap->drawable.width = DAMAGE_TEST_SIZE;
    fixture->pixmap->drawable.height = DAMAGE_TEST_SIZE;
    RegionInit(&fixture->clip, &bounds, 1);
    fixture->picture.pDrawable = &fixture->pixmap->drawable;
    fixture->picture.pCompositeClip = &fixture->clip;
    fixture->damage = DamageCreate(report_damage, NULL, DamageReportRawRegion,
                                   TRUE, &fixture->screen, &fixture->calls);
    assert(fixture->damage);
    DamageRegister(&fixture->pixmap->drawable, fixture->damage);
}

static void
fixture_fini(struct damage_fixture *fixture)
{
    DamageDestroy(fixture->damage);
    dixFreeObjectWithPrivates(fixture->pixmap, PRIVATE_PIXMAP);
    RegionUninit(&fixture->clip);
    assert(fixture->screen.CloseScreen(&fixture->screen));
    assert(fixture->picture_screen.AddTraps == add_traps);
    assert(!fixture->picture_screen.Composite);
    assert(!fixture->picture_screen.Glyphs);
    dixFreePrivates(fixture->screen.devPrivates, PRIVATE_SCREEN);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    dixResetPrivates();
}

static GCPtr
fixture_gc(struct damage_fixture *fixture)
{
    GCPtr gc = dixAllocateScreenObjectWithPrivates(&fixture->screen,
                                                 GC, PRIVATE_GC);

    assert(gc);
    gc->pScreen = &fixture->screen;
    gc->pCompositeClip = &fixture->clip;
    assert(fixture->screen.CreateGC(gc));
    gc->funcs->ValidateGC(gc, 0, &fixture->pixmap->drawable);
    return gc;
}

static void
damage_multiple_traps(void)
{
    struct damage_fixture fixture;
    xTrap traps[] = {
        { { 1 << 16, 3 << 16, 1 << 16 },
          { 1 << 16, 3 << 16, 3 << 16 } },
        { { 20 << 16, 24 << 16, 20 << 16 },
          { 20 << 16, 24 << 16, 24 << 16 } }
    };

    fixture_init(&fixture);
    fixture.picture_screen.AddTraps(&fixture.picture, 0, 0,
                                    ARRAY_SIZE(traps), traps);
    assert(RegionContainsPoint(DamageRegion(fixture.damage), 2, 2, NULL));
    assert(RegionContainsPoint(DamageRegion(fixture.damage), 22, 22, NULL));
    fixture_fini(&fixture);
}

static void
damage_shared_region(void)
{
    const DamageReportLevel levels[] = {
        DamageReportBoundingBox, DamageReportNonEmpty
    };
    BoxRec boxes[] = { { 1, 1, 3, 3 }, { 20, 20, 24, 24 } };
    struct damage_fixture fixture;
    RegionRec region, expected;
    unsigned calls = 0;

    fixture_init(&fixture);
    assert(RegionInitBoxes(&expected, boxes, ARRAY_SIZE(boxes)));
    for (unsigned i = 0; i < ARRAY_SIZE(levels); i++) {
        DamagePtr first = DamageCreate(report_damage, NULL, levels[i], FALSE,
                                       &fixture.screen, &calls);

        assert(first);
        DamageRegister(&fixture.pixmap->drawable, first);
        assert(RegionInitBoxes(&region, boxes, ARRAY_SIZE(boxes)));
        DamageRegionAppend(&fixture.pixmap->drawable, &region);
        assert(RegionEqual(&region, &expected));
        assert(RegionEqual(DamageRegion(fixture.damage), &expected));
        assert(RegionContainsPoint(DamageRegion(first), 10, 10, NULL));
        DamageDestroy(first);
        DamageEmpty(fixture.damage);
        RegionUninit(&region);
    }
    RegionUninit(&expected);
    fixture_fini(&fixture);
}

static void
damage_rectangle_batch(void)
{
    struct damage_fixture fixture;
    xRectangle rectangles[] = {
        { 5, 5, 20, 20 }, { 15, 15, 30, 30 },
        { 50, 50, 1, 1 }, { 70, 70, 0, 0 }, { -5, -5, 10, 10 }
    };
    const unsigned widths[] = { 0, 1, 2, 7 };
    GCPtr gc;
    RegionRec expected;

    fixture_init(&fixture);
    gc = fixture_gc(&fixture);
    for (unsigned w = 0; w < ARRAY_SIZE(widths); w++) {
        gc->lineWidth = widths[w];
        for (unsigned i = 0; i < ARRAY_SIZE(rectangles); i++)
            gc->ops->PolyRectangle(&fixture.pixmap->drawable, gc, 1, &rectangles[i]);
        RegionNull(&expected);
        assert(RegionCopy(&expected, DamageRegion(fixture.damage)));
        DamageEmpty(fixture.damage);
        gc->ops->PolyRectangle(&fixture.pixmap->drawable, gc,
                               ARRAY_SIZE(rectangles), rectangles);
        assert(RegionEqual(DamageRegion(fixture.damage), &expected));
        assert(!RegionContainsPoint(&expected, 10, 10, NULL));
        RegionUninit(&expected);
        DamageEmpty(fixture.damage);
    }
    dixFreeObjectWithPrivates(gc, PRIVATE_GC);
    fixture_fini(&fixture);
}

struct callback_state {
    struct damage_fixture *fixture;
    DamagePtr victim;
    Bool append;
    unsigned calls;
};

static void
mutate_listener(DamagePtr damage, RegionPtr region, void *closure)
{
    struct callback_state *state = closure;

    if (state->calls++ == 0) {
        if (state->victim) {
            DamageDestroy(state->victim);
            state->victim = NULL;
        }
        if (state->append) {
            BoxRec box = { 40, 40, 45, 45 };
            RegionRec added;

            RegionInit(&added, &box, 1);
            DamageRegionAppend(&state->fixture->pixmap->drawable, &added);
            RegionUninit(&added);
        }
        else
            DamageDestroy(damage);
    }
}

static void
damage_callback_removal(void)
{
    for (unsigned after = 0; after < 2; after++) {
        struct damage_fixture fixture;
        struct callback_state state = { 0 };
        DamagePtr listener;
        BoxRec box = { 1, 1, 3, 3 };
        RegionRec region;

        fixture_init(&fixture);
        state.fixture = &fixture;
        state.victim = DamageCreate(report_damage, NULL, DamageReportRawRegion,
                                    TRUE, &fixture.screen, &fixture.calls);
        assert(state.victim);
        DamageRegister(&fixture.pixmap->drawable, state.victim);
        DamageSetReportAfterOp(state.victim, after);
        listener = DamageCreate(mutate_listener, NULL, DamageReportRawRegion,
                                 TRUE, &fixture.screen, &state);
        assert(listener);
        DamageRegister(&fixture.pixmap->drawable, listener);
        DamageSetReportAfterOp(listener, after);
        RegionInit(&region, &box, 1);
        DamageDamageRegion(&fixture.pixmap->drawable, &region);
        assert(state.calls == 1);
        assert(!state.victim);
        assert(fixture.calls == 1);
        RegionUninit(&region);
        fixture_fini(&fixture);
    }
}

static void
damage_callback_append(void)
{
    struct damage_fixture fixture;
    struct callback_state state = { .append = TRUE };
    DamagePtr listener;
    BoxRec box = { 1, 1, 3, 3 };
    RegionRec region;

    fixture_init(&fixture);
    state.fixture = &fixture;
    listener = DamageCreate(mutate_listener, NULL, DamageReportRawRegion,
                             TRUE, &fixture.screen, &state);
    assert(listener);
    DamageRegister(&fixture.pixmap->drawable, listener);
    DamageSetReportAfterOp(listener, TRUE);
    RegionInit(&region, &box, 1);
    DamageDamageRegion(&fixture.pixmap->drawable, &region);
    assert(RegionContainsPoint(DamagePendingRegion(listener), 42, 42, NULL));
    DamageRegionProcessPending(&fixture.pixmap->drawable);
    assert(!RegionNotEmpty(DamagePendingRegion(listener)));
    assert(RegionContainsPoint(DamageRegion(listener), 42, 42, NULL));
    DamageRegionProcessPending(&fixture.pixmap->drawable);
    assert(state.calls == 3);
    DamageDestroy(listener);
    RegionUninit(&region);
    fixture_fini(&fixture);
}

static void
nested_report(DamagePtr damage, RegionPtr region, void *closure)
{
    struct callback_state *state = closure;

    if (state->calls++ == 0) {
        BoxRec box = { 40, 40, 45, 45 };
        RegionRec added;

        RegionInit(&added, &box, 1);
        DamageRegionAppend(&state->fixture->pixmap->drawable, &added);
        RegionUninit(&added);
    }
    else {
        DamageDestroy(state->victim);
        state->victim = NULL;
        DamageDestroy(damage);
    }
}

static void
damage_nested_callback_removal(void)
{
    struct damage_fixture fixture;
    struct callback_state state = { 0 };
    DamagePtr listener;
    BoxRec box = { 1, 1, 3, 3 };
    RegionRec region;

    fixture_init(&fixture);
    state.fixture = &fixture;
    state.victim = DamageCreate(report_damage, NULL, DamageReportRawRegion,
                                TRUE, &fixture.screen, &fixture.calls);
    assert(state.victim);
    DamageRegister(&fixture.pixmap->drawable, state.victim);
    listener = DamageCreate(nested_report, NULL, DamageReportRawRegion,
                             TRUE, &fixture.screen, &state);
    assert(listener);
    DamageRegister(&fixture.pixmap->drawable, listener);
    RegionInit(&region, &box, 1);
    DamageDamageRegion(&fixture.pixmap->drawable, &region);
    assert(state.calls == 2);
    assert(!state.victim);
    assert(fixture.calls == 2);
    RegionUninit(&region);
    fixture_fini(&fixture);
}

static void
damage_close_without_render(void)
{
    ScreenRec screen = { 0 };

    assert(dixAllocatePrivates(&screen.devPrivates, PRIVATE_SCREEN));
    screenInfo.numScreens = 1;
    screenInfo.screens[0] = &screen;
    screen.CloseScreen = close_screen;
    screen.DestroyWindow = destroy_window;
    screen.SetWindowPixmap = set_window_pixmap;
    assert(DamageSetup(&screen));
    assert(DamageSetup(&screen));
    assert(screen.CloseScreen(&screen));
    dixFreePrivates(screen.devPrivates, PRIVATE_SCREEN);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    dixResetPrivates();
}

const testfunc_t *
damage_test(void)
{
    static const testfunc_t tests[] = {
        damage_multiple_traps,
        damage_shared_region,
        damage_rectangle_batch,
        damage_callback_removal,
        damage_callback_append,
        damage_nested_callback_removal,
        damage_close_without_render,
        NULL
    };

    return tests;
}

#ifdef DAMAGE_BENCHMARK
#include <stdio.h>
#include <time.h>

#define DAMAGE_BENCH_RECTS 1024
#define DAMAGE_BENCH_EDGES 200000

int
main(void)
{
    const unsigned sizes[] = { 1, 16, 128, DAMAGE_BENCH_RECTS };
    struct damage_fixture fixture;
    xRectangle rectangles[DAMAGE_BENCH_RECTS];
    GCPtr gc;

    fixture_init(&fixture);
    gc = fixture_gc(&fixture);
    for (unsigned i = 0; i < ARRAY_SIZE(rectangles); i++) {
        rectangles[i] = (xRectangle) { i % 100, (i * 7) % 100, 20, 20 };
    }
    for (unsigned s = 0; s < ARRAY_SIZE(sizes); s++) {
        struct timespec start, end;
        unsigned repetitions = DAMAGE_BENCH_EDGES / sizes[s];

        clock_gettime(CLOCK_MONOTONIC, &start);
        for (unsigned i = 0; i < repetitions; i++) {
            gc->ops->PolyRectangle(&fixture.pixmap->drawable, gc,
                                   sizes[s], rectangles);
            DamageEmpty(fixture.damage);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("%u rectangles: %.0f ns/op\n", sizes[s],
               ((end.tv_sec - start.tv_sec) * 1e9 + end.tv_nsec - start.tv_nsec) /
               repetitions);
    }
    dixFreeObjectWithPrivates(gc, PRIVATE_GC);
    fixture_fini(&fixture);
    return 0;
}
#endif
