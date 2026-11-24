# TrenchBroom Unity fork: feature reference

This document inventories the custom features in this checkout for future maintenance
and rebasing. It describes the current implementation, rather than promising that each
feature is finished. Source paths are relative to the repository root.

Dedicated fork regression tests cover Unity entity identity (`_tb_unique_id` and
`classindex`), startup defaults, and texture folders. TrenchBroom's original upstream
test suites remain.
The standalone fork tests for other features, including the room creator prototype,
have been removed. Feature implementations remain in place.

## 1. Stable entity IDs for Unity

**Purpose:** Give downstream importers a persistent way to recognize an entity across
map edits and reimports.

**Use:** Enable **Generate unique entity identifiers** in the Unity preferences pane.
It is off by default. Entities receive a nonempty `_tb_unique_id` string, generated as a UUID
when no usable unique value is present. Existing nonempty IDs are preserved when they
do not collide with another entity registered in the map. Duplicated or pasted entities
receive a replacement when their copied ID collides.

The registry is rebuilt when a map is created or loaded and when the preference is
enabled. Added and removed entities update the registry, including entities nested
inside groups. Turning generation off stops automatic assignment; it does not strip
existing properties from the map. Worldspawn is not assigned an entity ID.

**Main files:** `common/src/mdl/Map.cpp`, `Map.h`, `EntityProperties.cpp`,
`EntityProperties.h`, `common/src/Preferences.cpp`, and
`common/src/unity/UnityPreferencePane.*`.

**Rebase considerations:** Preserve the exact `_tb_unique_id` property name and saved
values. Reconnect identity registration to the new upstream map lifecycle and node
notifications. Duplication, paste, deletion/undo and save/load must not accidentally
reuse a live entity's ID or replace an unchanged entity's ID.

**Retained tests:** `common/test/src/mdl/tst_UnityEntityIdentity.cpp` covers generation,
duplicate/empty ID repair, copy/paste, duplication, nested entities, transforms,
deletion, undo/redo, preference changes, and Standard/Valve save/load. Older identity
checks also remain in `common/test/src/mdl/tst_Map_Entities.cpp`.

## 2. Per-class entity indices

**Purpose:** Provide an additional numeric identifier within each entity classname for
downstream workflows.

**Use:** Enable **Generate unique entity class indices** in Unity preferences. It is off
by default. The saved property is `classindex`, a positive integer string. Classnames have
independent number spaces: a `light` and an `info_player_start` may both have index 1.
Valid unclaimed indices are preserved; missing, invalid or conflicting values are
assigned an available index. The allocator searches from 1, so indices are not a
guaranteed monotonically increasing sequence. Worldspawn and editor group/layer
containers are excluded.

**Update Current Map Entities** rebuilds the current map's class-index registry.
Disabling generation leaves existing values in place.

**Main files:** `common/src/mdl/Map.cpp`, `Map.h`, `EntityProperties.cpp`,
`EntityProperties.h`, and `common/src/unity/UnityPreferencePane.*`.

**Rebase considerations:** Preserve `classindex`, classname-specific ownership,
collision repair and existing saved indices. Pay attention to entity removal,
duplication, classname/property edits, and registry rebuilding after loading.

**Retained tests:** The same identity test file checks independent classname sequences,
malformed/duplicate imported values, persistence, preference changes and undo/redo.

## 3. Automatic entity defaults and rotation properties

**Purpose:** Make newly created entities carry useful default properties and rotation
metadata without requiring manual property entry.

**Use:** **Automatically set default entity properties** in Misc preferences is on by
default. Point-entity creation applies values from the entity definition when enabled.
The point-entity creation path also ensures the appropriate rotation property exists,
using a definition default when available and a zero rotation otherwise. This rotation
initialization is separate from the general defaults preference. Map entity settings
also enable rotation-property updates after transforms.

**Main files:** `common/src/mdl/Map_Entities.cpp`, `Map.cpp`,
`common/src/Preferences.cpp`, and `common/src/ui/MiscPreferencePane.cpp`.

**Rebase considerations:** Keep entity creation, definition defaults and rotation
updates consistent with upstream's revised entity configuration. Check that applying
defaults does not overwrite explicitly edited values during unrelated operations.

## 4. Prefab library and placement

**Purpose:** Save reusable selections as `.map` files and place them from a dedicated
prefab inspector/browser.

**Use:** Save the current map first, then use **Create Prefab From Selection** in the
prefab inspector. The library lives in a `prefabs` folder beside that map. Discovery
lists top-level `.map` files, accepts the extension case-insensitively and sorts entries
by display name. The implementation reserves a `prefabs/thumbnails` path.

