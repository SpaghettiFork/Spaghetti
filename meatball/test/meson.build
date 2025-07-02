protos = [
    '12to11-test.xml'
]

scanner_dep = dependency('wayland-scanner', native: true)
scanner = find_program(scanner_dep.get_variable(pkgconfig : 'wayland_scanner'))

server_header = generator(scanner,
                          output : '@BASENAME@.h',
                          arguments : ['server-header', '@INPUT@', '@OUTPUT@']
)

foreach xml : protos
    headers_meatball += server_header.process(xml)
endforeach