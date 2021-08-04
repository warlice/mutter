epoxy_dep = dependency('epoxy')

sdk_sources = [
  'mdk-context.c',
  'mdk-context.h',
  'mdk-keyboard.c',
  'mdk-keyboard.h',
  'mdk-monitor.c',
  'mdk-monitor.h',
  'mdk-pipewire.c',
  'mdk-pipewire.h',
  'mdk-pointer.c',
  'mdk-pointer.h',
  'mdk-session.c',
  'mdk-session.h',
  'mdk-stream.c',
  'mdk-stream.h',
  'mdk-touch.c',
  'mdk-touch.h',
  'mdk-types.h',
  'mdk-viewer.c',
]

sdk_built_sources = []

mutter_viewer_resources = gnome.compile_resources('viewer-resources',
  'mutter-viewer.gresource.xml',
  source_dir: '.',
)

sdk_built_sources = mutter_viewer_resources

dbus_remote_desktop_built_sources = gnome.gdbus_codegen('mdk-dbus-remote-desktop',
    join_paths(dbus_interfaces_dir, 'org.gnome.Mutter.RemoteDesktop.xml'),
    interface_prefix: 'org.gnome.Mutter.',
    namespace: 'MdkDBus',
  )
sdk_built_sources += dbus_remote_desktop_built_sources

dbus_screen_cast_built_sources = gnome.gdbus_codegen('mdk-dbus-screen-cast',
    join_paths(dbus_interfaces_dir, 'org.gnome.Mutter.ScreenCast.xml'),
    interface_prefix: 'org.gnome.Mutter.',
    namespace: 'MdkDBus',
  )
sdk_built_sources += dbus_screen_cast_built_sources

executable('mutter-viewer',
  sources: [
    sdk_sources,
    sdk_built_sources,
  ],
  include_directories: [
    top_includepath,
  ],
  c_args: mutter_c_args,
  dependencies: [
    gtk4_dep,
    libpipewire_dep,
    epoxy_dep,
  ],
  install_dir: libexecdir,
  install: true,
)