The browser supplies `prefab:` file-URL payloads. Dropping one reads its map contents,
pastes the nodes through the normal map pipeline and positions them using the view's
paste-placement logic. Insertion is grouped into an **Insert Prefab** transaction.
Saving into another directory switches the associated prefab library; clearing the
map disables it.

**Main files:** `common/src/ui/PrefabLibrary.*`, `PrefabBrowser.*`,
`PrefabInspector.*`, `PrefabDropController.*`, plus integration in `MapDocument.*`,
`Inspector.*`, `MapFrame.*` and `CreateEntityToolController.cpp`.

**Rebase considerations:** Reconnect document lifecycle notifications, inspector
registration and drag/drop routing. Preserve grouped contents, material/entity data,
placement, and undo behavior. Browser rendering and thumbnails need a visual check.

## 5. Material-browser WAD visibility

**Purpose:** Reduce material-browser clutter by hiding selected WAD collections without
removing them from the map's resource configuration.

**Use:** The material browser's **Hide Wads** menu provides one toggle per collection.
A checked entry is hidden. Collection choices combine the worldspawn `wad` list with
enabled material collections. Hidden paths are stored in the application preference
`Texture Browser/Hidden collections`, rather than written as map edits.

**Main files:** `common/src/ui/MaterialWadFilter.*`, `MaterialBrowser.*`,
`MaterialBrowserView.*`, and `common/src/Preferences.cpp`.

**Rebase considerations:** Preserve preference serialization, collection-path matching
and refresh notifications. Verify hiding a collection only affects browsing, and does
not unload materials used by geometry or modify worldspawn.

## 6. Layer highlighting and labels

**Purpose:** Identify the containing layer of selected entities and brushes in the view.

**Use:** Controlled by **Show layer highlight** and the layer-highlight hotkey in view
preferences. With no explicit hotkey configured, Control, Alt or AltGr activates the
overlay while held. The renderer displays `Layer "name"` above visible selected
entities and brushes. Release, focus loss and relevant preference changes clear the
active state.

**Main files:** `common/src/render/LayerHighlightRenderer.*`, `TextAnchor.h`,
`common/src/ui/MapViewBase.*`, `ViewEditor.*`, and `common/src/Preferences.cpp`.

**Rebase considerations:** Preserve key/focus handling, visibility filtering and label
placement. Upstream changes to rendering, selection or text anchors may need manual
adaptation. Check that labels do not remain visible after the activation key is released.

## 7. Marquee and freeform selection

**Purpose:** Select objects using either a rectangular marquee or a freeform outline.

**Use:** **Edit > Tools > Marquee Select Tool**, default shortcut **M**. Options include
**Center**, **Enclosed** and **Intersecting** selection modes, rectangular/freeform
shapes, and **Select through**. Center tests the projected object center; Enclosed
requires its projected bounds inside the outline; Intersecting accepts an overlap.
Without Select through, hit testing filters out occluded candidates. Ctrl/Cmd toggles
hits within the existing selection; an ordinary gesture replaces it. Selection changes
use a **Marquee Select** transaction.

**Main files:** `common/src/ui/MarqueeSelectTool.*`, `MapViewToolBox.*`,
`MapViewBase.*`, `MapView2D.cpp`, `MapView3D.cpp`, `Actions.cpp`, and
`ActionContext.*`. Icon: `app/resources/graphics/images/MarqueeSelectTool.svg`.

**Rebase considerations:** Preserve projection/depth handling, selectable-node filtering,
closed-group behavior, modifier keys, cancellation and undo. Verify both 2D and 3D views.

## 8. Paint roller

**Purpose:** Apply the current material directly to faces under the cursor without
changing the current object selection.

**Use:** **Edit > Tools > Paint Roller Tool**, default shortcut **P**. A left click
paints the first eligible face immediately; dragging paints additional faces. A stroke
uses a **Paint Faces** transaction and can be cancelled. The tool changes material
names through the explicit-face brush-attribute operation. It ignores faces that the
editor context considers unselectable and does nothing when no material is chosen.

**Main files:** `common/src/ui/PaintRollerTool.*`, `MapViewToolBox.*`, `Actions.cpp`,
`ActionContext.*`, and `common/src/mdl/Map_Brushes.*`.
Icons: `app/resources/graphics/images/PaintRollerTool.svg` and `PaintBrushTool.svg`.

