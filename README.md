Phone compositor
================
[![Code coverage](https://source.puri.sm/Librem5/phoc/badges/master/coverage.svg)](https://source.puri.sm/Librem5/phoc/commits/master)

[wlroots][1] based Phone compositor as used on the Librem5.

Phoc is pronounced like the English word fog.

## Dependencies
On a Debian based system run:

```sh
    sudo apt-get -y install build-essential
    sudo apt-get -y build-dep .
```

For an explicit list of dependencies check the `Build-Depends` entry in the
[debian/control][] file.

## Building

We use the meson (and thereby Ninja) build system for phoc. The quickest
way to get going is to do the following:

    meson . _build
    ninja -C _build
    ninja -C _build install

This assumes you have wlroots installed on your system. If you don't have that
and/or want to build from source run:

    git submodule update --init
    meson . _build
    ninja -C _build

This will fetch a matching version of wlroots and build that as well.

## Running

To run from the source tree use

    _build/run

## Test
After making source changes run

    xvfb-run ninja -C _build test

to see if anything broke.

# Configuration

phoc's behaviour can be configured via `GSettings`. For your convienience,
a set of scripts to manipulate config values is available in `helpers`
directory.

 - `scale-to-fit` toggles automatic scaling of applications that don't fit
   the screen. This setting is enabled per application using its reported
   app-id. For instance, to enable scaling of GNOME Maps windows use:

       helpers/scale-to-fit org.gnome.Maps on

 - `auto-maximize` toggles automatic maximization of Wayland windows.
   Disabling it allows windows to be resized and moved, which may be desired
   when running phoc on desktop-like setups.

        helpers/auto-maximize off

Outputs are configured via `phoc.ini` config file - see `src/phoc.ini.example`
for more information.

# Debugging

phoc uses glib so the `G_MESSAGES_DEBUG` environment variable can be
used to enable more log messages and `G_DEBUG` to assert on warnings
and criticals. The log domains all start with `phoc-` and are usally
`phoc-<sourcefile>`. All wlroots related messages are logged with
`phoc-wlroots`.
See https://developer.gnome.org/glib/stable/glib-running.html for more
details on these environment variables.

There's also a `PHOC_DEBUG` enviroment variable to turn on some debugging
features. Use `PHOC_DEBUG=help phoc` to see supported flags.

[1]: https://github.com/swaywm/wlroots
