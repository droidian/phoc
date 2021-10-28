project('phoc', 'c',
          version: '0.9.0',
          license: 'GPLv3+',
    meson_version: '>= 0.54.0',
  default_options: [
    'warning_level=1',
    'buildtype=debugoptimized',
    'c_std=gnu11',
  ],
)

add_project_arguments([
  '-DWLR_USE_UNSTABLE',
  '-I' + meson.build_root(),
], language: 'c')

root_inc = include_directories('.')
src_inc = include_directories('src')
protocol_inc = include_directories('protocols')

prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))

cc = meson.get_compiler('c')

gio            = dependency('gio-2.0', version: '>=2.50.0')
glesv2         = dependency('glesv2')
glib           = dependency('glib-2.0', version: '>=2.50.0')
gobject        = dependency('gobject-2.0', version: '>=2.50.0')
gnome_desktop  = dependency('gnome-desktop-3.0', version: '>=3.26')
gsettings_desktop_schemas_dep = dependency('gsettings-desktop-schemas')
input          = dependency('libinput')
drm            = dependency('libdrm')
pixman         = dependency('pixman-1')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.15')
wayland_server = dependency('wayland-server')
xkbcommon      = dependency('xkbcommon')
math           = cc.find_library('m')

embed_wlroots = get_option('embed-wlroots')

if not embed_wlroots.disabled()
   # Try first to find wlroots as a subproject, then as a system dependency
   wlroots_proj = subproject(
        'wlroots',
        default_options: ['examples=false'],
        required: false,
   )

   if embed_wlroots.enabled() and not wlroots_proj.found()
        error('Wlroots subproject not found but enabled')
   endif

   if get_option('gtk_doc') != false
     error('Can\'t generate docs and use embedded wlroots build')
   endif
endif

if not embed_wlroots.disabled() and wlroots_proj.found()
        wlroots = wlroots_proj.get_variable('wlroots')
        wlroots_conf = wlroots_proj.get_variable('conf_data')
        wlroots_has_xwayland = wlroots_conf.get('WLR_HAS_XWAYLAND') == 1
        wlroots_has_output_power_management = true
        have_wlr_set_startup_id = true
        have_wlr_remove_startup_info = true
else
        wlroots = dependency('wlroots', version: '>= 0.12.0')
        wlroots_has_xwayland = cc.get_define('WLR_HAS_XWAYLAND', prefix: '#include <wlr/config.h>', dependencies: wlroots) == '1'
        wlroots_has_output_power_management = cc.has_header('wlr/types/wlr_output_power_management_v1.h', dependencies: wlroots)
        have_wlr_set_startup_id = false
        if wlroots_has_xwayland and cc.has_member('struct wlr_xwayland_surface',
                                                  'events.set_startup_id',
                                                  prefix: '''#include "wlr/xwayland.h"''',
                                                  args: '-DWLR_USE_UNSTABLE',
                                                  dependencies: wlroots)
          have_wlr_set_startup_id = true
        endif

        have_wlr_remove_startup_info = false
        if wlroots_has_xwayland and cc.has_member('struct wlr_xwayland',
                                                  'events.remove_startup_info',
                                                  prefix: '''#include "wlr/xwayland.h"''',
                                                  args: '-DWLR_USE_UNSTABLE',
                                                  dependencies: wlroots)
          have_wlr_remove_startup_info = true
        endif
endif

if get_option('xwayland').enabled() and not wlroots_has_xwayland
        error('Cannot enable Xwayland in phoc: wlroots has been built without Xwayland support')
endif
have_xwayland = get_option('xwayland').enabled()

global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdate-time',
  ['-Werror=format-security', '-Werror=format=2'],
  '-Wendif-labels',
  '-Werror=incompatible-pointer-types',
  '-Werror=missing-declarations',
  '-Werror=overflow',
  '-Werror=return-type',
  '-Werror=shift-count-overflow',
  '-Werror=shift-overflow=2',
  '-Werror=implicit-fallthrough=3',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wtype-limits',
  '-Wundef',
  '-Wunused-function',
  '-Wswitch-default',
]

# TODO:
#  '-Wfloat-equal',
#  '-Wswitch-enum',

if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

add_project_arguments(
  cc.get_supported_arguments(test_c_args),
  language: 'c'
)

gnome = import('gnome')

meson.add_install_script(
  join_paths('build-aux', 'post_install.py'),
  join_paths(get_option('prefix'), get_option('datadir'))
)

subdir('protocols')
subdir('src')
subdir('tests')
subdir('data')
subdir('doc')

summary = [
        '',
        '--------------------------',
        'phoc @0@'.format(meson.project_version()),
        '',
        '           xwayland: @0@'.format(have_xwayland),
        ' wlroots subproject: @0@'.format(not embed_wlroots.disabled() and wlroots_proj.found()),
        '    wlroots version: @0@'.format(wlroots.version()),
        '      Documentation: @0@'.format(get_option('gtk_doc')),
        '--------------------------',
        ''
]
message('\n'.join(summary))


config_h = configuration_data()
config_h.set_quoted('PHOC_VERSION', meson.project_version())
config_h.set('PHOC_XWAYLAND', have_xwayland)
config_h.set('PHOC_HAVE_WLR_SET_STARTUP_ID', have_wlr_set_startup_id)
config_h.set('PHOC_HAVE_WLR_REMOVE_STARTUP_INFO', have_wlr_remove_startup_info)

configure_file(
  input: 'config.h.in',
  output: 'config.h',
  configuration: config_h,
)

run_data = configuration_data()
run_data.set('ABS_BUILDDIR', meson.current_build_dir())
run_data.set('ABS_SRCDIR', meson.current_source_dir())
configure_file(
  input: 'run.in',
  output: 'run',
  configuration: run_data)

