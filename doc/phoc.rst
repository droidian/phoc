.. _phoc(1):

====
phoc
====

---------------------------------------
A Wayland compositor for mobile devices
---------------------------------------

SYNOPSIS
--------
|   **phoc** [OPTIONS...]


DESCRIPTION
-----------

``phoc`` is a Wayland compositor for mobile devices using the
``wlroots`` library. It's often used with the ``phosh`` mobile shell
but works perfectly fine on its own.

OPTIONS
-------

``-h``, ``--help``
   Print help and exit
``-C``, ``--config FILE``
   Path to the configuration file. (default: phoc.ini).
``-E``, ``--exec EXECUTABLE``
   Executable (session) that will be run at startup
``-S``, ``--shell``
   Whether to expect a shell to attach
``-X``, ``--xwayland``
   Whether to start XWayland
``--version``
   Show version information

ENVIRONMENT VARIABLES
---------------------

``phoc`` honors the following environment variables:

- ``WLR_BACKENDS``: The backends the wlroots library should use when phoc launches. See
  https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/docs/env_vars.md
- ``WLR_RENDERER``: The renderer the wlroots library should use when phoc launches. See
  https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/docs/env_vars.md
- ``G_MESSAGES_DEBUG``, ``G_DEBUG`` and other environment variables supported
  by glib. https://docs.gtk.org/glib/running.html  
- ``PHOC_DEBUG``: Comma separated list of debugging flags:

      - ``auto-maximize``: Maximize toplevels
      - ``damage-tracking``: Debug damage tracking
      - ``no-quit``: Don't quit when session ends
      - ``touch-points``: Debug touch points
      - ``layer-shell``: Debug layer shell
      - ``cutouts``: Debug display cutouts and notches

See also
--------

``phosh(1)``
