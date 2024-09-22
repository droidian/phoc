subdir('anim')

phoc_enum_headers = files(
  [
    'event.h',
    'layer-shell-effects.h',
    'view.h',
    'output.h',
    'phosh-private.h',
  ])
phoc_enum_sources = gnome.mkenums_simple(
  'phoc-enums',
  sources : phoc_enum_headers)

phoc_marshalers = gnome.genmarshal('phoc-marshalers',
  sources : 'phoc-marshalers.list',
  prefix : '_phoc_marshal',
  valist_marshallers : true)

sources = files(
  'bling.c',
  'bling.h',
  'color-rect.c',
  'color-rect.h',
  'cursor.c',
  'cursor.h',
  'cutouts-overlay.c',
  'cutouts-overlay.h',
  'desktop.c',
  'desktop.h',
  'device-state.c',
  'device-state.h',
  'drag-icon.c',
  'drag-icon.h',
  'event.c',
  'event.h',
  'gesture.h',
  'gesture.c',
  'gesture-drag.c',
  'gesture-drag.h',
  'gesture-single.c',
  'gesture-single.h',
  'gesture-swipe.c',
  'gesture-swipe.h',
  'gesture-zoom.c',
  'gesture-zoom.h',
  'gtk-shell.c',
  'gtk-shell.h',
  'idle-inhibit.c',
  'idle-inhibit.h',
  'input.c',
  'input.h',
  'input-device.c',
  'input-device.h',
  'keyboard.c',
  'keyboard.h',
  'keybindings.c',
  'keybindings.h',
  'layer-surface.c',
  'layer-surface.h',
  'layer-shell.c',
  'layer-shell.h',
  'layer-shell-effects.h',
  'layer-shell-effects.c',
  'output.c',
  'output.h',
  'output-shield.c',
  'output-shield.h',
  'phoc-types.h',
  'phoc-types.c',
  'phosh-private.c',
  'phosh-private.h',
  'pointer.c',
  'pointer.h',
  'render.c',
  'render.h',
  'render-private.h',
  'seat.c',
  'seat.h',
  'server.c',
  'server.h',
  'settings.c',
  'settings.h',
  'subsurface.c',
  'subsurface.h',
  'switch.c',
  'switch.h',
  'tablet.c',
  'tablet.h',
  'input-method-relay.c',
  'input-method-relay.h',
  'touch.c',
  'touch.h',
  'utils.c',
  'utils.h',
  'view.c',
  'view.h',
  'view-child.c',
  'view-child-private.h',
  'view-deco.c',
  'view-deco.h',
  'virtual.c',
  'virtual.h',
  'xdg-activation-v1.c',
  'xdg-activation-v1.h',
  'xdg-popup.c',
  'xdg-popup.h',
  'xdg-surface.c',
  'xdg-surface.h',
  'xdg-toplevel-decoration.c',
  'xdg-toplevel-decoration.h',
) + phoc_anim_sources

libphoc_generated_sources = [
  phoc_anim_enum_sources[0],
  phoc_enum_sources[0],
  phoc_marshalers[0],
  protos_sources,
]

libphoc_generated_headers = [
  phoc_config_h,
  phoc_anim_enum_sources[1],
  phoc_enum_sources[1],
  phoc_marshalers[1],
  server_protos_headers,
]

phoc_deps = [
  input,
  drm,
  gio,
  glesv2,
  gmobile_dep,
  gnome_desktop,
  gsettings_desktop_schemas_dep,
  math,
  pixman,
  udev,
  wayland_server,
  wlroots,
  xkbcommon,
]

if have_xwayland
  sources += ['xwayland-surface.c',
              'xwayland-surface.h',
             ]
  phoc_deps += dependency('xcb')
endif

phoc_lib = both_libraries(
  'phoc',
  sources,
  libphoc_generated_sources,
  libphoc_generated_headers,
  include_directories: phoc_anim_inc,
  dependencies: phoc_deps,
  install: false,
)

libphoc_dep = declare_dependency(
  include_directories: [include_directories('.'),
                        phoc_anim_inc,
                        protocol_inc],
  link_with: phoc_lib,
  dependencies: phoc_deps,
  sources: libphoc_generated_headers)

libphoc_static_dep = declare_dependency(
  include_directories: [include_directories('.'),
                        phoc_anim_inc,
                        protocol_inc],
  link_with: phoc_lib.get_static_lib(),
  dependencies: phoc_deps,
  sources: libphoc_generated_headers)

if get_option('gtk_doc')
  phoc_gir_extra_args = [
    '--quiet',
  ]

  gir_sources = sources + phoc_anim_enum_sources + phoc_enum_sources

  phoc_gir = gnome.generate_gir(phoc_lib,
                 sources: gir_sources,
               nsversion: '0',
               namespace: 'Phoc',
         export_packages: 'phoc-0',
           symbol_prefix: 'phoc',
       identifier_prefix: 'Phoc',
                includes: ['Gio-2.0'],
                 install: false,
              extra_args: phoc_gir_extra_args,
            dependencies: [phoc_deps, libphoc_dep],
   )
endif

executable(
  'phoc',
  sources: 'main.c',
  dependencies: libphoc_static_dep,
  install: true,
)