**Rebase considerations:** Preserve first-click event ordering, target-face picking,
selection, non-material face attributes, and one-stroke undo/cancellation behavior.

## 9. Unity reimport and SubScene rebake on save

**Purpose:** Connect saving a map in TrenchBroom to asset refresh and optional SubScene
rebaking in an external Unity project.

**Use:** Configure **Unity project directory** and **Trigger Unity reimport on save**
in Unity preferences. Reimport is off by default. **Trigger SubScene rebake on save**
defaults to on, but its control depends on reimport being enabled. **Test Connection**
queries the Unity CLI for a running editor instance.

The integration finds `unity` on PATH, with a Windows fallback under
`%LOCALAPPDATA%/Unity/bin/unity.exe`. Reimport requires the saved map to belong to the
configured project and have an `Assets/` or `Packages/` relative path. It launches
`UnityEditor.AssetDatabase.ImportAsset` with `ForceUpdate`. After a successful reimport,
a live map frame and the rebake preference allow `rebake_subscene --map <map-stem>`.
These operations run asynchronously through `QProcess` and report results to the editor.

**Main files:** `common/src/unity/UnityIntegration.*`, `UnityPreferencePane.*`,
`common/src/ui/PreferenceDialog.cpp`, `MapFrame.cpp`, `common/CMakeLists.txt`, and
`common/src/Preferences.cpp`.

Unity integration and its preferences pane live in `common/src/unity/` under the
`tb::unity` namespace. `UnityIntegration.*` moved from `common/src/ui/`; the Unity
controls, entity ID and class-index controls moved out of `MiscPreferencePane.*`.
The Misc pane retains automatic entity defaults. All five moved preferences keep
their original `Misc/` storage keys and defaults so existing settings are preserved.
Restore Defaults affects only the settings on the selected pane. The map frame keeps
the small reimport calls in its save and save-as paths.

**Rebase considerations:** Preserve save-hook placement, path conversion, frame
lifetime guards, process completion handling and the success-only rebake sequence.
Keep user saves distinct from ordinary autosave operations. This process integration
has no dedicated retained tests; the retained Unity tests concern entity identity.

## 10. Live command-line control

**Status:** Work in progress, included in this fork.

**Purpose:** Allow a local client to read state and edit an open Quake map while the
editor remains interactive.

**Use:** Start the editor with `--command-line-control-socket <name>` and connect using
the `TrenchBroomCommandLineClient` or another compatible local client. Requests and
responses use newline-delimited JSON with matching IDs and a 1 MiB request limit.
Commands cover map state, new/load/save/save-as, brush creation, camera movement,
geometry framing and PNG viewport capture. Replacement of modified maps and file
overwrites require explicit request flags. The service currently targets Quake 1 maps
in Standard or Valve format.

**Main files:** `app/src/CommandLineClient.cpp`,
`common/src/ui/CommandLineControlServer.*`, `CommandLineControlService.*`,
`common/src/TrenchBroomApp.*`, `mdl/Map.*`, `mdl/Autosaver.cpp`, `ui/MapFrame.cpp`,
`ui/MapView3D.*`, and the app/common CMake files.

**Protocol:** See [CommandLineControlProtocol.md](CommandLineControlProtocol.md) for
requests, responses, command details and examples.

**Rebase considerations:** Carry this implementation forward with the other fork
commits. Reconnect the service to upstream's application/window ownership and map
operations. Preserve GUI-thread dispatch, unsaved-change/overwrite guards, undo,
viewport access and safe shutdown of connected local clients. The socket-buffer
lifetime-order fix found during the earlier testing pass is retained.

## 11. Room creator prototype

**Status:** Unfinished prototype. No dedicated tests are retained, as requested.

**Purpose:** Generate a simple room shell from box bounds or floor brushes. Settings
include wall thickness/height, optional floor and ceiling, and separate materials for
walls, ceiling and floor. Regeneration can replace previously generated geometry in
one transaction. The UI persists settings under the `Room Creator/` preferences.

**Main files:** `common/src/mdl/RoomGenerator.*`,
`common/src/ui/RoomCreatorDialog.*`, `MapFrame.*`, `Actions.cpp`, and
`common/src/Preferences.cpp`. Icon: `app/resources/graphics/images/RoomCreator.svg`.

**Rebase considerations:** Decide whether to carry the prototype forward. If retained,
reconnect its menu/action and model operations, but do not treat it as a finished feature
or an acceptance requirement for the rebase.

## 12. Direct startup and map defaults

