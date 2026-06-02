/**
 * Spaghetti Display Server
 * Copyright (C) 2026  SpaghettiFork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
/* EGL headers are included for type definitions only – all symbols are
 * reached through function pointers obtained via dlopen/dlsym. */
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <drm_fourcc.h>

#define _XF86DRI_SERVER_
#include <xf86.h>
#include <dri2.h>

#include "glxserver.h"
#include "glxutil.h"
#include "opaque.h"

#include <X11/Xosdefs.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* Can't get these from <GL/glx.h> since it pulls in client headers */
#define GLX_RGBA_BIT                0x00000001
#define GLX_WINDOW_BIT              0x00000001
#define GLX_PIXMAP_BIT              0x00000002
#define GLX_PBUFFER_BIT             0x00000004
#define GLX_NONE                    0x8000
#define GLX_SLOW_CONFIG             0x8001
#define GLX_TRUE_COLOR              0x8002
#define GLX_DIRECT_COLOR            0x8003
#define GLX_STATIC_GRAY             0x8007
#define GLX_TRANSPARENT_RGB         0x8008
#define GLX_NON_CONFORMANT_CONFIG   0x800D
#define GLX_RGBA_FLOAT_BIT_ARB      0x00000004
#define GLX_SWAP_UNDEFINED_OML      0x8063
#define GLX_TEXTURE_1D_BIT_EXT        0x00000001
#define GLX_TEXTURE_2D_BIT_EXT        0x00000002
#define GLX_TEXTURE_RECTANGLE_BIT_EXT 0x00000004

#ifndef EGL_PLATFORM_DEVICE_EXT
#define EGL_PLATFORM_DEVICE_EXT     0x313F
#endif
#ifndef EGL_DRM_DEVICE_FILE_EXT
#define EGL_DRM_DEVICE_FILE_EXT     0x3233
#endif

#ifndef fourcc_code
#define fourcc_code(a,b,c,d) \
    ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
#endif

#ifndef DRM_FORMAT_XRGB1555
#define DRM_FORMAT_XRGB1555 fourcc_code('X','R','1','5')
#endif
#ifndef DRM_FORMAT_XBGR1555
#define DRM_FORMAT_XBGR1555 fourcc_code('X','B','1','5')
#endif
#ifndef DRM_FORMAT_XRGB4444
#define DRM_FORMAT_XRGB4444 fourcc_code('X','R','1','2')
#endif
#ifndef DRM_FORMAT_XBGR4444
#define DRM_FORMAT_XBGR4444 fourcc_code('X','B','1','2')
#endif

/* Allowed colour FOURCCs that we are prepared to expose. */
static const uint32_t basil_color_fourccs[] = {
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_XBGR8888,
    DRM_FORMAT_BGRX8888,
    DRM_FORMAT_BGRA8888,
    DRM_FORMAT_RGBX8888,
    DRM_FORMAT_RGBA8888,
    DRM_FORMAT_ARGB2101010,
    DRM_FORMAT_XRGB2101010,
    DRM_FORMAT_ABGR2101010,
    DRM_FORMAT_XBGR2101010,
    DRM_FORMAT_RGB565,
    DRM_FORMAT_ARGB1555,
    DRM_FORMAT_XRGB1555,
    DRM_FORMAT_ABGR1555,
    DRM_FORMAT_XBGR1555,
    DRM_FORMAT_ARGB4444,
    DRM_FORMAT_XRGB4444,
    DRM_FORMAT_ABGR4444,
    DRM_FORMAT_XBGR4444,
};

/* Allowed depth/stencil combinations. */
static const struct {
    int depth;
    int stencil;
} basil_zs_combos[] = {
    {  0, 0 },  /* PIPE_FORMAT_NONE          */
    { 16, 0 },  /* PIPE_FORMAT_Z16_UNORM     */
    { 24, 0 },  /* PIPE_FORMAT_Z24X8_UNORM   */
    { 24, 8 },  /* PIPE_FORMAT_Z24_UNORM_S8  */
    {  0, 8 },  /* PIPE_FORMAT_S8_UINT       */
};

static Bool
fourcc_in_table(uint32_t fourcc)
{
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(basil_color_fourccs); i++)
        if (basil_color_fourccs[i] == fourcc)
            return TRUE;
    return FALSE;
}

