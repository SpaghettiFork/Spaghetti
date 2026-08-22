soft2d_src = [
    'soft2d.c',
    'soft2d_gc.c',
    'soft2d_sync.c',
    'soft2d_module.c',
]

shared_module(
    'soft2d',
    soft2d_src,
    include_directories: [inc, xorg_inc],
    c_args: xorg_c_args,
    dependencies: [
        common_dep,
        dependency('libdrm', version: '>= 2.4.46'),
        gbm_dep,
    ],
    install: true,
    install_dir: module_dir,
)