**Use:** Startup opens an empty, untitled map. **New Map** uses the default game and
format from Unity preferences without prompting. The defaults are Quake and Standard;
Quake defaults always use Standard because Scopa imports paraxial UVs. Explicit map
headers retain their format, including Valve. Missing game/format headers use the
defaults. No Quake installation is required for bundled definitions or folder textures.

Opening a file reuses an untouched startup map. **File > New Map (Choose Game)...**
retains the game chooser, and **Help > Welcome Window** retains the welcome screen.
Closing the last map window quits the application, including when Welcome is open.

**Main files:** `common/src/unity/UnityWorkflow.*`, `UnityPreferencePane.*`,
`common/src/TrenchBroomApp.*`, `common/src/ui/Actions.cpp`, and `FrameManager.cpp`.

**Rebase considerations:** Preserve the shared open-file path for recent files and
command-line files, the untouched-startup-map check, unsaved-change confirmations,
explicit format headers, and the empty-map configuration copy (never mutate the
shared game config).

## 13. Unity texture folders

**Use:** Unity preferences offers **Unity folders** (default) and **WAD** texture
sources for Quake. `<Unity project>/Assets/Content/Textures` is always used and is
shown read-only. **Additional folders** (`Unity/Additional texture folders`) are listed
one per line after it; relative folders resolve against the Unity project path. Only
PNG, TGA, BMP, JPEG and TIFF images directly inside each folder are used. Subfolders
are not searched; list them on their own lines (for example
`Assets/Content/Textures/PrototypingQuake`). **Add Folder...** starts in
`Assets/Content/Textures` and stores folders inside the project as relative paths.
Each configured folder is a material collection, with flat texture names taken from
the filename without its extension. Image dimensions are retained.

Folder mode ignores the worldspawn `wad` property without editing it or face names.
WAD mode retains the existing loader. Case-insensitive duplicate names are errors
listing every conflicting file; all candidates for an ambiguous name are omitted.
Other images still load. Names over 15 characters and `*`, `+0` through `+9`, or
`sky` prefixes are reported as one summary warning (individual paths at debug level),
without rejecting those images. Case is not checked, since Scopa and Quake tools match
names case-insensitively. Missing folders, and relative folders without a Unity project
path, are reported in the editor log. Adding, removing or editing images reloads
materials automatically after a short debounce. Other changes in the folders, such as
Unity writing `.meta` files, do not reload. **Refresh Textures** in Unity
preferences and the existing collection reload control provide manual refresh.

**Main files:** `common/src/unity/UnityTextureFolders.*`, `TextureFolderWatcher.*`,
`UnityPreferencePane.*`, `common/src/mdl/GameFileSystem.*`, `GameImpl.cpp`, `Map.cpp`,
`common/src/io/LoadMaterialCollections.cpp`, `common/src/ui/MapDocument.*`, and
`MaterialWadFilter.cpp`.

**Rebase considerations:** Keep the flat virtual filesystem, folder metadata used for
collection names, palette-free image loading, and captured extension-list ownership
in asynchronous texture loaders. Reconnect watcher lifetimes to document lifetimes.
Reloading resources must not modify maps or their undo history. Keep WAD export
separate; it is not implemented by this feature.

**Tests:** `common/test/src/unity/tst_UnityWorkflow.cpp` covers Standard startup,
explicit format preservation, empty map creation, non-recursive flat discovery,
project-relative folders, duplicate and missing-folder errors, WAD-name warnings, collection grouping, image dimensions,
map save round trips, automatic reload without map modifications, and no reload for
non-image files. Upstream test
fixtures explicitly use WAD mode; Unity tests opt into folder mode.

## 14. Unity live link and camera sync

**Purpose:** Keep TrenchBroom's 3D camera and the Unity scene view camera in sync, so the
same spot can be seen with Unity's real lighting.

**Use:** TrenchBroom listens on a local socket (`Unity/Live link socket name`, default
`trenchbroom-unity`, set under **Live Link** in Unity preferences, which also shows the
connection status). The Unity editor connects from `com.junk.trenchbroom`. The **Unity**
menu has **Send Camera to Unity**, **Pull Camera from Unity** and the checkable **Live
Camera**. With Live Camera on, the application that has focus sends its camera about 30
times a second and the other follows. Live Camera is shared by both sides and not saved.
No default shortcuts; assign them in Keyboard preferences. Protocol in
`UnityLiveLinkProtocol.md`.

**Main files:** `common/src/unity/UnityLiveLink.*`, `UnityLiveLinkProtocol.*`,
`UnityPreferencePane.*`; hooks in `common/src/TrenchBroomApp.*` (owns the link),
`common/src/ui/Actions.*` (Unity menu) and `common/src/ui/MapFrame.cpp` (refreshes menu
state when the link status changes).

