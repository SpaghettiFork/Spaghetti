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
    'xdg-decoration-unstable-v1.xml'
]

scanner_dep = dependency('wayland-scanner', native: true)
scanner = find_program(scanner_dep.get_variable(pkgconfig : 'wayland_scanner'))

server_header = generator(scanner,
                          output : '@BASENAME@.h',
                          arguments : ['server-header', '@INPUT@', '@OUTPUT@']
)

foreach xml : wayland_protos
    headers_meatball += server_header.process(xml)
endforeach