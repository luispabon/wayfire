wobbly = shared_module('wobbly',
                       ['wobbly.cpp', 'wobbly.c'],
                       include_directories: [wayfire_api_inc, wayfire_conf_inc],
                       dependencies: [wlroots, pixman, wfconfig],
                       install: true,
                       install_dir: join_paths(get_option('libdir'), 'wayfire'))
