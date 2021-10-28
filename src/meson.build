phoc_enum_headers = files(
  [
    'phosh-private.h',
  ])
phoc_enum_sources = gnome.mkenums_simple(
  'phoc-enums',
  sources : phoc_enum_headers)

sources = files(
  'settings.c',
  'settings.h',
  'cursor.c',
  'cursor.h',
  'desktop.c',
  'desktop.h',
  'gtk-shell.c',
  'gtk-shell.h',
  'ini.c',
  'ini.h',
  'input.c',
  'input.h',
  'keyboard.c',
  'keyboard.h',
  'keybindings.c',
  'keybindings.h',
  'layer_shell.c',
  'layers.h',
  'output.c',
  'output.h',
  'phosh-private.c',
  'phosh-private.h',
  'pointer.c',
  'pointer.h',
  'render.c',
  'render.h',
  'seat.c',
  'seat.h',
  'server.c',
  'server.h',
  'switch.c',
  'switch.h',
  'text_input.c',
  'text_input.h',
  'touch.c',
  'touch.h',
  'utils.c',
  'utils.h',
  'view.c',
  'view.h',
  'virtual.c',
  'virtual.h',
  'xdg_shell.c',
)
libphoc_generated_sources = [
  phoc_enum_sources,
  server_protos_headers,
  server_protos_sources,
]

phoc_deps = [
  input,
  drm,
  gio,
  glesv2,
  gnome_desktop,
  gsettings_desktop_schemas_dep,
  math,
  pixman,
  wayland_server,
  wlroots,
  xkbcommon,
]

if have_xwayland
  sources += 'xwayland.c'
  phoc_deps += dependency('xcb')
endif

phoc_lib = static_library(
  'phoc',
  sources,
  libphoc_generated_sources,
  dependencies: phoc_deps,
  install: false,
)

libphoc_dep = declare_dependency(
  include_directories: [include_directories('.'), protocol_inc],
  link_with: phoc_lib,
  dependencies: phoc_deps,
  sources: libphoc_generated_sources)

if get_option('gtk_doc')
  phoc_gir_extra_args = [
    '--quiet',
  ]

  phoc_gir = gnome.generate_gir(phoc_lib,
                 sources: sources,
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
  dependencies: libphoc_dep,
  install: true,
)