**Rebase considerations:** The link reads and moves the camera of the top frame's
`MapView3D`. Keep `Camera::moveTo`/`setDirection` semantics (unit direction and up) and
the `topFrame()` lookup. It is separate from the command-line control server (feature 10),
which is only started by its command-line option.

**Tests:** `common/test/src/unity/tst_UnityLiveLinkProtocol.cpp` covers parsing,
validation, normalization and serialization of every message type. The socket and
camera behaviour are checked manually against a running Unity editor.

## 15. Unity prefab catalog

**Purpose:** Place Unity prefabs from TrenchBroom without hand-maintained entity
definitions, and give every placed prefab a stable identity the Unity importer can find.

**Use:** Unity (`com.junk.trenchbroom`) exports `<Unity project>/TrenchBroom/`:
`entities.fgd` (the core FGD plus one point class per prefab, with a preview model and a
read-only `_unity_prefab` GUID property), `models/<guid>.obj`, `thumbnails/<guid>.png`
and `catalog.json`, written last. With **Entity definitions from Unity** on (Unity
preferences, `Unity/Entity definitions from Unity`, default on, needs a Unity project
path), TrenchBroom loads that FGD instead of the map's `_tb_def`, which is left
untouched, and mounts the export folder so the models resolve. The **Unity** inspector
tab lists the catalog's prefabs with thumbnails, a category filter and search; dragging
one places it like the Entity browser does. Creating a point entity always sets
`_unity_prefab` from its definition, even with automatic default properties off, and
never copies a Unity prefab's other defaults (they are the prefab's component values,
which placed prefabs follow until changed; `unity::isUnityPrefab`). A new
`catalog.json` reloads the entity definitions and the tab. **Unity > Update Prefab
Entities from Catalog** gives catalog GUIDs to entities placed by class name only and
renames entities whose prefab was renamed; it is one undoable command.

**Main files:** `common/src/unity/UnityCatalog.*`, `UnityCatalogWatcher.*`,
`UnityPrefabBrowser.*`, `UnityPreferencePane.*`; hooks in `common/src/mdl/Map.cpp`
(entity definition path, reload on preference change), `Map_Entities.cpp` (GUID on
creation), `GameFileSystem.cpp` (export folder mount), `common/src/ui/MapDocument.*`
(owns the watcher), `Inspector.cpp` (Unity tab) and `Actions.cpp` (update command).

**Rebase considerations:** Keep the entity definition path override in
`Map::loadEntityDefinitions`, the mount after the game file systems so the export's
`models/` wins, and the GUID hook next to `ensureRotationProperty` in point entity
creation. The Unity tab is the last inspector page so existing page indices still hold.

**Tests:** `common/test/src/unity/tst_UnityCatalog.cpp` covers catalog parsing and
rejection, the entity definition preference, GUIDs from definitions, and the update
command including undo, and that placing a Unity prefab copies no defaults while other
entities still get theirs.

## 16. Decal stamps

**Purpose:** Place surface detail (markings, stripes, signage, grime) from Unity decal
atlases without unique textures, previewed on brushes and imported as URP decal
projectors. Modelled on Source's `info_overlay`; the design is in `DecalStampPlan.md`.

**Use:** In Unity, a `DecalLibrary` asset (Create > Junk > Decal Library) names an atlas
id, a URP decal material, the atlas texture and the decals in it (pixel rectangles with a
bottom-left origin, a category and a default size in meters). The catalog export writes
each atlas to `TrenchBroom/decals/<id>.png` and lists `decal_atlases` and `decals` in
`catalog.json`; both sections are optional, so older catalogs still load. A library or
atlas change re-exports only the decals. The **Decals** inspector tab lists them with
tiles cut from the atlases. Clicking one arms the **decal stamp tool**: the outline
follows the brush face under the cursor (up follows world up on walls and the view on
floors and ceilings), Alt+wheel turns it in 15 degree steps, Alt+Shift+wheel scales it,
a click places a `decal` entity and the tool stays armed until Escape. With the tool off,
Alt+wheel and Alt+Shift+wheel turn and scale the selected decals as one undoable command
each. The `decal` entity stores `_decal` (`<atlas>/<name>`), `size`, `depth`, `angles`,
`opacity`, `angle_fade`, `flip` and `_weights` (stored only). Its bounds cover its
projection box, so it is picked and selected there. The map renderer projects it onto
the visible brushes in its box and draws it with the atlas, faces turned more than
`angle_fade`'s end away getting none. Unity's `DecalImporter` (`Junk.Scopa.Editor`)
adds a `DecalProjector` with the atlas material, UV scale and bias from the rectangle,
and a rotation computed from `angles` with TrenchBroom's convention, because Scopa's
own angle conversion turns roll the other way. Projectors only paint the Statics
rendering layer, which brush geometry has.

