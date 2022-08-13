phoc_anim_inc = include_directories('.')

phoc_anim_enum_headers = files([
  'phoc-anim-enums.h',
])

phoc_anim_enum_sources = gnome.mkenums_simple(
  'phoc-anim-enum-types',
  sources : phoc_anim_enum_headers)

phoc_anim_sources = files(
  'animatable.c',
  'animatable.h',
  'easing.c',
  'easing.h',
  'property-easer.c',
  'property-easer.h',
  'timed-animation.c',
  'timed-animation.h',
)
