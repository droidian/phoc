# Contributing to Phoc

Below are some basic guidelines on coding style and merge requests
that hopefully makes it easier for you to land your code.

## Building

For build instructions see the README.md

### Merge requests

Before filing a pull request run the tests:

```sh
meson test -C _build
```

Use descriptive commit messages, see

   <https://wiki.gnome.org/Git/CommitMessages>

and check

   <https://wiki.openstack.org/wiki/GitCommitMessages>

for good examples.

## Coding Patterns

### Coding Style

We're mostly using [libhandy's Coding Style][1].

These are the differences:

- We're not picky about GTK+ style function argument indentation, that is
  having multiple arguments on one line is also o.k.
- Since we're not a library we usually use `G_DEFINE_TYPE` instead of
  `G_DEFINE_TYPE_WITH_PRIVATE` (except when we need a derivable
  type) since it makes the rest of the code more compact.

## Function names

New public functions and structs should have a `phoc_` prefix for consistency
and so they get picked up with documentation builds

## `wl_listener` callbacks

Callbacks for `wl_listener` should be prefixed with `handle_`:

```c
  self->keyboard_key.notify = handle_keyboard_key;
  wl_signal_add (&device->keyboard->events.key,
                 &self->keyboard_key);
```

## GObject signal callbacks

Callbacks for GObject signals should be prefixed with `on_`.

```c
  g_signal_connect_swapped (keyboard, "device-destroy",
                            G_CALLBACK (on_keyboard_destroy),
                            seat);
```

## Examples

The `examples/` folder contains Wayland clients that exercise certain
protocols.  This is similar in spirit to [Westons clients][2] or
[wlr-clients][3].

- phosh-private: A client for the [phosh-private protocol](./protocols/phosh-private.xml)
- device-state: A client for the [phoc-device-state-unstable-v1 protocol](./protocols/phoc-device-state-unstable-v1.xml)
- layer-shell-effects: A client for the [phoc-layer-shell-effects-unstable-v1 protocol](./protocols/phoc-layer-shell-effects-unstable-v1.xml)

You can run them against any phoc instance.

[1]: https://gitlab.gnome.org/GNOME/libhandy/-/blob/main/HACKING.md
[2]: https://gitlab.freedesktop.org/wayland/weston/-/tree/main/clients
[3]: https://gitlab.freedesktop.org/wlroots/wlr-clients