**Main files:** `common/src/unity/UnityDecals.*` (box, projection, angles, property
values), `UnityDecalLibrary.*` (atlas materials, owned by `mdl::Map`), `UnityDecalBrowser.*`,
`UnityCatalog.*` (decal sections), `common/src/ui/DecalStampTool.*`; hooks in
`render/EntityDecalRenderer.*` (atlas path beside upstream `infodecal`), `render/EntityRenderer.cpp`
(wireframe box), `mdl/EntityNode.cpp` (bounds), `mdl/Map.*` (library loading, `decalLibraryDidChangeNotifier`),
`render/MapRenderer.cpp`, `unity/UnityCatalogWatcher.cpp`, `ui/MapViewToolBox.*`,
`ui/MapView3D.cpp`, `ui/SwitchableMapViewContainer.*`, `ui/Inspector.*` and
`ui/MapFrame.cpp`. Unity side: `com.junk.scopa/Junk.Scopa.Editor/Decals/` and
`com.junk.trenchbroom/.../Catalog/DecalCatalogExporter.cs`.

**Rebase considerations:** Keep upstream `infodecal` geometry untouched; the atlas
path branches on the `decal` class before it. `MapRenderer::render` draws entity decals
after the opaque passes (upstream draws them first), so soft-edged atlas decals blend
over the faces they sit on; this also applies to `infodecal`. `EntityRenderer` draws
decal entities as wireframe boxes only, since a solid box would hide the decal. Decal
polygons are reversed from brush face vertex order to be front facing. Atlas materials are reloaded with the
catalog and must not change the map or its undo history. The decal tab is the last
inspector page so existing page indices still hold. Alt+wheel is otherwise unused in
the 3D view; Ctrl+wheel (drill selection) and Ctrl+Alt+wheel (grid) are taken.

**Tests:** `common/test/src/unity/tst_UnityDecals.cpp` covers the box from entity
properties, bounds, projection onto one face, clipping at a face edge, wrapping a
corner, the fade angle, depth, flips, atlas coordinates and angle round trips;
`tst_UnityCatalog.cpp` covers the decal sections; `common/test/src/ui/tst_DecalStampTool.cpp`
covers placement on walls and floors, turning and scaling selected decals with undo,
the remembered stamp offset and its reset, and cycling through stacked decals.

### Decal editing, appearance and folder categories

Whenever the selection is only decals (or the decal tool is active), the 3D view treats
them as decals, not boxes: `DecalEditController` sits before the move tool, so dragging a
selected decal slides it across brush faces and around corners; holding W, E or R while
dragging moves, scales (Shift stretches width and height separately) or turns them (Shift
snaps to 45 degrees), and Shift keeps a move to one screen axis. `MapView3D` accepts the
ShortcutOverride for W, E and R then, so they don't fly the camera or toggle the Rotate
and Edge tools. Selected decals are outlined on the faces they cover
(`EntityDecalRenderer::renderOutlines`) instead of an entity box, and the brush-style
selection guide is skipped; a decal that covers nothing shows its projection box. 2D
views draw every atlas decal's projection box.

**Placing decals** is a mode: the Place Decals button on the toolbar (after the Room
Creator button; also Edit > Tools > Place Decals, no default shortcut) turns it on and
shows the Decals tab, and clicking a decal in the Decals tab turns it on with that decal.
While it is on, a click only ever places the chosen decal, also over existing decals, so
it never selects one by accident; turn it off (button or Escape) to select decals again.
While placing, holding E or R and dragging (or Alt+Shift+wheel and Alt+wheel) scales or
turns the stamp, not the selection: Shift stretches width and height separately or snaps
to 45 degrees. That scale and turn are a remembered offset applied to every decal placed
from then on, whichever decal is chosen, and shown in the tool bar with a Reset
Transform button that sets them back to the default size and no turn. The decal just
placed is selected, so W moves it straight away; a move doesn't change the offset. Placing a decal
while decals (the `decal` entity class, or all point entities) are hidden in the view
options shows them again: hidden entities are only drawn while selected, so each new
decal would otherwise seem to move the previous one.

