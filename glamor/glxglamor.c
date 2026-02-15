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
/*
 * Copyright © 2019 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Adam Jackson <ajax@redhat.com>
 */

#define MESA_EGL_NO_X11_HEADERS
#define EGL_NO_X11
#include <epoxy/egl.h>
#include "glxserver.h"
#include "glxutil.h"
#include "compint.h"
#include <X11/extensions/composite.h>
#include "glamor_priv.h"
#include "glamor.h"

/* Can't get these from <GL/glx.h> since it pulls in client headers */
#define GLX_RGBA_BIT                0x00000001
#define GLX_WINDOW_BIT              0x00000001
#define GLX_PIXMAP_BIT              0x00000002
#define GLX_PBUFFER_BIT             0x00000004
#define GLX_NONE                    0x8000
#define GLX_SLOW_CONFIG             0x8001
#define GLX_TRUE_COLOR              0x8002
#define GLX_DIRECT_COLOR            0x8003
#define GLX_TRANSPARENT_RGB         0x8008
#define GLX_NON_CONFORMANT_CONFIG   0x800D
#define GLX_DONT_CARE               0xFFFFFFFF
#define GLX_RGBA_FLOAT_BIT_ARB      0x00000004
#define GLX_SWAP_UNDEFINED_OML      0x8063

struct egl_config_meta
{
    Bool srgb_only;
    Bool direct_color;
    Bool double_buffer;
    Bool duplicate_for_composite;
};

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

    /* GLAMOR has it's own synchronization mechanism using 
     * glFinish() and glFlush() which isn't really compatible
     * with what GLX expects waitX and waitGL to behave like.
     * 
     * Set waitX and waitGL as NULL, the server will NO-OP them. */
    ret->waitX = NULL;
    ret->waitGL = NULL;

    return ret;
}

