# Getting started with Phoc development

## Overview

Phoc is a [Wayland](https://wayland.freedesktop.org/) compositor based on the
[wlroots](https://gitlab.freedesktop.org/wlroots) library. It's often
used as a compositor for
[phosh](https://gitlab.gnome.org/World/Phosh/phosh), but works
perfectly well on its own.

## Running

After
[building](https://gitlab.gnome.org/World/Phosh/phosh/-/blob/main/README.md)
phoc, it can be run in several ways.

### Running nested

The easiest way is to run it nested within another graphical session. The
supplied run script is able to detect that:

```sh
    ./_build/run -E gnome-terminal
```

This will launch phoc with either the X11 or Wayland backend rendering
everything within a window in your current graphical session. The `-E`
option specifies a program to be started by phoc, in this case we use
gnome-terminal but any other application that opens a window will do.

### Running on hardware

The simplest way is to log into a terminal and use the same invocation as
above:

```sh
    ./_build/run -E gnome-terminal
```

The script will notice that neither `$DISPLAY` nor `$WAYLAND_DISPLAY` is set
and hence use the DRM backend.

Sometimes it isn't easy to get to a virtual terminal and also have keyboard
input (e.g. on a mobile phone). To help with that there's a systemd unit.
This will start phoc on a tty for the user with uid `1000`:

```sh
    cp _build/data/phoc-dev.service /etc/systemd/system/
    systemctl daemon-reload
    systemctl start phoc-dev
```

Make sure you stop other graphical session and display managers first.
If you want to override some settings (e.g. a different user id to run phoc
with) put these in `/etc/systemd/system/phoc-dev.service.d/override.conf`).

The systemd unit makes sure it picks up phoc from the freshly built source tree
so whenever you made any changes it's enough to invoke:

```sh
    systemctl restart phoc-dev
```

to run your modified version. Note that the systemd unit doesn't launch phoc
directly but via a small shell wrapper at `_build/data/phoc-dev` which is another
good place to make some temporary changes like setting `PHOC_DEBUG` or
`G_MESSAGES_DEBUG`.

To start an appplication in the running phoc run:

```sh
    WAYLAND_DISPLAY=wayland-0 gnome-terminal
```

Make sure you do this as the same user phoc runs with (uid 1000 by default).

Note that the systemd unit is meant for development purposes only.
