schemas = ['sm.puri.phoc.gschema.xml']
compiled = gnome.compile_schemas(
  build_by_default: true
)

install_data(
  schemas,
  install_dir: 'share/glib-2.0/schemas'
)

devconf = configuration_data()
devconf.set('compositor', meson.project_build_root() / 'src' / 'phoc')
devconf.set('devscript', meson.current_build_dir() / 'phoc-dev')
devconf.set('devconfig', meson.current_source_dir() / 'phoc-dev.ini')
devconf.set('devuid', get_option('dev-uid'))
devconf.set('version', meson.project_version())

configure_file(
  input: 'phoc-dev.in',
  output: 'phoc-dev',
  configuration: devconf,
)

configure_file(
  input: 'phoc-dev.service.in',
  output: 'phoc-dev.service',
  configuration: devconf,
)
