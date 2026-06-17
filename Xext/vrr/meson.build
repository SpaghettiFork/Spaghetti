srcs_vrr = [
    'vrr.c',
]

libxserver_vrr = static_library('libxserver_vrr',
    srcs_vrr,
    include_directories: inc,
    dependencies: [common_dep],
)

if build_xorg
    install_data('vrr.h', install_dir: xorgsdkdir)
endif