static Bool
zs_in_table(int depth, int stencil)
{
    unsigned i;
    for (i = 0; i < ARRAY_SIZE(basil_zs_combos); i++)
        if (basil_zs_combos[i].depth   == depth &&
            basil_zs_combos[i].stencil == stencil)
            return TRUE;
    return FALSE;
}

struct basil_egl {
    void        *lib;   /* dlopen("libEGL.so.1", ...) handle */

    void *      (*GetProcAddress)(const char *procname);

    EGLDisplay  (*GetPlatformDisplayEXT)(EGLenum platform,
                                         void *native_display,
                                         const EGLAttrib *attrib_list);
    EGLDisplay  (*GetDisplay)(EGLNativeDisplayType display_id);
    EGLBoolean  (*Initialize)(EGLDisplay dpy, EGLint *major, EGLint *minor);
    EGLBoolean  (*Terminate)(EGLDisplay dpy);
    const char *(*QueryString)(EGLDisplay dpy, EGLint name);
    EGLBoolean  (*GetConfigs)(EGLDisplay dpy, EGLConfig *configs,
                              EGLint config_size, EGLint *num_config);
    EGLBoolean  (*GetConfigAttrib)(EGLDisplay dpy, EGLConfig config,
                                   EGLint attribute, EGLint *value);

    EGLBoolean  (*QueryDevicesEXT)(EGLint max_devices, EGLDeviceEXT *devices,
                                   EGLint *num_devices);
    const char *(*QueryDeviceStringEXT)(EGLDeviceEXT device, EGLint name);
};

static Bool
basil_egl_open(struct basil_egl *egl)
{
    memset(egl, 0, sizeof *egl);

    egl->lib = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!egl->lib)
        return FALSE;

    egl->GetProcAddress = dlsym(egl->lib, "eglGetProcAddress");
    if (!egl->GetProcAddress) {
        dlclose(egl->lib);
        egl->lib = NULL;
        return FALSE;
    }

#define PROC(name) \
    egl->name = (typeof(egl->name)) egl->GetProcAddress("egl" #name); \
    if (!egl->name) { \
        LogMessage(X_ERROR, "AIGLX: egl" #name " not found in libEGL\n"); \
        dlclose(egl->lib); \
        egl->lib = NULL; \
        return FALSE; \
    }

    PROC(GetPlatformDisplayEXT);
    PROC(GetDisplay);
    PROC(Initialize);
    PROC(Terminate);
    PROC(QueryString);
    PROC(GetConfigs);
    PROC(GetConfigAttrib);

#undef PROC

    egl->QueryDevicesEXT = (typeof(egl->QueryDevicesEXT))
        egl->GetProcAddress("eglQueryDevicesEXT");
    egl->QueryDeviceStringEXT = (typeof(egl->QueryDeviceStringEXT))
        egl->GetProcAddress("eglQueryDeviceStringEXT");

    return TRUE;
}

static void
basil_egl_close(struct basil_egl *egl)
{
    if (egl->lib) {
        dlclose(egl->lib);
        egl->lib = NULL;
    }
}
struct basil_screen {
    __GLXscreen       base;
    struct basil_egl  egl;
    EGLDisplay        display;
    int               fd;
};

/*
 * Enumerate EGL devices and return a platform-device EGLDisplay whose
 * EGL_DRM_DEVICE_FILE_EXT matches the symlink target of /proc/self/fd/<fd>
 */
static EGLDisplay
basil_device_display(struct basil_egl *egl, int fd)
{
    EGLDeviceEXT *devices = NULL;
    EGLint ndevices = 0;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    char fd_path[32], dev_real[PATH_MAX];
    ssize_t len;
    int i;

    if (!egl->QueryDevicesEXT || !egl->QueryDeviceStringEXT)
        return EGL_NO_DISPLAY;

    if (!egl->QueryDevicesEXT(0, NULL, &ndevices) || ndevices <= 0)
        return EGL_NO_DISPLAY;

    devices = calloc(ndevices, sizeof *devices);
    if (!devices)
        return EGL_NO_DISPLAY;

    if (!egl->QueryDevicesEXT(ndevices, devices, &ndevices))
        goto out;

    snprintf(fd_path, sizeof fd_path, "/proc/self/fd/%d", fd);
    len = readlink(fd_path, dev_real, sizeof dev_real - 1);
    if (len <= 0) {
        goto out;
    }

    dev_real[len] = '\0';

    for (i = 0; i < ndevices; i++) {
        const char *dev_file =
            egl->QueryDeviceStringEXT(devices[i], EGL_DRM_DEVICE_FILE_EXT);

        if (dev_file && strcmp(dev_file, dev_real) == 0) {
            dpy = egl->GetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,
                                              devices[i], NULL);
            break;
        }
    }

out:
    free(devices);
    return dpy;
}

