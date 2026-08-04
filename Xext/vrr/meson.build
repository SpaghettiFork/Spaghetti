srcs_vrr = [
    'vrr.c',
]

hdrs_vrr = [
    'vrr_priv.h',
]

libxserver_vrr = static_library('libxserver_vrr',
    srcs_vrr,
    include_directories: inc,
    dependencies: [common_dep],
)

if build_xorg
    install_data(hdrs_vrr, install_dir: xorgsdkdir)
endif
