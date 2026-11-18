# Large scenes: camera, scale and shadows for map-sized worlds

Most of Strata's examples and golden scenes are human-scale, with a village a
few dozen units across. A map viewer works in map units: a world a few hundred
units wide, hills only a few units high, and markers a few units across.
Nothing in the API changes at that scale. But the defaults that suit a village
(the near and far planes, `shadow_distance`, the camera height) give
surprising results there, and finding out why can take several test renders.
This page collects those choices in one place.

| Choice | What it controls | Where |
|---|---|---|
| Near and far planes | Depth precision, fog, how far shadows can reach | `strata_camera_look_at`, `StrataCamera` |
| Camera height and pitch | How much of the terrain's relief shows | the eye and target you pass |
| Height exaggeration | How tall the relief is compared with the world's width | the heights in `StrataTerrainTile` |
| `shadow_distance` | How far shadows reach, and how sharp they are | `StrataLighting` |
| Marker size | How large markers look against the terrain | the scale you give primitives and meshes |

## Near and far planes

`strata_camera_look_at` takes `near_plane` and `far_plane`, and
`StrataCamera` carries them alongside the matrices. Three things depend on
them:

- **Depth precision.** A perspective depth buffer puts most of its precision
  just beyond the near plane. The window surface asks for a 24-bit depth
  buffer (`src/platform/sdl_surface.cpp`). A near plane of 0.1 with a far plane
  in the thousands leaves little precision for terrain hundreds of units away,
  and distant slopes and markers can z-fight. For a world a few hundred units
  across, a near plane around 0.5–1 is usually enough. Raise it as far as the
  closest thing the camera ever needs to see allows.
- **Fog.** With `STRATA_LIGHTING_SUN` and `fog` on, distance fog begins at
  `0.35 × far_plane` from the eye and reaches its strongest (85 % fog colour)
  at `far_plane` (`src/render/renderer.cpp`, the sun fragment shader in
  `src/render/shaders.hpp`). The far plane therefore sets the haze as well as
  the clipping. If it is set just past the far edge of the map, the far side
  fades into the horizon colour. If it is set far beyond the map, there is
  almost no fog. Pick it for the look you want, or turn `fog` off.
- **Shadows.** Cascades are fitted from the near plane out to
  `shadow_distance`, capped at `far_plane` (`src/render/cascades.cpp`). A
  `shadow_distance` larger than the far plane has no effect beyond it.

A rough starting point for a world W units across, seen whole (a rule of
thumb, not a measured limit): a far plane of about 1.5–2 × W, and a near
plane of about W / 500 to W / 400.

## Camera height and terrain relief

What decides how much relief shows is the camera's height *compared with the
terrain's relief*, not the world's width. Hills 2–4 units high in a 400-unit
world, seen from a camera about 4 units up and looking at the horizon, flatten
into a thin strip: almost every slope is seen edge-on.

- For low, cinematic shots, put the eye a few times higher than the tallest
  relief near it (with 2–4-unit hills, about 9–13 units). Aim the target at a
  point a few units ahead and slightly below the eye, rather than level with
  the horizon.
- For an overview, pitch down more steeply and move back. A shallower view of
  a large area needs a larger `shadow_distance` (below).
- Exaggerate heights in the terrain itself, not the camera. `strata_set_terrain`
  takes the heights as given, so multiply them (for example by 2) when you
  fill `StrataTerrainTile::heights`. Normals are computed from those heights
  (`src/render/terrain_mesh.cpp`), so the lighting follows the exaggerated
  slopes. A non-uniform scale in the view or projection does not do this, and
  it distorts everything else in the frame.

## Shadow distance

`shadow_distance` (default 150) is the farthest distance from the eye that
receives sun shadows. Past it, shadows fade out. Its interaction with
`shadow_map_size` is described under `STRATA_LIGHTING_SUN` in
[ARCHITECTURE.md](../ARCHITECTURE.md). In brief, a
shadow texel is `2 × radius / shadow_map_size`, and each cascade's radius
grows with its slice of the distance from the near plane to `shadow_distance`.

- For a map seen whole, raise `shadow_distance` so it covers the visible
  terrain. Otherwise the far half of the map shows no shadows.
- A longer distance makes every texel larger. Hills still cast shadows that
  read well, but small markers may cast none. Use four cascades
  (`cascade_count = 4`, the default) so the near ground keeps fine texels. If
  you need detail everywhere, raise `shadow_map_size` (up to 4096).
- The engine does not yet fit `shadow_distance` to the view automatically;
  that is tracked in issue #100. Until then, set it from the distance between
  the eye and the far edge of what the camera frames.

## Marker size

Markers sized for a top-down map view look far too large in a low shot, where
they stand against hills only a few units high. A marker about 6 units across
next to 3-unit hills dominates the frame.

- Scale markers with the shot. In one map viewer, about 0.55 × the map-view
  size worked for low shots.
- Size markers against the terrain's relief, not the world's width: a marker
  taller than the hills around it reads as a tower, not a settlement.
- An overlay point can be sized in pixels, in world units, or both (its
  diameter is the larger of `size_pixels` and its projected `world_diameter`;
  see Overlays in [ARCHITECTURE.md](../ARCHITECTURE.md)). A pixel minimum
  keeps a map marker legible in a wide shot without making it huge in a low
  one.

## Not covered

- Fitting `shadow_distance` to the view automatically (issue #100).
- Water planes and the horizon colour at grazing angles (issue #98).
- Low-sun lighting, which is the same at every scale. Why a sun near the
  horizon draws a dark frame, and the light levels that brighten it, are
  under `STRATA_LIGHTING_SUN` in [ARCHITECTURE.md](../ARCHITECTURE.md)
  (issue #99).
- A map-scale example under `examples/`. Only `examples/village-day` exists so
  far.