static const struct {
    uint8_t  r, g, b, a;
    uint32_t fourcc;
} basil_fourcc_size_map[] = {
    {  8,  8,  8,  8, DRM_FORMAT_ARGB8888    },
    {  8,  8,  8,  0, DRM_FORMAT_XRGB8888    },
    { 10, 10, 10,  2, DRM_FORMAT_ARGB2101010 },
    { 10, 10, 10,  0, DRM_FORMAT_XRGB2101010 },
    {  5,  6,  5,  0, DRM_FORMAT_RGB565      },
    {  5,  5,  5,  1, DRM_FORMAT_ARGB1555    },
    {  5,  5,  5,  0, DRM_FORMAT_XRGB1555    },
    {  4,  4,  4,  4, DRM_FORMAT_ARGB4444    },
    {  4,  4,  4,  0, DRM_FORMAT_XRGB4444    },
};

static uint32_t
get_config_fourcc(const struct basil_egl *egl, EGLDisplay dpy, EGLConfig cfg)
{
    EGLint r = 0, g = 0, b = 0, a = 0;
    unsigned i;

    egl->GetConfigAttrib(dpy, cfg, EGL_RED_SIZE,   &r);
    egl->GetConfigAttrib(dpy, cfg, EGL_GREEN_SIZE, &g);
    egl->GetConfigAttrib(dpy, cfg, EGL_BLUE_SIZE,  &b);
    egl->GetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &a);

    for (i = 0; i < ARRAY_SIZE(basil_fourcc_size_map); i++)
        if (basil_fourcc_size_map[i].r == r &&
            basil_fourcc_size_map[i].g == g &&
            basil_fourcc_size_map[i].b == b &&
            basil_fourcc_size_map[i].a == a)
            return basil_fourcc_size_map[i].fourcc;

    return 0;
}

static const char *
basil_get_glvnd_vendor(const struct basil_egl *egl, EGLDisplay dpy, int fd, const char *exts)
{
    if (exts && strstr(exts, "EGL_MESA_query_driver")) {
        return "mesa";
    }

    if (fd >= 0) {
        drmVersionPtr ver = drmGetVersion(fd);
        if (ver) {
            /* XXX: This should be a lookup table for fallback... */
            const char *vendor =
                (strcmp(ver->name, "nvidia") == 0) ? "nvidia" : "mesa";
            drmFreeVersion(ver);
            return vendor;
        }
    }

    return "mesa";
}

static __GLXcontext *
egl_create_context(__GLXscreen *screen, __GLXconfig *modes,
                   __GLXcontext *shareContext, unsigned num_attribs,
                   const uint32_t *attribs, int *error)
{
    *error = BadImplementation;
    return NULL;
}

static void
egl_drawable_destroy(__GLXdrawable *draw)
{
    free(draw);
}

static GLboolean
egl_drawable_swap_buffers(ClientPtr client, __GLXdrawable *draw)
{
    return GL_TRUE;
}

static void
egl_drawable_copy_sub_buffer(__GLXdrawable *draw, int x, int y, int w, int h)
{
}

static __GLXdrawable *
egl_create_glx_drawable(ClientPtr client, __GLXscreen *screen,
                        DrawablePtr draw, XID drawid, int type,
                        XID glxdrawid, __GLXconfig *modes)
{
    __GLXdrawable *ret;

    ret = calloc(1, sizeof(__GLXdrawable));
    if (!ret)
        return NULL;

    if (!__glXDrawableInit(ret, screen, draw, type, glxdrawid, modes)) {
        free(ret);
        return NULL;
    }

    ret->destroy = egl_drawable_destroy;
    ret->swapBuffers = egl_drawable_swap_buffers;
    ret->copySubBuffer = egl_drawable_copy_sub_buffer;

    return ret;
}

struct egl_config_meta
{
    uint32_t fourcc;

