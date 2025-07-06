protos = [
    '12to11-test.xml'
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

foreach xml : protos
    headers_meatball += server_header.process(xml)
endforeach

foreach xml : protos
    srcs_meatball    += server_code.process(xml)
endforeach