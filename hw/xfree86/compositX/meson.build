sources = [
    'compositx.c',
    'render.c',
]

shared_module('compositx',
    sources,
    include_directories: [ inc, xorg_inc ],
    dependencies: common_dep,
    link_with: [ xorg_server ],
    c_args: xorg_c_args,
    install: true,
    install_dir: module_dir,
)