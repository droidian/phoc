fs = import('fs')

wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

wayland_scanner = find_program('wayland-scanner')

server_protocols = [
	[wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml'],
	[wl_protocol_dir, 'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'],
        [wl_protocol_dir, 'unstable/tablet/tablet-unstable-v2.xml'],
	['gtk-shell.xml'],
	['phosh-private.xml'],
	['wlr-foreign-toplevel-management-unstable-v1.xml'],
	['wlr-layer-shell-unstable-v1.xml'],
	['wlr-output-power-management-unstable-v1.xml'],
	['wlr-screencopy-unstable-v1.xml']
]

server_protos_sources = []
server_protos_headers = []
client_protos_headers = []

foreach p : server_protocols
  xml = join_paths(p)

  base = fs.name(xml)
  proto = fs.stem(base)

  server_protos_sources += custom_target('@0@ source'.format(proto),
					 input: xml,
					 output: '@0@-protocol.c'.format(proto),
					 command: [wayland_scanner,
						   'private-code',
						   '@INPUT@',
						   '@OUTPUT@']
					)

  server_protos_headers += custom_target('@0@ server header'.format(proto),
					 input: xml,
					 output: '@0@-protocol.h'.format(proto),
					 command: [wayland_scanner,
						   'server-header',
						   '@INPUT@',
						   '@OUTPUT@']
					)

  client_protos_headers += custom_target('@0@ client header'.format(proto),
					 input: xml,
					 output: '@0@-client-protocol.h'.format(proto),
					 command: [wayland_scanner,
						   'client-header',
						   '@INPUT@',
						   '@OUTPUT@']
					)
endforeach