    int  depth;
    int  stencil;

    Bool srgb_only;
    Bool direct_color;
    Bool double_buffer;
    Bool duplicate_for_composite;
};

struct channel_shifts
{
    int red, green, blue, alpha;
};

static struct channel_shifts
fourcc_to_shifts(uint32_t fourcc)
{
    switch (fourcc) {
    /* 32-bit: [31:0] A:R:G:B */
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
        return (struct channel_shifts){ 16,  8,  0, 24 };
    /* 32-bit: [31:0] A:B:G:R */
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
        return (struct channel_shifts){  0,  8, 16, 24 };
    /* 32-bit: [31:0] B:G:R:A */
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX8888:
        return (struct channel_shifts){  8, 16, 24,  0 };
    /* 32-bit: [31:0] R:G:B:A */
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX8888:
        return (struct channel_shifts){ 24, 16,  8,  0 };
    /* 30-bit: [31:0] A:R:G:B 2:10:10:10 */
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_XRGB2101010:
        return (struct channel_shifts){ 20, 10,  0, 30 };
    /* 30-bit: [31:0] A:B:G:R 2:10:10:10 */
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
        return (struct channel_shifts){  0, 10, 20, 30 };
    /* 16-bit: [15:0] R:G:B 5:6:5 */
    case DRM_FORMAT_RGB565:
        return (struct channel_shifts){ 11,  5,  0,  0 };
    /* 16-bit: [15:0] A:R:G:B 1:5:5:5 */
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_XRGB1555:
        return (struct channel_shifts){ 10,  5,  0, 15 };
    /* 16-bit: [15:0] A:B:G:R 1:5:5:5 */
    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_XBGR1555:
        return (struct channel_shifts){  0,  5, 10, 15 };
    /* 16-bit: [15:0] A:R:G:B 4:4:4:4 */
    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_XRGB4444:
        return (struct channel_shifts){  8,  4,  0, 12 };
    /* 16-bit: [15:0] A:B:G:R 4:4:4:4 */
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_XBGR4444:
        return (struct channel_shifts){  0,  4,  8, 12 };
    default:
        return (struct channel_shifts){  0,  0,  0,  0 };
    }
}

static struct __GLXconfig *
eglConfigToGLXConfig(const struct basil_egl *egl,
                     EGLDisplay disp,
                     struct egl_config_meta meta,
                     EGLConfig egl_config,
                     struct __GLXconfig *chain)
{
    EGLint value = 0;
    EGLint surface_type = 0;

    __GLXconfig *glx_config = calloc(1, sizeof(__GLXconfig));
    if (!glx_config)
        return chain;

    egl->GetConfigAttrib(disp, egl_config, EGL_SURFACE_TYPE, &surface_type);
    egl->GetConfigAttrib(disp, egl_config, EGL_LEVEL,        &glx_config->level);
    egl->GetConfigAttrib(disp, egl_config, EGL_RED_SIZE,     &glx_config->redBits);
    egl->GetConfigAttrib(disp, egl_config, EGL_GREEN_SIZE,   &glx_config->greenBits);
    egl->GetConfigAttrib(disp, egl_config, EGL_BLUE_SIZE,    &glx_config->blueBits);
    egl->GetConfigAttrib(disp, egl_config, EGL_ALPHA_SIZE,   &glx_config->alphaBits);
    egl->GetConfigAttrib(disp, egl_config, EGL_BUFFER_SIZE,  &glx_config->rgbBits);

    /* Derived state: sRGB.
     * 
     * EGL doesn't put this in the FBconfig at all,
     * it's a property of the surface specified at creation time, so we have
     * to infer it from the GL's extensions.
     * 
     * Only makes sense at 8bpc though as applications assume this is only supported. */
    if (meta.srgb_only) {
        if (glx_config->redBits == 8) {
            glx_config->sRGBCapable = GL_TRUE;
        } else {
            free(glx_config);
            return chain;
        }
    }

#define TRANSLATE_TO_EQUIVALENT(EGL, GLX) \
    if ((surface_type & EGL)) glx_config->drawableType |= GLX;

    /* Mesa returns only EGL_PBUFFER_BIT for this backend;
     * assume support for the other surface types too. */
    if (!(surface_type & EGL_WINDOW_BIT))
        surface_type = EGL_WINDOW_BIT | EGL_PIXMAP_BIT | EGL_PBUFFER_BIT;

