headers_meatball = []

srcs_meatball = [
    'meatball.c',
    'log.c',
    'run.c',
    'alloc.c',
    'fns.c',
    'output.c',
    'compositor.c',
    'surface.c',
    'region.c',
    'shm.c',
    'atoms.c',
    'subcompositor.c',
    'positioner.c',
    'xdg_wm.c',
    'xdg_surface.c',
    'xdg_toplevel.c',
    'frame_clock.c',
    'xerror.c',
    'ewmh.c',
    'timer.c',
    'subsurface.c',
    'seat.c',
    'data_device.c',
    'xdg_popup.c',
    'dmabuf.c',
    'buffer.c',
    'select.c',
    'xdata.c',
    'xsettings.c',
    'dnd.c',
    'icon_surface.c',
    'primary_selection.c',
    'renderer.c',
    'explicit_synchronization.c',
    'transform.c',
    'wp_viewporter.c',
    'decoration.c',
    'text_input.c',
    'single_pixel_buffer.c',
    'drm_lease.c',
    'pointer_constraints.c',
    'time.c',
    'relative_pointer.c',
    'keyboard_shortcuts_inhibit.c',
    'idle_inhibit.c',
    'process.c',
    'fence_ring.c',
    'pointer_gestures.c',
    'test.c',
    'buffer_release.c',
    'xdg_activation.c',
    'tearing_control.c',
    'sync_source.c',
    'xdg_system_bell.c',
    'picture_renderer.c',
    'egl_renderer.c'
]

# Do the dependency check first.
meatball_dep = [
    # Core.
    dependency('xorg-server', required : true),

    # Xorg interaction.
    dependency('xpresent', required : true),
    dependency('xcb-randr', required : true),
    dependency('xcb-dri3', required : true),
    dependency('xcb-shape', required : true),
    dependency('xcb-shm', required : true),
    dependency('x11-xcb', required : true),
    dependency('xcb', required : true),
    dependency('xfixes', required : true),
    dependency('xrandr', required : true),
    dependency('xi', required : true),
    dependency('xshmfence', required : true),

    # Finally we also need Wayland of course.
    dependency('wayland-server', version: wayland_req, required: true)
]

# We need the headers in order to actually get a build.
subdir('test')
subdir('wayland-proto')

meatball_headers = declare_dependency(
    sources : headers_meatball,
    include_directories: [
        '.',
        'static-headers',
        'gpu-shaders',
    ]
)

meatball = static_library('meatball',
    srcs_meatball,
    c_args : [
        '-D_GNU_SOURCE',
        '-D_BSD_SOURCE',
        '-U_SVID_SOURCE'
    ],
    include_directories : [
        inc
    ],
    dependencies : [
        common_dep,
        epoxy_dep,
        libdrm_dep,
        pixman_dep,
        meatball_dep,
        meatball_headers
    ]
)

if build_xorg
    install_data('meatball.h', install_dir : xorgsdkdir)
endif