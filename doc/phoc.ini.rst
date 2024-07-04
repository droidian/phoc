.. _phoc.ini(5):

========
phoc.ini
========

---------------------------
Configuration file for phoc
---------------------------

DESCRIPTION
-----------

Most of ``phoc``'s configuration is read from GSettings. Output and
core settings are read from ``phoc.ini``. The configuration file is
searched in phoc's current working directory. It's path can also
be given on the command line via the `-C` option.

The phoc.ini file is composed of multiple sections which can be in any
order. If values are not given then built in defaults are used. The main
purpose of `phoc.ini` is to specify output configuration and layouts.
Each section has the form:

::

  [SectionHeader]
  key1 = value1
  key2 = value2

Lines starting with `#` are considered comments and are ignored. The section
headers might contain a colon in which case the part after the colon specifies
the configuration item e.g.

::

  [output:DSI-1]


is the configuration section for the output `DSI-1`.

The available section headers are:

- `core`: core options
- `output`: output configuration

Modifications to this file require a compositor restart to take effect.

CORE SECTION
------------

The core section can appear only once and has a single option:

- ``xwayland=[true|immediate|false]``: Whether to enable
  XWayland. With `true` XWayland is activated when,
  needed. `immediate` launches it immediately and `false` turns it off.

OUTPUT SECTION
--------------

The output section can appear multiple times with different
configuration items

::

  [output:ITEM]

`ITEM` is either the DRM connector name like `DSI-1` or `DP-2` *or* the make, model and serial
as obtained from EDID separated by `%`: `[output:A Vendor%The Model%Serial]`. Make, model and serial
can be specified as `*` to match any value. The configuration options are:

- `enable=[true|false]`: Whether the output should be enabled
- `x`, `y`: The `x` and `y` position in the output layout. This can be used to arrange outputs.
  The default is to use automatic layout (new outputs are added to the right of the current layout).
- `scale`: The outputs scale. The default `auto` calculates the scale based on the screen size.
- `rotate`: The rotation. Valid values are `normal`, `90`, `180`,
  `270`, `flipped`, `flipped-90`, `flipped-180` and `flipped-270`. The default is to use
  `normal` orientation.
- `mode`: The mode to use. The mode must be a valid mode for this output. See e.g. the output of `wlr-randr`.
  The default is to use the outputs preferred mode.
- `modeline`: A custom video mode. This is only valid for the DRM backend
- `scale-filter`: Filter to use to scale down textures. Valid values are `bilinear`, `nearest` and `auto`.
  The later selects `bilinear` for fractional and `nearest` for integer scales automatically. If unset
  `auto` is assumed.
- `drm-panel-orientation`: If `true` applies the panel orientation read from the DRM connector
  (if available). Defaults to `true`.
- `phys_width`, `phys_height`: The physical dimensions of the display in `mm`.

Example:

::

  [output:DSI-1]
  scale = 2

  [output:Some Vendor%Some Model%*]
  scale = 1
  rotate = 90
  x = 300

See also
--------

``phoc(1)`` ``wlr-randr(1)``