static struct __GLXconfig*
eglConfigToGLXConfig(EGLDisplay disp, struct egl_config_meta meta,
                     EGLConfig egl_config, struct __GLXconfig *chain)
{
    EGLint value = 0;
    EGLint surface_type = 0;

    __GLXconfig *glx_config = calloc(1, sizeof(__GLXconfig));
    if (!glx_config)
        return chain;

    eglGetConfigAttrib(disp, egl_config, EGL_SURFACE_TYPE, &surface_type);
    eglGetConfigAttrib(disp, egl_config, EGL_RED_SIZE, &glx_config->redBits);
    eglGetConfigAttrib(disp, egl_config, EGL_GREEN_SIZE, &glx_config->greenBits);
    eglGetConfigAttrib(disp, egl_config, EGL_BLUE_SIZE, &glx_config->blueBits);
    eglGetConfigAttrib(disp, egl_config, EGL_ALPHA_SIZE, &glx_config->alphaBits);
    eglGetConfigAttrib(disp, egl_config, EGL_BUFFER_SIZE, &glx_config->rgbBits);

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

    /* Mesa returns only EGL_WINDOW_BIT support when using the
     * GBM backend, we'll need to assume support for other bits. */
    if (surface_type == EGL_WINDOW_BIT)
        surface_type = EGL_WINDOW_BIT | EGL_PIXMAP_BIT | EGL_PBUFFER_BIT;

    TRANSLATE_TO_EQUIVALENT(EGL_WINDOW_BIT, GLX_WINDOW_BIT);
    TRANSLATE_TO_EQUIVALENT(EGL_PBUFFER_BIT, GLX_PBUFFER_BIT);
    TRANSLATE_TO_EQUIVALENT(EGL_PIXMAP_BIT, GLX_PIXMAP_BIT);
    
#undef TRANSLATE_TO_EQUIVALENT

    glx_config->redMask = ((1 << glx_config->redBits) - 1) << (glx_config->blueBits + glx_config->greenBits);
    glx_config->greenMask = ((1 << glx_config->greenBits) - 1) << glx_config->blueBits;
    glx_config->blueMask = (1 << glx_config->blueBits) - 1;
    
    if (glx_config->alphaBits)
        glx_config->alphaMask = ((1 << glx_config->alphaBits) - 1) << (glx_config->blueBits + glx_config->greenBits + glx_config->redBits);
    
    /* GLX conformance failure: there's no such thing as accumulation
     * buffers in EGL.  they could be emulated with shaders and FBOs,
     * but I'm pretty sure nobody's using this feature since it's
     * entirely software. Note that GLX conformance merely requires
     * that an accum buffer _exist_, not a minimum bitness. */
    glx_config->accumRedBits = 0;
    glx_config->accumGreenBits = 0;
    glx_config->accumBlueBits = 0;
    glx_config->accumAlphaBits = 0;
    
    eglGetConfigAttrib(disp, egl_config, EGL_DEPTH_SIZE, &glx_config->depthBits);
    eglGetConfigAttrib(disp, egl_config, EGL_STENCIL_SIZE, &glx_config->stencilBits);

    if (meta.direct_color)
        glx_config->visualType = GLX_DIRECT_COLOR;
    else
        glx_config->visualType = GLX_TRUE_COLOR;

    /* This is duplicated. */
    glx_config->doubleBufferMode = meta.double_buffer;

    eglGetConfigAttrib(disp, egl_config, EGL_CONFIG_CAVEAT, &value);

    if (value == EGL_NONE)
        glx_config->visualRating = GLX_NONE;
    else if (value == EGL_SLOW_CONFIG)
        glx_config->visualRating = GLX_SLOW_CONFIG;
    else if (value == EGL_NON_CONFORMANT_CONFIG)
        glx_config->visualRating = GLX_NON_CONFORMANT_CONFIG;

    /* Only query transparent channels for EGL_TRANSPARENT_RGB. */
    eglGetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_TYPE, &value);
    
    if (value == EGL_TRANSPARENT_RGB) {
        glx_config->transparentPixel = GLX_TRANSPARENT_RGB;
        eglGetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_RED_VALUE,
                           &glx_config->transparentRed);
        eglGetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_GREEN_VALUE,
                           &glx_config->transparentGreen);
        eglGetConfigAttrib(disp, egl_config, EGL_TRANSPARENT_BLUE_VALUE,
                           &glx_config->transparentBlue);
    } else {
        glx_config->transparentPixel = GLX_NONE;
    }
    
    eglGetConfigAttrib(disp, egl_config, EGL_SAMPLE_BUFFERS, 
                       &glx_config->sampleBuffers);
    eglGetConfigAttrib(disp, egl_config, EGL_SAMPLES, &glx_config->samples);

    if (eglGetConfigAttrib(disp, egl_config,
                           EGL_COLOR_COMPONENT_TYPE_EXT,
                           &value) == EGL_TRUE) {

        if (value == EGL_COLOR_COMPONENT_TYPE_FIXED_EXT) {
            glx_config->renderType = GLX_RGBA_BIT;
        } else if (value == EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT) {
            glx_config->renderType = GLX_RGBA_FLOAT_BIT_ARB;
        } else {
            LogMessage(X_WARNING, "Unknown EGL_COLOR_COMPONENT_TYPE_EXT value of 0x%08x\n", value);
            free(glx_config);
            return chain;
        }
    } else {
        /* Assume GLX_RGBA_BIT if the eglGetConfigAttrib call failed. */
        glx_config->renderType = GLX_RGBA_BIT;
    }

    if (surface_type & EGL_PBUFFER_BIT) {
        eglGetConfigAttrib(disp, egl_config, EGL_MAX_PBUFFER_WIDTH, &glx_config->maxPbufferWidth);
        eglGetConfigAttrib(disp, egl_config, EGL_MAX_PBUFFER_HEIGHT, &glx_config->maxPbufferHeight);
        eglGetConfigAttrib(disp, egl_config, EGL_MAX_PBUFFER_PIXELS, &value);

        if (value) {
            glx_config->maxPbufferPixels = value;
        } else {
            /* Mesa seems to ignore EGL_MAX_PBUFFER_PIXELS... */
            glx_config->maxPbufferPixels = glx_config->maxPbufferWidth * glx_config->maxPbufferHeight;
        }
    }

    /* EGL doesn't have an equivalent for OML_swap_method. */
    glx_config->swapMethod = GLX_SWAP_UNDEFINED_OML;

    /* EXT_texture_from_pixmap is an absolute nightmare,
     * just lie that both variants are supported unconditionally.
     *
     * 1. Mesa returns EGL_FALSE for both EGL queries through GBM
     * 2. NVIDIA drivers also return EGL_FALSE when doing queries.
     * 3. Configs without bindToTextureRgba don't get exposed at all */
    glx_config->bindToTextureRgb = GL_TRUE;
    glx_config->bindToTextureRgba = GL_TRUE;

    /* No EGL equivalent, pretend we support everything. */
    glx_config->bindToTextureTargets = GLX_DONT_CARE;

    /* Here we decide which FBconfigs will be duplicated for compositing.
     * FBconfigs marked with duplicatedForComp will be reserved for
     * compositing visuals.
     * 
     * It might look strange to do this decision this late when translation
     * from an EGLConfig is already done, but using the EGLConfig
     * accessor functions becomes worse both with respect to code complexity
     * and CPU usage. */
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

