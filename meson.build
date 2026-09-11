project('phosh-nfc-quick-setting', 'c',
          version: '0.1.0',
          license: 'GPL-3.0-or-later',
    meson_version: '>= 1.0.0',
  default_options: ['warning_level=2', 'c_std=gnu11'],
)

gnome = import('gnome')

name = 'nfc-quick-setting'

# phosh publishes phosh-plugins for out of tree plugins: it carries the public
# phosh-plugin.h and says where plugins are loaded from. In phosh's own tree
# this file would take plugin_dep and plugins_dir from the parent instead;
# everything else here is the same as plugins/location-quick-setting.
plugins_dep = dependency('phosh-plugins')
plugins_dir = plugins_dep.get_variable(pkgconfig: 'quick_setting_plugins_dir')

plugin_dep = [
  plugins_dep,
  dependency('libphosh-0.45'),
  dependency('gtk+-3.0'),
  dependency('gio-2.0'),
]

nfc_quick_setting_resources = gnome.compile_resources(
  'phosh-plugin-nfc-quick-setting-resources',
  'phosh-plugin-nfc-quick-setting.gresources.xml',
  c_name: 'phosh_plugin_nfc_quick_setting',
)

nfc_quick_setting_plugin_sources = files(
  'nfc-quick-setting.c',
  'phosh-plugin-nfc-quick-setting.c',
)

phosh_nfc_quick_setting_plugin = shared_module(
  'phosh-plugin-nfc-quick-setting',
  nfc_quick_setting_plugin_sources,
  nfc_quick_setting_resources,
  c_args: [
    '-DG_LOG_DOMAIN="phosh-plugin-@0@"'.format(name),
    '-DPLUGIN_NAME="@0@"'.format(name),
    '-DGETTEXT_PACKAGE="phosh"',
    '-Wno-unused-parameter',
  ],
  dependencies: plugin_dep,
  install: true,
  install_dir: plugins_dir,
)

pluginconf = configuration_data()
pluginconf.set('name', name)
pluginconf.set('plugins_dir', plugins_dir)

configure_file(
        input: name + '.desktop.in.in',
       output: name + '.plugin',
configuration: pluginconf,
      install: true,
  install_dir: plugins_dir,
)

install_data(
  'icons/nfc-quick-setting-symbolic.svg',
  'icons/nfc-disabled-quick-setting-symbolic.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/symbolic/apps',
)
