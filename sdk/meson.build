mutter_viewer_resources = gnome.compile_resources('viewer-resources',
  'mutter-viewer.gresource.xml',
  source_dir: '.',
)

executable('mutter-viewer',
  sources: [
    'mdk-viewer.c',
    mutter_viewer_resources,
  ],
  include_directories: [
    top_includepath,
  ],
  c_args: mutter_c_args,
  dependencies: [gtk4_dep],
  install_dir: bindir,
  install: true,
)