With placing off, clicking where several decals overlap cycles through them: the first
click selects the nearest, and each further click on the selected decal selects the next
one in the stack (`decalsUnderCursor`, `nextDecalInStack`), so the one underneath can be
reached. Only decals whose boxes the ray enters before the first brush face count, so
decals behind a wall don't join the stack; a lone decal is selected as usual.

The Decals tab lists categories, with their decal counts and an "All" entry, beside the
decal tiles in a split view. Dropping images on a category imports them into it; its
context menu imports into it or shows its folder.

`tint` (0-255 per channel), `brightness` and `invert` change a decal's colors: inverted,
then multiplied, alpha untouched. TrenchBroom previews them with recolored copies of the
atlas made on first use (`UnityDecalLibrary::atlasMaterial(atlas, appearance)`, which
keeps a CPU copy of each atlas because uploaded textures drop their pixels); Unity's
`DecalVariants` bakes the same into a texture and material stored in the imported map,
so any decal shader works.

Decal categories come from folders: every image in `Assets/Content/Decals/<category>/`
is a decal. `DecalFolderLibraries` (Unity) rebuilds the category's packed atlas, URP decal
material and DecalLibrary in its `_Generated` folder when images there change, and the
catalog lists each decal's `source` image. Hand-made DecalLibrary assets still work.

## 17. Image viewer and Unity asset import

**Use:** Double-click a material or decal (or use View Texture / View Image in their
context menus) to open the built-in image viewer: wheel zoom around the cursor, drag to
pan, double-click to toggle fit and 1:1, a checkerboard behind transparency, RGBA, RGB,
alpha and single-channel views, Left/Right for the other images in the folder, and Open in
Editor (the external image editor in the Unity preferences, or the system default),
Show in Folder and Copy Path. The image reloads when it changes on disk.

Drop image files on the Materials browser, or use Unity > Import Textures into Unity, to
copy them into a texture folder (a new folder is added to the texture folders); the
texture folder watcher reloads the materials. Drop them on the Decals browser, its
Import... button, or Unity > Import Decals into Unity, to copy them into a decal category
under the decal folder (Unity preferences, default `Assets/Content/Decals`); Unity builds
the category atlas when it next refreshes. Existing files are only replaced after asking.
Remove from Unity Project... in the material and decal context menus moves the image and
its `.meta` file to the recycle bin after confirming.

**Main files:** `common/src/ui/ImageViewerDialog.*`, `common/src/unity/UnityAssetImport.*`
(plans, copies, categories, removal), `UnityAssetImportDialogs.*` (folder pickers and
prompts), hooks in `ui/CellView.*` (double-click), `ui/MaterialBrowserView.*`,
`ui/MaterialBrowser.*` (drops), `unity/UnityDecalBrowser.*`, `ui/Actions.cpp` (Unity
menu), `Preferences.*` and `unity/UnityPreferencePane.*` (decal folder, image editor).

**Tests:** `common/test/src/unity/tst_UnityAssetImport.cpp` covers import plans,
conflicts, replace and skip, folder names, categories and project paths.

## 18. Label decluttering

**Use:** When zoomed out, entity classname, group name and layer labels no longer pile on
top of each other: a label that would overlap another is left out, so crowded areas show
one readable label instead of a smear. Labels drawn on top (2D views and labels shown
through geometry) win over the others, and nearer labels over farther ones. Zoom in and
the hidden labels come back as space opens up.

**How:** `TextRenderer::setDeclutter` (through `RenderService::setDeclutterStrings`)
sorts the frame's labels and keeps them greedily with `declutterLabels`, which bins
placed label rectangles in a 64 pixel screen grid so each label is only tested against
its neighbours. `EntityRenderer`, `GroupRenderer` and `LayerHighlightRenderer` turn it
on; other text (measurements, tool readouts) is unchanged.

**Tests:** `common/test/src/render/tst_TextDeclutter.cpp` ([text-declutter]).

## 19. Scene camera

**Purpose:** Keep where the level designer was looking in the map itself, so TrenchBroom
reopens a map at the same view and Unity knows the editor camera without the live link,
as the base for "Play From Here".

**Use:** Every saved map holds exactly one `scene_camera` point entity in the default
layer: its `origin` and `angles` (pitch yaw roll, positive pitch down, as for decals) are
the 3D camera's pose. Saving (Save, Save As and the command-line control `saveMap` /
`saveMapAs`) moves it to the 3D camera, adding it if the map has none and removing all
but the first if pasting brought in more; it is one undoable "Update Scene Camera" step
and is skipped when the camera hasn't moved. Opening a map moves the 3D camera to it. It
needs no entity definition (no missing definition issue) and is neither drawn nor
picked unless selected, since the 3D camera already shows where it is.

