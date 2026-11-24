# Decal Stamp Feature Plan

Atlas decals ("stamps") placed in TrenchBroom, previewed on brushes, and imported into
cyberjunk as URP decal projectors. Modelled on Source's `info_overlay` and on the placement
workflow of idStudio's Decal Editor (DOOM Eternal's projected decals, not its geo decals).

## Implementation status (2026-11-23)

Milestones 0–3 and most of 4 are implemented; `ForkFeatures.md` section 16 describes
what exists. Differences from the plan below:

- **Importer location:** `DecalImporter` lives in `com.junk.scopa/Junk.Scopa.Editor/Decals/`
  and hooks `MapImporter.OnMapImported` (like `PrefabImporter`), not a Scopa partial.
  `DecalLibrary` lives there too, so the exporter and the importer share it.
- **Rotation:** Scopa's `TryGetAngles3D` turns roll the opposite way from TrenchBroom
  (checked numerically). The importer computes the projector rotation from `angles` with
  TrenchBroom's convention instead. Prefabs imported through Scopa are still affected.
- **Rendering layer:** the existing "Statics" layer (bit 1), which Scopa already gives
  brush geometry, is the decal receiver layer. No new layer was added.
- **Wheel bindings:** Alt+wheel turns (15°) and Alt+Shift+wheel scales (×1.1), because
  Ctrl+wheel (drill selection) and Ctrl+Alt+wheel (grid) are taken.
- **Decal-only export:** a library or atlas change re-exports only the decal sections,
  not the slow prefab export.

Not done yet:

- Spike: play-mode and Player-build check of the companion projectors (baking was
  confirmed: `CompanionComponent<DecalProjector>` + `CompanionLink`).
- Sliding placed decals across surfaces by dragging; grid snapping of the stamp origin.
- A textured hover preview (the tool draws the outline and box; the decal itself shows
  once placed) and angle-fade alpha in the preview.
- Cleanup of `Assets/Scripts/Junk.Decals/` and its test objects in `DOTS Resources.unity`.

This plan covers both repositories:

- **TrenchBroom fork** (this repo): decal library loading, decal browser, stamp tool,
  in-editor preview.
- **cyberjunk** (`C:\Users\thele\Documents\Projects\cyberjunk`): the decal library asset,
  the catalog export (`com.junk.trenchbroom`), and the map import (`com.radiatoryang.scopa`).

## Goals

- Add surface detail (markings, stripes, panel seams, signage, grime) without unique
  textures.
- Stamp decals from a thumbnail library onto brush surfaces. The preview follows the
  cursor and lines up with the surface. Snapped rotate and scale on the mouse wheel.
- The `.map` is the source of truth. Decals survive reimport and need no Unity-side
  placement.
- First pass runtime: stock URP `DecalProjector`. The runtime must be replaceable later
  (baked overlay meshes or an Entities-native projector) without re-authoring maps.

## Non-goals (first pass)

