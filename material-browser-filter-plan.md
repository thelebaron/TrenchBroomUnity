## Material Browser Enhancements Plan

### Objective
- Move the search input above the texture grid so it appears directly under the *Material Browser* label.
- Introduce a wad filter button alongside the existing sort, group, and usage controls that opens a dropdown to toggle visibility of enabled wad textures.
- Keep the code changes concentrated in new helper/UI components where possible and persist the wad visibility preference safely even when wads are missing.

### Steps
1. **Inspect current layout** – The browser currently has controls (sort order combo, “Group”, “Used”, search field) below the texture grid (`MaterialBrowser::createGui`). We need to push the search field above the grid without disrupting the existing layout for the other buttons.
2. **Material filtering** – Build a new class (e.g., `MaterialWadFilter`) responsible for reading wad lists (using `mdl::EntityPropertyKeys::Wad`/`EnabledMaterialCollections` via `Map` data), tracking per-wad visibility toggles, persisting them through a new preference, and exposing a signal when the hidden wad set changes.
3. **MaterialBrowserView updates** – Extend filtering logic to skip materials whose `collectionName()` corresponds to hidden wad names/paths, honoring missing wads when the preference refers to a wad that no longer exists.
4. **UI integration** – Insert the search widget above the grid and restructure the lower control row to include the new filter button that pops up the `MaterialWadFilter` menu; have the menu reflect the wad list and update the browser via the view when selections change.
5. **Preferences** – Add a preference for storing the hidden wad identifiers (likely a `std::vector<std::filesystem::path>` or `std::vector<std::string>`), defaulting to empty, with serialization via the preference manager.
6. **Persistence & safety** – When loading, ensure we ignore unknown/missing wads so no errors occur (e.g., check against current material collections). Emit updates when wad collections change, reloading filter menus accordingly.

### Next Actions
- Await the go-ahead to modify files.
- If we proceed, create `MaterialWadFilter` (widget/model), update `MaterialBrowser`/`MaterialBrowserView`, and add supporting preference entries.
