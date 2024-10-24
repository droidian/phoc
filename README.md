# Phone compositor

[![Code coverage](https://gitlab.gnome.org/World/Phosh/phoc/badges/main/coverage.svg)](https://gitlab.gnome.org/World/Phosh/phoc/-/commits/main)

[wlroots][1] based Phone compositor as used on the Librem5.

Phoc is pronounced like the English word fog.

## Building Phoc

### Dependencies

On a Debian based system run:

```sh
    sudo apt-get -y install build-essential
    sudo apt-get -y build-dep .
```

For an explicit list of dependencies check the `Build-Depends` entry in the
[debian/control][2] file.

### Building

We use the meson (and thereby Ninja) build system for phoc. The quickest
way to get going is to do the following:

```sh
    meson setup _build
    meson compile -C _build
    meson install -C _build --skip-subprojects
```

This assumes you have wlroots installed on your system. If you don't have that
and/or want to build from source run:

```sh
    meson -Dembed-wlroots=enabled --default-library=static _build
    meson compile -C _build
```

This will fetch a matching version of wlroots and build that as well.

## Running

To run from the source tree use

```sh
    ./_build/run
```

### Test

After making source changes run

```sh
    xvfb-run meson test -C _build
```

to see if anything broke.

## Configuration

phoc's behaviour can be configured via `GSettings`. For your convienience,
a set of scripts to manipulate config values is available in `helpers`
directory.

- `scale-to-fit` toggles automatic scaling of applications that don't fit
  the screen. This setting is enabled per application using its reported
  app-id. For instance, to enable scaling of GNOME Maps windows use:

      ./helpers/scale-to-fit org.gnome.Maps on

- `auto-maximize` toggles automatic maximization of Wayland windows.
  Disabling it allows windows to be resized and moved, which may be desired
  when running phoc on desktop-like setups.

      ./helpers/auto-maximize off

Outputs are configured via `phoc.ini` config file - see [`src/phoc.ini.example`][3]
for more information.

## Debugging

phoc uses glib so the `G_MESSAGES_DEBUG` environment variable can be
used to enable more log messages and `G_DEBUG` to assert on warnings
and criticals. The log domains all start with `phoc-` and are usally
`phoc-<sourcefile>`. All wlroots related messages are logged with
`phoc-wlroots`.
For more details on these environment variables, read the [documentation for GLib][4].

There's also a `PHOC_DEBUG` enviroment variable to turn on some debugging
features. Use `PHOC_DEBUG=help phoc` to see supported flags.

## API docs

API documentation is available at <https://world.pages.gitlab.gnome.org/Phosh/phoc/>

[1]: https://gitlab.freedesktop.org/wlroots/
[2]: debian/control
[3]: src/phoc.ini.example
[4]: https://docs.gtk.org/glib/running.html#environment-variables