    TRANSLATE_TO_EQUIVALENT(EGL_WINDOW_BIT,  GLX_WINDOW_BIT);
    TRANSLATE_TO_EQUIVALENT(EGL_PBUFFER_BIT, GLX_PBUFFER_BIT);
    TRANSLATE_TO_EQUIVALENT(EGL_PIXMAP_BIT,  GLX_PIXMAP_BIT);

#undef TRANSLATE_TO_EQUIVALENT

    struct channel_shifts s = fourcc_to_shifts(meta.fourcc);

    glx_config->redMask   = ((1u << glx_config->redBits)   - 1u) << s.red;
    glx_config->greenMask = ((1u << glx_config->greenBits) - 1u) << s.green;
    glx_config->blueMask  = ((1u << glx_config->blueBits)  - 1u) << s.blue;
    
    if (glx_config->alphaBits)
        glx_config->alphaMask = ((1u << glx_config->alphaBits) - 1u) << s.alpha;

    /* GLX conformance failure: there's no such thing as accumulation
     * buffers in EGL.  They could be emulated with shaders and FBOs,
     * but I'm pretty sure nobody's using this feature since it's
     * entirely software.  Note that GLX conformance merely requires
     * that an accum buffer _exist_, not a minimum bitness. */
    glx_config->accumRedBits   = 0;
    glx_config->accumGreenBits = 0;
    glx_config->accumBlueBits  = 0;
    glx_config->accumAlphaBits = 0;

    /* We already probed these. */
    glx_config->depthBits   = meta.depth;
    glx_config->stencilBits = meta.stencil;

    if (meta.direct_color)
        glx_config->visualType = GLX_DIRECT_COLOR;
    else
        glx_config->visualType = GLX_TRUE_COLOR;

    /* This is duplicated. */
    glx_config->doubleBufferMode = meta.double_buffer;

    egl->GetConfigAttrib(disp, egl_config, EGL_CONFIG_CAVEAT, &value);

    if (value == EGL_NONE)
        glx_config->visualRating = GLX_NONE;
    else if (value == EGL_SLOW_CONFIG)
        glx_config->visualRating = GLX_SLOW_CONFIG;
    else if (value == EGL_NON_CONFORMANT_CONFIG)
        glx_config->visualRating = GLX_NON_CONFORMANT_CONFIG;

    /* Only query transparent channels for EGL_TRANSPARENT_RGB. */
    egl->GetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_TYPE, &value);

    if (value == EGL_TRANSPARENT_RGB) {
        glx_config->transparentPixel = GLX_TRANSPARENT_RGB;
        egl->GetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_RED_VALUE,
                             &glx_config->transparentRed);
        egl->GetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_GREEN_VALUE,
                             &glx_config->transparentGreen);
        egl->GetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_BLUE_VALUE,
                             &glx_config->transparentBlue);
    } else {
        glx_config->transparentPixel = GLX_NONE;
    }

    egl->GetConfigAttrib(disp, egl_config, EGL_SAMPLE_BUFFERS,
                         &glx_config->sampleBuffers);
    egl->GetConfigAttrib(disp, egl_config, EGL_SAMPLES, &glx_config->samples);

    if (egl->GetConfigAttrib(disp, egl_config,
                             EGL_COLOR_COMPONENT_TYPE_EXT,
                             &value) == EGL_TRUE) {
        if (value == EGL_COLOR_COMPONENT_TYPE_FIXED_EXT) {
            glx_config->renderType = GLX_RGBA_BIT;
        } else if (value == EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT) {
            glx_config->renderType = GLX_RGBA_FLOAT_BIT_ARB;
        } else {
            LogMessage(X_WARNING,
                       "Unknown EGL_COLOR_COMPONENT_TYPE_EXT value of 0x%08x\n", value);
            free(glx_config);
            return chain;
        }
    } else {
        /* Assume GLX_RGBA_BIT if the eglGetConfigAttrib call failed. */
        glx_config->renderType = GLX_RGBA_BIT;
    }

    if (surface_type & EGL_PBUFFER_BIT) {
        egl->GetConfigAttrib(disp, egl_config, EGL_MAX_PBUFFER_WIDTH,
                             &glx_config->maxPbufferWidth);
        egl->GetConfigAttrib(disp, egl_config, EGL_MAX_PBUFFER_HEIGHT,
                             &glx_config->maxPbufferHeight);
        egl->GetConfigAttrib(disp, egl_config, EGL_MAX_PBUFFER_PIXELS, &value);

        if (value) {
            glx_config->maxPbufferPixels = value;
        } else {
            /* Mesa seems to ignore EGL_MAX_PBUFFER_PIXELS... */
            glx_config->maxPbufferPixels =
                glx_config->maxPbufferWidth * glx_config->maxPbufferHeight;
        }
    }

    /* EGL doesn't have an equivalent for OML_swap_method. */
    glx_config->swapMethod = GLX_SWAP_UNDEFINED_OML;

    /* EXT_texture_from_pixmap is an absolute nightmare,
     * just lie that both variants are supported unconditionally.
     *
     * 1. Mesa returns EGL_FALSE for both EGL queries through GBM.
     * 2. NVIDIA drivers also return EGL_FALSE when doing queries.
     * 3. Configs without bindToTextureRgba don't get exposed at all. */
    glx_config->bindToTextureRgb  = GL_TRUE;
    glx_config->bindToTextureRgba = GL_TRUE;

    /* No EGL equivalent, pretend we support everything. */
    glx_config->bindToTextureTargets = 
        GLX_TEXTURE_1D_BIT_EXT | GLX_TEXTURE_2D_BIT_EXT | GLX_TEXTURE_RECTANGLE_BIT_EXT;

