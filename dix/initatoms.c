/* THIS IS A GENERATED FILE
 *
 * Do not change!  Changing this file implies a protocol change!
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>
#include <X11/Xatom.h>
#include "misc.h"
#include "dix.h"

#define ATOM(x) { #x, sizeof(#x) - 1, XA_##x }

static const struct AtomDef {
    const char *name;
    unsigned len;
    Atom atom;
} atomDefs[] = {
    ATOM(PRIMARY),
    ATOM(SECONDARY),
    ATOM(ARC),
    ATOM(ATOM),
    ATOM(BITMAP),
    ATOM(CARDINAL),
    ATOM(COLORMAP),
    ATOM(CURSOR),
    ATOM(CUT_BUFFER0),
    ATOM(CUT_BUFFER1),
    ATOM(CUT_BUFFER2),
    ATOM(CUT_BUFFER3),
    ATOM(CUT_BUFFER4),
    ATOM(CUT_BUFFER5),
    ATOM(CUT_BUFFER6),
    ATOM(CUT_BUFFER7),
    ATOM(DRAWABLE),
    ATOM(FONT),
    ATOM(INTEGER),
    ATOM(PIXMAP),
    ATOM(POINT),
    ATOM(RECTANGLE),
    ATOM(RESOURCE_MANAGER),
    ATOM(RGB_COLOR_MAP),
    ATOM(RGB_BEST_MAP),
    ATOM(RGB_BLUE_MAP),
    ATOM(RGB_DEFAULT_MAP),
    ATOM(RGB_GRAY_MAP),
    ATOM(RGB_GREEN_MAP),
    ATOM(RGB_RED_MAP),
    ATOM(STRING),
    ATOM(VISUALID),
    ATOM(WINDOW),
    ATOM(WM_COMMAND),
    ATOM(WM_HINTS),
    ATOM(WM_CLIENT_MACHINE),
    ATOM(WM_ICON_NAME),
    ATOM(WM_ICON_SIZE),
    ATOM(WM_NAME),
    ATOM(WM_NORMAL_HINTS),
    ATOM(WM_SIZE_HINTS),
    ATOM(WM_ZOOM_HINTS),
    ATOM(MIN_SPACE),
    ATOM(NORM_SPACE),
    ATOM(MAX_SPACE),
    ATOM(END_SPACE),
    ATOM(SUPERSCRIPT_X),
    ATOM(SUPERSCRIPT_Y),
    ATOM(SUBSCRIPT_X),
    ATOM(SUBSCRIPT_Y),
    ATOM(UNDERLINE_POSITION),
    ATOM(UNDERLINE_THICKNESS),
    ATOM(STRIKEOUT_ASCENT),
    ATOM(STRIKEOUT_DESCENT),
    ATOM(ITALIC_ANGLE),
    ATOM(X_HEIGHT),
    ATOM(QUAD_WIDTH),
    ATOM(WEIGHT),
    ATOM(POINT_SIZE),
    ATOM(RESOLUTION),
    ATOM(COPYRIGHT),
    ATOM(NOTICE),
    ATOM(FONT_NAME),
    ATOM(FAMILY_NAME),
    ATOM(FULL_NAME),
    ATOM(CAP_HEIGHT),
    ATOM(WM_CLASS),
    ATOM(WM_TRANSIENT_FOR),
};

void
MakePredeclaredAtoms(void)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(atomDefs); i++)
        if (MakeAtom(atomDefs[i].name, atomDefs[i].len, 1) != atomDefs[i].atom)
            AtomError();
}
