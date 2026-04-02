This document provides a summary of behaviour changes
between Spaghetti and the X.Org Display server.

# 26.0

## MODULES

- All video driver DDX's must be recompiled due to ABI changes since 1.21.x.

- Other drivers may work with `IgnoreABI` being set to true.

- Some drivers may still suffer from breakages from the master branch of X.Org, this may include libVNC.

## GLAMOR

- GLAMOR's GLX provider is more strict:

    - EGL surface types are now set only if supported. [1]

	- `maxPbufferWidth`, `maxPbufferHeight` and `maxPbufferPixels` is now set, fixing an Xorg bug.

	- `transparentPixel` is now queried via `EGL_TRANSPARENT_TYPE`

	- `EGL_TRANSPARENT_[RED|GREEN|BLUE]_VALUE` is now only set if `EGL_TRANSPARENT_TYPE` is `EGL_TRANSPARENT_RGB`

	- Channel masks now use a generic path.

	- `waitX` and `waitGL` is no longer supported.
	
	- `swapBuffers` is a NO-OP that always returns TRUE.

- GLAMOR uses GBM buffer objects in place of EGLImage's.

- Optimizations using `gbm_bo_map` to reduce data copying.

- Memory leak fixes.

- `dmabuf_capable` is enabled by default for AMD and Zink.

- Context priority can be set using the `high_priority` debug value.

- GLAMOR now requests contexts slightly differently to prevent NVIDIA devices being stuck on OpenGL 3.1 contexts.

[1] Mesa is excluded from this behaviour due to a driver bug.

## MODESETTING

- Memory leak fixes.

- The front BO is imported during setup instead of during the first page-flip.

- Cursor dimensions are no-longer guessed, using DRM metadata only.

- A MSC ordering issue is fixed with TearFree being enabled alongside `dmabuf_capable`

- Atomic KMS is no longer available through the `Option "Atomic" "true"` configuration option.

- Minor opaque format handling fixes.

## RENDER

- 4-bit alpha (`PICT_a4`) support is exposed.

## FB

- Fixed source and mask clipping for `fbComposite`.