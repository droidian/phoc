wayland_egl = dependency('wayland-egl')
wayland_cursor = dependency('wayland-cursor')
egl = dependency('egl', required: false, disabler: true)
glesv2 = dependency('glesv2', required: false, disabler: true)

executable('layer-shell-effects',
  sources: ['layer-shell-effects.c', 'egl-common.c', client_protos_headers, protos_sources],
  dependencies: [egl,
                 glesv2,
                 glib,
                 wayland_client,
                 wayland_cursor,
                 wayland_egl,
                 wlroots,
                ],
)

executable('phosh-private',
  sources: ['phosh-private.c', client_protos_headers, protos_sources],
  dependencies: [glib, wayland_client],
)

executable('device-state',
  sources: ['device-state.c', client_protos_headers, protos_sources],
  dependencies: [glib, wayland_client],
)

executable('idle',
  sources: ['idle.c', client_protos_headers, protos_sources],
  dependencies: [glib, wayland_client],
)
