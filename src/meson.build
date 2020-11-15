sources = [
  'settings.c',
  'cursor.c',
  'desktop.c',
  'gtk-shell.c',
  'ini.c',
  'input.c',
  'keyboard.c',
  'keybindings.c',
  'layer_shell.c',
  'output.c',
  'phosh.c',
  'render.c',
  'seat.c',
  'server.c',
  'switch.c',
  'text_input.c',
  'touch.c',
  'utils.c',
  'view.c',
  'virtual.c',
  'xdg_shell.c',
]

phoc_deps = [
  input,
  drm,
  gio,
  glesv2,
  gnome_desktop,
  math,
  pixman,
  wayland_server,
  wlroots,
  server_protos,
  xkbcommon,
]

if have_xwayland
  sources += 'xwayland.c'
  phoc_deps += dependency('xcb')
endif

phoc_lib = static_library(
  'phoc',
  sources,
  dependencies: phoc_deps,
  install: false,
)
phoc_inc = include_directories('.')

phoc_dep = declare_dependency(
  include_directories: phoc_inc,
  link_with: phoc_lib,
  dependencies: phoc_deps)

executable(
  'phoc',
  sources: 'main.c',
  dependencies: phoc_dep,
  install: true,
)
