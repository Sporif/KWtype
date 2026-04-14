project(
    'KWtype',
    'cpp',
    version: '0.1.0',
    license: 'MIT',
    default_options: [
        'buildtype=release',
        'cpp_std=c++20'
    ],
)
project_name = meson.project_name().to_lower()

qt6 = import('qt6')
qt6_deps = dependency('qt6', modules: ['Core', 'DBus'])
kwaylandclient = dependency('KWaylandClient')
wayland_client = dependency('wayland-client')
xkbcommon = dependency('xkbcommon')

qtprocessed = qt6.compile_moc(headers: 'src/main.h')

prefix = get_option('prefix')
conf_data = configuration_data()
conf_data.set('prefix', prefix)
conf_data.set('executable', project_name)
desktop_file = configure_file(input : 'dist/' + project_name + '.desktop.in',
               output : project_name + '.desktop',
               configuration : conf_data)

src_files = files(
    'src/main.cpp',
)

install_data(desktop_file, install_dir : get_option('datadir') / 'applications')

executable(
    project_name,
    src_files,
    qtprocessed,
    dependencies: [
        qt6_deps,
        kwaylandclient,
        wayland_client,
        xkbcommon,
    ],
    install : true,
)
