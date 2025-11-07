# Prefab Library Feature Plan

## Goals
- Provide an in-editor prefab browser (between Map and Entity tabs) that lets users drag canned geometry into the map just like entities or materials.
- Allow saving the current selection as a prefab (`<map-dir>/prefabs/<name>.map`) with optional thumbnail generation and metadata refresh.
- Keep prefabs as ordinary map geometry; once dropped, they behave exactly like pasted brushes/entities.

## High-Level Architecture
- **Inspector integration:** Update `common/src/ui/Inspector.cpp` to insert a `Prefab` tab between the Map and Entity pages. Add a `PrefabInspector` (mirrors `EntityInspector`) that hosts creation tools and the browser inside a splitter so layout/shortcuts stay familiar.
- **Prefab browser widget:** Model after `common/src/ui/EntityBrowser.*`:
  - `PrefabBrowser` (Qt widget) → builds the scrollbar + `PrefabBrowserView` + control strip (sort/filter/group buttons, refresh, open-folder, disabled banner).
  - `PrefabBrowserView : CellView` handles tile rendering, thumbnails, tooltips, keyboard focus, and drag payload creation.
  - Control row defaults: sort by Name/Modified, filter text, optional grouping by folders/tags, toggle for “Show Missing Thumbnails”.
- **Drag-and-drop:** Views already forward drag/drop via `MapViewBase::dragEnter/dragMove/drop` into the tool chain. Register a `PrefabDropController` next to `CreateEntityToolController` in 2D/3D views. Accept payloads like `prefab:/abs/or/rel/path.map`, preview geometry during drag, on drop paste nodes using the existing copy/paste helpers so the result is normal geometry.
- **PrefabManager service:** Lives with the document/UI layer, owns:
  - Prefab root: `<map-dir>/prefabs` (disabled while `Map::persistent()` is false).
  - Optional `<prefabs>/thumbnails` subfolder for PNG snapshots.
  - In-memory list of prefab descriptors (path, display name, bounds, modified time, thumbnail state, metadata).
  - Connections to `MapDocument` notifiers (`mapWasCreated`, `mapWasLoaded`, `mapWasSaved`, `nodesDidChange` if we want auto-create) plus a manual `Refresh` action.
  - Background scanning via `kdl::task_manager` and optional `QFileSystemWatcher` so UI updates when files change externally.
- **MapPreview reuse:** The python project under `MapPreview/` can batch-render thumbnails if a native renderer isn’t ready. Integrate via a helper that shells out (if available), otherwise fall back to text placeholders and log warnings.

## Prefab Lifecycle
1. **Creation**
   - Requires saved map (prefab folder derived from `Map::path()`).
   - Command button collects the current selection; call `mdl::serializeSelectedNodes` (see `common/src/mdl/Map_CopyPaste.cpp:248-265`) and write to `<prefabs>/<name>.map`.
   - Store metadata (JSON/yaml/ini) if tags/author info are needed later.
   - Kick thumbnail generation and refresh the manager list. Wrap writes in an undoable command or graceful error logging so partial files don’t linger.
2. **Scanning**
   - Manager enumerates `.map` files in the prefab folder, reads minimal metadata (bbox, node count) via `io::NodeReader` or a lightweight parser.
   - Cache results in memory; offer a `Refresh` button plus automatic rescan on filesystem events or when the Prefab tab becomes visible.
3. **Placement**
   - Drag payload → prefab drop controller loads nodes once, positions preview based on cursor hit (use center or stored origin), and commits via `mdl::paste` semantics so transactions, undo, and selection handling remain consistent (`common/src/mdl/Map_CopyPaste.cpp:267-302`).
   - Dropped prefabs become real map nodes with no special flags.
4. **Management**
   - Context menu per prefab: open file, reveal in explorer, regenerate thumbnail, rename/delete (with filesystem confirmation).
   - Disabled state messaging when prefab folder is missing or map hasn’t been saved yet.

## Thumbnail Strategy
- **Primary:** Off-screen capture using `render::MapRenderer` with a lightweight scene containing only the prefab nodes; save PNG to `thumbnails/<name>.png`. Trigger after creation and when a thumbnail is missing/stale.
- **Fallback:** Invoke `python MapPreview/quake_map_renderer.py <prefabs>` when Python + dependencies are available. Link to `MapPreview/README.md` for setup. Consider batching to avoid repeated startups.
- **Last resort:** Render text placeholders (initials or bounding box sketch) inside `PrefabBrowserView` tiles; mark entries with a “Generate” button so users know thumbnails are missing.

## UI/UX Details
- Prefab tab header and browser should mirror existing terminology (reuse `SwitchableTitledPanel` if we expose settings such as folder overrides).
- Control strip additions:
  - `Refresh` button calling `PrefabManager::rescan`.
  - `Create From Selection` button (enabled only when selection has nodes and prefab folder exists).
  - `Open Folder` button opening the prefab directory in the OS file browser.
- Tiles show thumbnail + name + metadata (e.g., brush/entity counts). Tooltips can list path and last modified time.
- Accessibility: keep keyboard navigation consistent with `CellView` (arrow keys, enter to drop, space to open context menu).

## Testing & Documentation
- **Unit tests:** Add cases under `common/test/src` that mock prefab directories (use temp dirs) to validate scanning, creation, deletion, and metadata parsing. Include failure cases (invalid map file, missing folder).
- **Integration tests:** If feasible, script a prefab creation + drag workflow using existing tool/controller harnesses to ensure the drag payload spawns geometry.
- **Manual docs:** Extend the manual section near the entity browser screenshot (`app/resources/documentation/manual/index.md:504`) with a “Prefab Browser” chapter covering folder layout, creation, drag-drop, refresh, and thumbnail regeneration steps.
- **Future enhancements:** track follow-up tasks such as prefab tagging/filtering, shipping starter prefab packs in `app/resources/prefabs`, and supporting metadata-driven placement rules (e.g., snap offsets).

Use this plan as the authoritative guide when implementing the prefab library feature. Update it if requirements shift so design and implementation stay aligned.