#ifdef COMPOSITE
    glx_config->duplicatedForComp = meta.duplicate_for_composite;
#endif

    glx_config->next = chain;
    return glx_config;
}

static __GLXconfig *
egl_setup_configs(ScreenPtr pScreen, EGLDisplay disp)
{
    int i, j, k, nconfigs;
    struct __GLXconfig *c = NULL;
    EGLConfig *host_configs = NULL;
    
    Bool can_srgb = epoxy_has_gl_extension("GL_ARB_framebuffer_sRGB") ||
                    epoxy_has_gl_extension("GL_EXT_framebuffer_sRGB") ||
                    epoxy_has_gl_extension("GL_EXT_sRGB_write_control");

    eglGetConfigs(disp, NULL, 0, &nconfigs);

    host_configs = calloc(nconfigs, sizeof(EGLConfig));
    if (!host_configs)
        return NULL;

    eglGetConfigs(disp, host_configs, nconfigs, &nconfigs);

    /* We walk the EGL configs backwards to make building the
     * ->next chain easier. */
    for (i = nconfigs - 1; i >= 0; i--) {
        for (j = 0; j < 3; j++) /* direct_color */ {
            for (k = 0; k < 2; k++) /* double_buffer */ {
                if (can_srgb) {
                    c = eglConfigToGLXConfig(disp, (struct egl_config_meta) {
                        .direct_color = (j == 1),
                        .double_buffer = (k > 0),
                        .duplicate_for_composite = (j == 0),
                        .srgb_only = TRUE,
                    }, host_configs[i], c);
                }

                c = eglConfigToGLXConfig(disp, (struct egl_config_meta) {
                    .direct_color = (j == 1),
                    .double_buffer = (k > 0),
                    .duplicate_for_composite = (j == 0),
                    .srgb_only = FALSE,
                 }, host_configs[i], c);
            }
        }
    }

    free(host_configs);
    return c;
}

static __GLXscreen *
egl_screen_probe(ScreenPtr pScreen)
{
    __GLXscreen *screen;
    glamor_screen_private *glamor_screen;

    if (enableIndirectGLX)
        return NULL; /* not implemented */

    glamor_screen = glamor_get_screen_private(pScreen);
    if (!glamor_screen)
        return NULL;

    screen = calloc(1, sizeof(__GLXscreen));
    if (!screen)
        return NULL;

    screen->destroy = NULL; /* Handled by GLX. */
    screen->createDrawable = egl_create_glx_drawable;
    screen->createContext = egl_create_context;
    screen->swapInterval = NULL;

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

    screen->fbconfigs = egl_setup_configs(pScreen, glamor_screen->ctx.display);
    if (!screen->fbconfigs) {
        free(screen);
        return NULL;
    }

    if (!screen->glvnd) {
        if (glamor_screen->glvnd_vendor) {
            screen->glvnd = strdup(glamor_screen->glvnd_vendor);
        } else {
            screen->glvnd = strdup("mesa");
        }
    }

    __glXScreenInit(screen, pScreen);
    __glXsetGetProcAddress(eglGetProcAddress);

    return screen;
}

__GLXprovider glamor_provider = 
{
    egl_screen_probe,
    "GLAMOR",
    NULL
};