#ifdef COMPOSITE
    /* Here we decide which FBconfigs will be duplicated for compositing.
     * FBconfigs marked with duplicatedForComp will be reserved for
     * compositing visuals.
     *
     * It might look strange to do this decision this late when translation
     * from an EGLConfig is already done, but using the EGLConfig accessor
     * functions becomes worse both with respect to code complexity and CPU
     * usage. */
    if (meta.duplicate_for_composite &&
        (glx_config->renderType == GLX_RGBA_FLOAT_BIT_ARB ||
         glx_config->rgbBits != 32 ||
         glx_config->redBits != 8 ||
         glx_config->greenBits != 8 ||
         glx_config->blueBits != 8 ||
         glx_config->visualRating != GLX_NONE ||
         glx_config->sampleBuffers != 0)) {
        free(glx_config);
        return chain;
    }

    glx_config->duplicatedForComp = meta.duplicate_for_composite;
#endif

    glx_config->next = chain;
    return glx_config;
}

static __GLXconfig *
egl_setup_configs(struct basil_screen *priv, const char* exts)
{
    const struct basil_egl *egl = &priv->egl;
    EGLDisplay dpy = priv->display;
    EGLConfig *host_configs = NULL;
    struct __GLXconfig *c = NULL;
    EGLint nconfigs = 0;
    int i, j, k;

    Bool can_srgb = exts && strstr(exts, "EGL_KHR_gl_colorspace");

    egl->GetConfigs(dpy, NULL, 0, &nconfigs);

    host_configs = calloc(nconfigs, sizeof(EGLConfig));
    if (!host_configs)
        return NULL;

    egl->GetConfigs(dpy, host_configs, nconfigs, &nconfigs);

    for (i = nconfigs - 1; i >= 0; i--) {
        EGLint depth = 0, stencil = 0;
        uint32_t fourcc;

        fourcc = get_config_fourcc(egl, dpy, host_configs[i]);
        if (!fourcc_in_table(fourcc))
            continue;

        egl->GetConfigAttrib(dpy, host_configs[i], EGL_DEPTH_SIZE,   &depth);
        egl->GetConfigAttrib(dpy, host_configs[i], EGL_STENCIL_SIZE, &stencil);

        if (!zs_in_table(depth, stencil))
            continue;

        for (j = 0; j < 3; j++) /* 0=tc+comp, 1=dc, 2=tc */ {
            for (k = 0; k < 2; k++) /* double_buffer */ {
                if (can_srgb) {
                    c = eglConfigToGLXConfig(egl, dpy,
                                             (struct egl_config_meta) {
                                                 .fourcc                  = fourcc,
                                                 .depth                   = depth,
                                                 .stencil                 = stencil,
                                                 .direct_color            = (j == 1),
                                                 .double_buffer           = (k > 0),
                                                 .duplicate_for_composite = (j == 0),
                                                 .srgb_only               = TRUE,
                                             },
                                             host_configs[i], c);
                }

                c = eglConfigToGLXConfig(egl, dpy,
                                         (struct egl_config_meta) {
                                             .fourcc                  = fourcc,
                                             .depth                   = depth,
                                             .stencil                 = stencil,
                                             .direct_color            = (j == 1),
                                             .double_buffer           = (k > 0),
                                             .duplicate_for_composite = (j == 0),
                                             .srgb_only               = FALSE,
                                         },
                                         host_configs[i], c);
            }
        }
    }

    free(host_configs);
    return c;
}

