.. _phoc.gsettings(5):

==============
phoc.gsettings
==============

----------------------
Gsettings used by phoc
----------------------

DESCRIPTION
-----------

Phoc uses gsettings for most of its configuration. You can use the ``gsettings(1)`` command
to change them or to get details about the option. To get more details about an option use
`gsettings describe`, e.g.:

::

   gsettings describe sm.puri.phoc auto-maximize

These are the currently used gsettings schema and keys:

GSettings
~~~~~~~~~

These gsettings are used by ``phoc``:

- `sm.puri.phoc`

    - `automaximize`
    - `scale-to-fit`

- Animations: `org.gnome.desktop.interface`

    - `enable-animations`
- Touchscreen: `org.gnome.desktop.peripherals.touchscreen`
- Pointer: `org.gnome.desktop.peripherals.mouse`

    - `middle-click-emulation`
    - `natural-scroll`
    - `speed`
- Touchpad: `org.gnome.desktop.peripherals.touchpad`

    - `disable-while-typing`
    - `edge-scrolling-enabled`
    - `left-handed`
    - `middle-click-emulation`
    - `speed`
    - `tan-and-drag-lock`
    - `tap-and-drag`
    - `tap-to-click`
    - `two-finger-scrolling-enabled`
- Keybindings: `org.gnome.desktop.wm.keybindings`

    - `always-on-top`
    - `close`
    - `cycle-windows-backward`
    - `cycle-windows-backward`
    - `cycle-windows`
    - `maximize`
    - `move-monitor-up`, `move-monitor-down`, `move-monitor-left`, `move-monitor-right`,
    - `move-to-corner-ne`, `move-to-corner-nw`, `move-to-corner-se`, `move-to-corner-sw`
    - `switch-applications-backward`
    - `switch-applications`
    - `switch-input-source`
    - `toggle-fullscreen`
    - `toggle-maximzed`
    - `toggle-tiled-left`
    - `toggle-tiled-right`
    - `unmaximize`

See also
--------

``phoc(1)`` ``phoc.ini(5)`` ``gsettings(1)``
