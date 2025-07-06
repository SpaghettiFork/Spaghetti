srcs_sauce = [
    'sauce.c'
]

shared_module('sauce',
    srcs_sauce,
    include_directories: [
        inc,
        xorg_inc,
        meatball_inc
    ],
    dependencies: [
        common_dep,
        meatball_headers
    ],
    link_with: [
        meatball
    ],
    install: true,
    install_dir: module_dir,
)