- Per-decal channel weights at runtime (URP only has a per-projector `fadeFactor`). The
  weights are stored in the map anyway; see [Later](#later).
- Decals in the TrenchBroom preview on prefab models (`models/<guid>.obj`). They still
  render in Unity, because the projector hits anything in its box.
- Dynamic or impact decals. Those stay in `ContactWorldSystem`'s quad path.
- Geo decals (decal geometry authored on props in a DCC tool).

## Current state

**cyberjunk**
- `Assets/Scripts/Junk.Decals/DecalProjectorAuthoring.cs` is a copy of URP's
  `DecalProjector` with no baker and no subscribers to its add/remove events. It has never
  rendered. It is only used by a test object in `Assets/Scenes/Main/DOTS Resources.unity`.
  **It gets deleted as part of this work.**
- `Default Renderer.asset` has `DecalRendererFeature` on (technique Automatic, surface
  data Albedo+Normal+MAOS, decal layers on).
- Entities `CompanionComponentSupportedTypes` registers
  `UnityEngine.Rendering.Universal.DecalProjector`, so a projector inside a SubScene is
  baked as a companion GameObject.
- Scopa's `MapImporter` runs `PostProcess*` partials per entity type
  (`MapImporterLights.cs` is the template to follow).
- `com.junk.trenchbroom` exports `<project>/TrenchBroom/` (`catalog.json`, `entities.fgd`,
  `models/`, `thumbnails/`, `textures/`). The core FGD comes from
  `TrenchBroomExport.CoreFgdAssetPath`.

**TrenchBroom**
- `common/src/unity/UnityCatalog.*` parses `catalog.json`. It reads only `prefabs` and
  ignores unknown keys, so adding an optional `decals` section does not break older builds.
  The catalog stays at version 1.
- `UnityCatalogWatcher` reloads when `catalog.json` changes.
- `UnityPrefabBrowser` is the template for the decal browser (thumbnails, category filter,
  search, drag to place).
- Upstream already has `render/EntityDecalRenderer.*` and `mdl/DecalDefinition.*` for
  Quake `infodecal`. That renderer projects a whole material onto the brush faces the
  entity's bounds intersect, aligned with the face's UV axes and clipped against the
  brush, and it tracks which brushes each decal depends on. Its invalidation and clipping
  approach is reused here. Its projection is not, because stamps use their own oriented box.

## Data model

### Decal library (Unity, source of truth)

A `DecalLibrary` ScriptableObject in cyberjunk, one per atlas:

| Field | Meaning |
|---|---|
| `id` | Atlas id, lowercase, for example `markings` |
| `material` | URP decal material (Decal Shader Graph) that samples the atlas |
| `previewTexture` | Albedo texture exported for the TrenchBroom preview |
| `entries[]` | `name`, `category`, `rect` (pixels in the atlas), `defaultSize` (meters) |

All decals in one atlas share one material, so they instance into one batch.

A decal's full id is `<atlas>/<name>`, for example `markings/strips_med_detail_2`. Ids must
be unique across libraries. The exporter reports duplicates as errors.

### Catalog export

The exporter writes `TrenchBroom/decals/<atlas>.png`. It blits through a `RenderTexture`,
so compressed or non-readable textures still export. It also adds two optional arrays to
`catalog.json`, which is still written last:

```json
{
  "version": 1,
  "prefabs": [ ... ],
  "decal_atlases": [
    { "id": "markings", "texture": "decals/markings.png", "width": 2048, "height": 2048 }
  ],
  "decals": [
    {
      "id": "markings/strips_med_detail_2",
      "display_name": "Strips Med Detail 2",
      "category": "markings",
      "atlas": "markings",
      "rect": [0.25, 0.5, 0.125, 0.0625],
      "default_size": [32, 16]
    }
  ]
}
```

- `rect` is `[u, v, width, height]`, normalized, with the origin at the **bottom left**
  (Unity/GL UV convention). That maps directly to `DecalProjector.uvBias = (u, v)` and
  `uvScale = (width, height)`. TrenchBroom converts it if its image rows are flipped. A
  unit test pins the convention down.
- `default_size` is in map units (already converted with Scopa's `scalingFactor`).
- There are no per-decal thumbnails. TrenchBroom crops tiles from the atlas PNG.

### Map entity

The class `decal` is defined in the core FGD, so it is always available:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `origin` | vec3 | | Box center. The tool puts it on the surface. |
| `angles` | "pitch yaw roll" | `0 0 0` | Box orientation. Forward (+X) points **into** the surface. Roll spins the decal on the surface. |
| `_decal` | string | | Library id, `<atlas>/<name>` |
| `size` | "w h" | from library | Footprint in map units |
| `depth` | float | 16 | Projection depth, centered on the origin |
| `opacity` | 0–1 | 1 | → `DecalProjector.fadeFactor` |
| `angle_fade` | "start end" (degrees) | `30 60` | → `startAngleFade` / `endAngleFade` |
| `flip` | "u v" (0/1) | `0 0` | Mirrors the rect (negative `uvScale`) |
| `_weights` | "albedo normal maos emissive" | `1 1 1 1` | Stored only. Not used by the URP runtime. |

`angles` reuses the fork's rotation-property handling (feature 3). Scopa's
`TryGetAngles3D("angles")` already turns it into a transform whose forward matches the
TrenchBroom forward (see the comment in `MapImporterLights.cs`).

## TrenchBroom work

### 1. Library loading — `common/src/unity/UnityCatalog.*`, new `UnityDecalLibrary.*`
- Extend `UnityCatalog` with `decalAtlases` and `decals`. A missing section is an empty
  list. A malformed entry fails the parse, as malformed prefabs already do.
- A `UnityDecalLibrary`, owned by `MapDocument` next to `UnityCatalogWatcher`, loads each
  atlas PNG once (`io::readFreeImageTexture` → `mdl::Texture` → a `mdl::Material` used only
  for rendering) and resolves an id to atlas material plus rect. It reloads with the
  catalog. Reloading must not modify the map or undo history (same rule as texture
  folders).

### 2. Entity bounds — `common/src/mdl/EntityNode.*`
- For class `decal`, logical bounds are the AABB of the oriented box
  (`size.x × size.y × depth`, rotated by `angles`). This drives picking, selection, node
  tree queries and the brush-intersection lookup. Bounds are invalidated when `size`,
  `depth` or `angles` change.

### 3. Preview rendering — extend `render/EntityDecalRenderer.*`
- Keep the upstream `infodecal` path unchanged. Add a second path for `decal` entities that
  reuses the dependency tracking (`brushes`, `validated`, VBO blocks) and the
  intersector query.
- Geometry per decal:
  1. Build the oriented box: `fwd`, `right`, `up` from `angles`; half extents
     `(depth/2, w/2, h/2)`.
  2. For each visible brush that intersects the box AABB, take each face whose
     `dot(normal, -fwd) > cos(angle_fade.end)`.
  3. Clip the face polygon against the 6 box planes (`vm::polygon_clip_by_plane`), then
     against the brush's other faces (as upstream already does).
  4. UV = `(dot(p - origin, right) / w + 0.5, dot(p - origin, up) / h + 0.5)`, then flip,
     then map into the atlas rect.
  5. Push the result a small distance along the face normal, or use polygon offset, to
     avoid z-fighting.
- One face batch per atlas material. Fade alpha between `angle_fade.start` and `end`.
- Selected decals also draw their box as wireframe.
- An unknown `_decal` id draws the box in the error color and logs once per id.

### 4. Decal browser — new `common/src/unity/UnityDecalBrowser.*`
- Modelled on `UnityPrefabBrowser`: tiles cropped from the atlas, category filter, search.
- An inspector page placed after the Unity tab, so existing page indices still hold (same
  rule as feature 15).
- Clicking a tile arms the stamp tool with that decal. Dragging a tile into the 3D view
  places one decal at the drop point.

### 5. Stamp tool — new `common/src/ui/DecalStampTool.*` + controller
Registered like `RoomDrawTool` (`MapViewToolBox`, `MapView3D::createTools`).

**Stamp mode** (armed from the browser):
- Hovering picks a brush face (`BrushNode::BrushHitType`). The preview box sits at the hit
  point with `fwd = -face.normal`. `up` is world Z projected onto the face; on floors and
  ceilings it is the camera's up projected instead. Then the current stamp roll is applied.
- The preview uses the same renderer path (a temporary decal that is not in the map).
- Left click places a `decal` entity (one transaction) and stays armed for repeated
  stamping. Esc disarms.
- Ctrl+wheel scales logarithmically (×1.1 per notch, Ctrl+Shift for ×1.02).
  Shift+wheel rotates in 15° steps (Alt for free rotation). A plain wheel still moves the
  camera. **Check these modifiers against existing camera and tool bindings before
  committing to them.**
- Aspect is locked to the library rect. Changing aspect means editing `size` directly.
- Optional: with grid snap on, the origin snaps to the grid within the face plane.

**Editing placed decals** (tool active, one or more decals selected):
- Dragging a selected decal slides it over surfaces. Each move re-picks the face under the
  cursor and re-aligns `fwd`, so a decal dragged around a corner turns onto the new face.
  One transaction per drag.
- The same wheel bindings rotate and scale the selection. A burst of wheel events forms
  one transaction, ending on mouse move or after about 500 ms idle.

The stamp settings (current decal, roll, scale) persist while the tool stays armed.

## cyberjunk work

### 6. Library asset and exporter — `com.junk.trenchbroom`
- `DecalLibrary` ScriptableObject plus a basic inspector (the entry list with a rect
  preview over the atlas; visual rect editing can come later).
- `DecalCatalogExporter` runs inside the existing catalog export, before `catalog.json`
  is written. It writes `decals/<atlas>.png` and the two arrays.
- Add the `decal` point class to the core FGD.

### 7. Import — new `com.radiatoryang.scopa/Editor/Importers/MapImporterDecals.cs`
- `PostProcessDecals(mapObject, scalingFactor)`, called after `PostProcessLights`.
- For each `MapComponent` with class `decal`:
  - Resolve `_decal` through an editor-side registry (every `DecalLibrary` asset, id →
    material and rect). If the id is missing, call `ctx.LogImportWarning` and skip.
  - Add a `DecalProjector`:
    - `material` = atlas material
    - `size = (w, h, depth) / scalingFactor`, matching the lights' unit conversion
    - `pivot = Vector3.zero`, because URP defaults to `(0, 0, 0.5)` and our origin is the
      box center
    - `uvBias` / `uvScale` from the rect, with flip applied
    - `fadeFactor = opacity`
    - `startAngleFade` / `endAngleFade` from `angle_fade`
    - `renderingLayerMask` = the World decal layer
  - Mark the GameObject static.
- Call `ctx.DependsOnSourceAsset` for each library used, so editing a rect reimports the
  maps that use it.

### 8. Rendering layers
- Add a "World" rendering layer. Static map geometry receives it; characters, weapons and
  dynamic props don't. Otherwise a character walking through a wall decal's box gets
  painted.
- Confirm "Use Rendering Layers" is on in `DecalRendererFeature`.

### 9. Cleanup
- Delete `Assets/Scripts/Junk.Decals/` (including the `UniversalRP.Runtime` asmref
  `InternalsVisibleTo` hack) and the two decal test objects in `DOTS Resources.unity`.
  First confirm that no asmdef references `Junk.Decals`.

## Milestones

0. **Spike (half a day):** put a stock `DecalProjector` with an atlas material in a
   SubScene, enter play mode, and make a Player build. Confirm it renders through the
   companion path, and that rendering layers keep it off characters. Everything else
   depends on this.
1. **Data path:** steps 6, 7, 8 and the FGD class. A `decal` entity typed by hand in
   TrenchBroom shows up correctly in Unity after save and reimport.
2. **Preview:** steps 1, 2, 3. Hand-placed decals render on brushes in TrenchBroom and
   match Unity in position, orientation, UV and flip.
3. **Authoring:** steps 4 and 5 (browser and stamp mode).
4. **Editing:** sliding, rotating and scaling placed decals; grid snap; step 9 cleanup;
   update `ForkFeatures.md` with a new section.

## Testing

**TrenchBroom** (`common/test/src/unity/`, Catch2):
- `tst_UnityCatalog.cpp`: decal sections parse; missing sections give empty lists; a
  malformed decal entry or unknown atlas reference is rejected; prefab parsing is unchanged.
- New `tst_DecalProjection.cpp`:
  - a decal inside one face gives one quad with its corners on the rect corners
  - a decal across a convex corner gives two polygons, one per face
  - faces past `angle_fade.end` are excluded
  - flip mirrors the UVs
  - the depth bound excludes faces outside the box
- Entity bounds follow `size`, `depth` and `angles`, and are invalidated when they change.
- New `tst_DecalStampTool.cpp`: placement creates one entity with the expected properties
  and one undo step; rotation snaps to 15°; scale keeps the aspect.

**cyberjunk** (EditMode tests):
- Importing a test map with a `decal` entity gives a `DecalProjector` with the expected
  size, pivot, rotation, `uvBias`/`uvScale` and `fadeFactor`.
- A missing id logs a warning and adds no projector.
- The exporter writes the atlas PNG and the decal arrays, and flags duplicate ids.

## Open questions

- Is the class name `decal` free of clashes with existing FGD classes?
- Which wheel modifiers are actually free in TrenchBroom's 3D view?
- Should TrenchBroom's preview eventually clip against prefab preview models too? (This
  matters most for decals on props.)
- Should one decal library entry allow several sizes or variants, or does the stamp's
  scale cover that?

## Later

- **Per-channel weights:** generate material variants per weight set at import, or move to
  a custom runtime.
- **Baked overlay meshes:** at import, clip against brush meshes using the same math as
  TrenchBroom step 3 (C# port) and output static meshes, rendered by Entities Graphics with
  no companion objects. Worth checking whether URP decal shaders render on these meshes
  through Entities Graphics (DBuffer mesh decals).
- **Entities-native projector:** a renderer feature that reads decal entities directly,
  removing the companion GameObjects.
- **Decal sets / randomization:** pick randomly from a category on each stamp.
- **Visual rect editor** for `DecalLibrary`, and auto-slicing atlases by alpha islands.