static void
basil_screen_destroy(__GLXscreen *base)
{
    struct basil_screen *screen = (struct basil_screen *) base;

    screen->egl.Terminate(screen->display);

    if (screen->fd >= 0)
        close(screen->fd);

    basil_egl_close(&screen->egl);
}

static __GLXscreen *
basil_screen_probe(ScreenPtr pScreen)
{
    struct basil_screen *priv;
    __GLXscreen *screen;
    const char *driver_name, *device_name, *exts;
    EGLDisplay dpy = EGL_NO_DISPLAY;

    if (enableIndirectGLX) {
        LogMessage(X_ERROR, "basil: Indirect GLX is not supported\n");
        return NULL;
    }

    if (xf86DRI2Enabled()) {
        LogMessage(X_ERROR, "basil: Flag 'DRI2' is enabled, bailing.\n");
        return NULL;
    }

    priv = calloc(1, sizeof *priv);
    if (!priv)
        return NULL;

    priv->fd = -1;

    if (!basil_egl_open(&priv->egl)) {
        LogMessage(X_ERROR, "basil: failed to open libEGL.so.1\n");
        free(priv);
        return NULL;
    }

    /* DRI2Connect is used solely to obtain the DRM fd for device matching.
     * The driver name it returns is ignored; we drive EGL ourselves. */
    DRI2Connect(serverClient, pScreen, DRI2DriverDRI,
                &priv->fd, &driver_name, &device_name);

    /* Try EGL_EXT_platform_device, matching against our DRM fd. */
    if (priv->fd >= 0)
        dpy = basil_device_display(&priv->egl, priv->fd);

    if (dpy == EGL_NO_DISPLAY ||
        !priv->egl.Initialize(dpy, NULL, NULL)) {
        LogMessage(X_ERROR, "basil: failed to initialise EGL display\n");
        goto handle_error;
    }

    priv->display = dpy;

    if (!(exts = priv->egl.QueryString(dpy, EGL_EXTENSIONS))) {
        LogMessage(X_ERROR, "basil: failed to query EGL_EXTENSIONS\n");
        goto handle_error;
    }

    screen = &priv->base;
    screen->destroy        = basil_screen_destroy;
    screen->createDrawable = egl_create_glx_drawable;
    screen->createContext  = egl_create_context;
    screen->swapInterval   = NULL;

    __glXInitExtensionEnableBits(screen->glx_enable_bits);

    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_context_flush_control");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context_no_error");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context_profile");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_create_context_robustness");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_ARB_fbconfig_float");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_create_context_es2_profile");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_create_context_es_profile");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_fbconfig_packed_float");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_framebuffer_sRGB");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_no_config_context");
    __glXEnableExtension(screen->glx_enable_bits, "GLX_EXT_texture_from_pixmap");

    screen->fbconfigs = egl_setup_configs(priv, exts);
    if (!screen->fbconfigs) {
        LogMessage(X_ERROR, "basil: no usable EGL configs found\n");
        goto handle_error;
    }

    if (!screen->glvnd)
        screen->glvnd = strdup(basil_get_glvnd_vendor(&priv->egl, dpy,
                                                       priv->fd, exts));

    __glXScreenInit(screen, pScreen);
    __glXsetGetProcAddress(
        (glx_func_ptr (*)(const char *)) priv->egl.GetProcAddress);

    return screen;

handle_error:
    if (dpy != EGL_NO_DISPLAY)
        priv->egl.Terminate(dpy);

    if (priv->fd >= 0)
        close(priv->fd);

    basil_egl_close(&priv->egl);
    free(priv);
    return NULL;
}

_X_EXPORT __GLXprovider __glXDRI3Provider =
{
    basil_screen_probe,
    "DRI3",
    NULL
};