**Main files:** `common/src/unity/UnitySceneCamera.*`; hooks in
`common/src/ui/MapFrame.cpp` (`storeSceneCamera` before saving, `restoreSceneCamera` on
load), `CommandLineControlService.cpp` (saves), `mdl/EditorContext.cpp` (hidden unless
selected) and `mdl/MissingDefinitionValidator.cpp`.

**Tests:** `common/test/src/unity/tst_UnitySceneCamera.cpp` ([unity-scene-camera]):
pose round trips (including straight up and down), adding, moving, deduplicating, undo
and visibility.

## 20. Play From Here

**Purpose:** Go from editing to playing the map in Unity in one key, starting where you
are looking or where you click, and come back to TrenchBroom where you stopped.

**Use:** **Unity > Play in Unity** (Alt+P, and the play button on the tool bar after
Place Decals) saves the map if it has changes and asks Unity, over the live link, to
play with the player's eyes at the 3D camera. While Unity plays, the same action (shown
as a stop button) stops it. Right-clicking a brush face in the 3D view offers **Play From
Here**, which starts the player standing on that spot, facing the camera's way along the
floor; on a wall the player starts out from it and under a ceiling below it, and drops to
the floor. When play stops, TrenchBroom's camera moves to where the player's eyes were.
Both need Unity connected to the live link. The Unity side assumes the open map is the
one the game loads.

**How:** The live link protocol (version 2) adds `play`, `stop` and `play_state`
(`UnityLiveLinkProtocol.md`). `unity::playStartAtCamera` and `playStartOnSurface`
(`UnityPlay.*`) make the start; `MapFrame::playInUnity` saves without the CLI reimport,
since Unity refreshes its assets itself, and `AllowSetForegroundWindow` lets Unity come
to the front. In cyberjunk, `TrenchBroomPlay` (in `com.junk.trenchbroom`, editor only)
enters play mode, starts a game past the menu as Start Game does, waits for the level to
finish streaming in, and moves the player by setting its components (position, yaw,
first person camera pitch, velocity, interpolation); no game code changes. Its
**Junk > Trenchbroom > Play From Scene Camera** menu plays from the map's `scene_camera`
without TrenchBroom. The Unity link no longer starts in asset import workers or batch
mode, which could take the connection from the editor.

**Main files:** `common/src/unity/UnityPlay.*`, `UnityLiveLinkProtocol.*`,
`UnityLiveLink.*`; hooks in `common/src/ui/MapFrame.*` (play, save without reimport),
`Actions.cpp` (Play in Unity), `MapViewBase.cpp` (context menu) and the
`PlayInUnity.svg` / `PlayInUnity_on.svg` icons.

**Tests:** `common/test/src/unity/tst_UnityPlay.cpp` ([unity-play]) covers starts on
floors, walls, ceilings and when looking straight up or down;
`tst_UnityLiveLinkProtocol.cpp` covers the new messages. The Unity side was checked
against the running cyberjunk editor with a stand-in for TrenchBroom: entering play,
skipping the menu, the player's position, facing and pitch, stopping, and the camera sent
back.

## Build and maintenance changes

The fork also changes Windows build guidance, ignore rules, editor icons and toolbar
registration, and removes some inherited CI/build-helper files. These are maintenance
changes rather than user-facing features. Compare them against upstream's current build
and dependency setup before replaying them. A local branch name such as `feature/decal`
or `feature/2d` alone is not evidence of another implemented feature in the active tip.

## Running the fork regression tests

With `build-vs18` configured as described in `AGENTS.md`:

```powershell
# Build common-test and run Unity identity, workflow, live link, catalog and decal tests.
.\test-fork.ps1

# Use the existing binary, or run the full original common suite as well.
.\test-fork.ps1 -SkipBuild
.\test-fork.ps1 -All
```

The runner selects `[entity-identity],[unity-workflow],[unity-live-link],[unity-catalog],[unity-decals],[decal-stamp],[unity-asset-import],[text-declutter],[unity-scene-camera],[unity-play]` by default, uses the configured Qt kit and sets
the executable directory as the working directory. `-All` runs the remaining full common
suite; the removed feature tests are not part of either run.

Before and after a rebase, run the retained identity tests and manually exercise the
other features that you choose to preserve. In particular, inspect prefabs, overlays,
selection/painting interactions, real Unity integration and viewport capture.
