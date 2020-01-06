animiate = shared_module('cube',
                         ['cube.cpp', 'cubemap.cpp', 'skydome.cpp', 'simple-background.cpp'],
                         include_directories: [wayfire_api_inc, wayfire_conf_inc],
                         dependencies: [wlroots, pixman, wfconfig],
                         install: true,
                         install_dir: join_paths(get_option('libdir'), 'wayfire'))

#TODO: install only one of them?
install_subdir('shaders_2.0', install_dir: 'share/wayfire/cube')
install_subdir('shaders_3.2', install_dir: 'share/wayfire/cube')
