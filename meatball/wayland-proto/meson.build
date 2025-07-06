wayland_protos = [
    'drm-lease-v1.xml',
    'idle-inhibit-unstable-v1.xml',
    'keyboard-shortcuts-inhibit-unstable-v1.xml',
    'linux-dmabuf-unstable-v1.xml',
    'linux-explicit-synchronization-unstable-v1.xml',
    'pointer-constraints-unstable-v1.xml',
    'pointer-gestures-unstable-v1.xml',
    'primary-selection-unstable-v1.xml',
    'relative-pointer-unstable-v1.xml',
    'single-pixel-buffer-v1.xml',
    'tearing-control-v1.xml',
    'text-input-unstable-v3.xml',
    'viewporter.xml',
    'xdg-shell.xml',
    'xdg-activation-v1.xml',
    'xdg-decoration-unstable-v1.xml',
    'xdg-system-bell-v1.xml'
]

scanner_dep = dependency('wayland-scanner', native: true)
scanner = find_program(scanner_dep.get_variable(pkgconfig : 'wayland_scanner'))

server_header = generator(scanner,
                          output : '@BASENAME@.h',
                          arguments : ['server-header', '@INPUT@', '@OUTPUT@']
)

server_code = generator(scanner,
                          output : '@BASENAME@.c',
                          arguments : ['private-code', '@INPUT@', '@OUTPUT@']
)

foreach xml : wayland_protos
    headers_meatball += server_header.process(xml)
endforeach

foreach xml : wayland_protos
    srcs_meatball    += server_code.process(xml)
endforeach