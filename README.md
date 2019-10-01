# Wayfire

# Introduction

[![Join the chat at https://gitter.im/Wayfire-WM/Lobby](https://badges.gitter.im/Wayfire-WM/Lobby.svg)](https://gitter.im/Wayfire-WM/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge) or join #wayfire at freenode.net

Wayfire is a wayland compositor based on wlroots. It aims to create a customizable, extendable and lightweight environment without sacrificing its appearance. If you want to gain a better impression at what it can do, see the demo videos on youtube: [link](https://www.youtube.com/playlist?list=PLb7YRKEhWEBUIoT-a29UoJW9mhfzjpNle)

# Build and install

To build wayfire, you'll need `glm`, `wf-config`(built as a submodule by default) and `wlroots` + its build dependencies. When ready, simply clone this repo, compile and install:

```
git clone https://github.com/WayfireWM/wayfire && cd wayfire
meson build --prefix=/usr --buildtype=release
ninja -C build && sudo ninja -C build install
```

# Packaging status

- [Fedora](https://apps.fedoraproject.org/packages/wayfire) (31+): `sudo dnf install wayfire`

**Before running Wayfire, copy the default configuration file which is located in the root of the repository and place it in:**
```
cp wayfire.ini.default ~/.config/wayfire.ini
```
It is also advisable to install https://github.com/WayfireWM/wf-shell in order to get a background and a panel. Just follow the instructions in the README of wf-shell. You may also want to visit the page on [external tools](https://github.com/WayfireWM/wayfire/wiki/External-tools).

To start wayfire, just execute `wayfire` from a TTY. If you encounter any issues, please read [debug report guidelines](https://github.com/ammen99/wayfire/wiki/Debugging-problems) and open a bug in this repo. Or you can also write in gitter.
# Project status

**IMPORTANT**: Although many of the features one can expect from a WM are implemented, Wayfire should be considered as **(pre-)alpha** quality. In my setup it works just fine, but the project hasn't been extensively tested, so there are a lot of bugs to be expected and to be fixed. Bug reports are welcome!

Currently supported:
1. Seamless integration of both native wayland & Xwayland clients
2. Workspaces (or more like viewports if you are familiar with compiz)
3. Configurable bindings (int many cases multiple bindings to the same action are supported)
4. Configuration on-the-fly - changes made to the config file are applied immediately without restarting wayfire
5. Various plugins: Desktop cube, Expo(live workspace previews), Grid(arrange floating windows in a grid), Auto snap at edges, and many others
6. Shell panel with launchers, date, internet connection & battery support
7. Basic touchscreen gestures - swipe, swipe-from-edge, pinch, all of them with >= 3 fingers. Can be configured to any command or activation/toggle binding. See the config for examples.

See the list of issues to know what else is coming to wayfire. Also, don't hesitate to open a new one if you find any bugs or want some new feature.

# Contributing to the project

There are many ways you can help, aside from developing - open bug reports, test features, add documentation, etc.

If you want to write your own plugin, a general outline of how the plugin system works is here: [plugin architecture](https://github.com/ammen99/wayfire/wiki/Plugin-architecture). Unfortunately documentation always end up last in the TODO list, so information is very scarce. You can take a look at the simpler plugins(the simplest are `command` and `rotator`, around 50-60 loc each). I have also added documentations for many of the API functions in the headers(`src/api/*`). Don't hesitate to write in IRC (or gitter) if you have any questions.
