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
close_screen(ScreenPtr screen)
{
    return TRUE;
}

static void
add_traps(PicturePtr picture, INT16 x, INT16 y, int count, xTrap *traps)
{
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
    dixFreePrivates(fixture->screen.devPrivates, PRIVATE_SCREEN);
    screenInfo.numScreens = 0;
    screenInfo.screens[0] = NULL;
    dixResetPrivates();
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

const testfunc_t *
damage_test(void)
{
    static const testfunc_t tests[] = {
        damage_multiple_traps,
        damage_shared_region,
        NULL
    };

    return tests;
}
