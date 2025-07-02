# Meatball.

A Wayland implementation on X11, based upon 12to11 that lets Spaghetti users sleep with ease.

## Supported platforms.

> Software acceleration

- Anything.

> Hardware acceleration

- A video DDX with DRI3 v1.2 support [1] [2]
- An EGL capable graphics card with the following extensions supported:
  - `GL_OES_EGL_sync`
  - `GL_OES_EGL_image`
  - `GL_OES_EGL_image_external`
  - `GL_EXT_read_format_bgra`
  - `GL_EXT_unpack_subimage`
  - `EGL_KHR_image_base`
  - `EGL_EXT_platform_base`
  - Either `EGL_EXT_device_drm_render_node` or `EGL_EXT_device_drm`
- Additional optional but recommended extensions:
  - `EGL_EXT_device_query`
  - `EGL_EXT_image_dma_buf_import_modifiers`
  - `EGL_KHR_fence_sync`
  - `EGL_KHR_wait_sync`
  - `EGL_ANDROID_native_fence_sync`
  - `EGL_EXT_swap_buffers_with_damage`
  - `EGL_EXT_buffer_age`
  - `EGL_KHR_partial_update`

[1] `modesetting` is the only video DDX that currently supports this.

[2] All graphics devices known to Spaghetti *MUST* support DRI3 v1.2 for it to be exposed.

## Enabling Meatball

As of this commit, Meatball is not yet available.

## License

```
Spaghetti Display Server
Copyright (C) 2025  SpaghettiFork

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
```

```
12to11, a Wayland compositor running on top of an X server.
Copyright (C) 2022 to various contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```