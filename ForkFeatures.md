# TrenchBroom Unity fork: feature reference

This document inventories the custom features in this checkout for future maintenance
and rebasing. It describes the current implementation, rather than promising that each
feature is finished. Source paths are relative to the repository root.

The only dedicated fork regression tests retained are for Unity entity identity:
`_tb_unique_id` and `classindex`. TrenchBroom's original upstream test suites remain.
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

## Build and maintenance changes

The fork also changes Windows build guidance, ignore rules, editor icons and toolbar
registration, and removes some inherited CI/build-helper files. These are maintenance
changes rather than user-facing features. Compare them against upstream's current build
and dependency setup before replaying them. A local branch name such as `feature/decal`
or `feature/2d` alone is not evidence of another implemented feature in the active tip.

## Running the retained identity tests

With `build-vs18` configured as described in `AGENTS.md`:

```powershell
# Build common-test and run the Unity entity ID/class-index tests.
.\test-fork.ps1

# Use the existing binary, or run the full original common suite as well.
.\test-fork.ps1 -SkipBuild
.\test-fork.ps1 -All
```

The runner selects `[entity-identity]` by default, uses the configured Qt kit and sets
the executable directory as the working directory. `-All` runs the remaining full common
suite; the removed feature tests are not part of either run.

Before and after a rebase, run the retained identity tests and manually exercise the
other features that you choose to preserve. In particular, inspect prefabs, overlays,
selection/painting interactions, real Unity integration and viewport capture.
