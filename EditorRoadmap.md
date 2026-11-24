# Editor Roadmap: Ideas for a Game-Specific Level Editor

The aim is to make this fork a level editor built for cyberjunk, not a general Quake map
editor with Unity export added. These ideas come from editors studios use in production:
Hammer 1 and 2, Unreal, CryEngine Sandbox, idStudio, Radiant and others. Each is matched
to what the fork already has: the Unity live link, the prefab and decal catalogs, the
atlas pipeline, the room creator, layers and DOTS SubScenes.

Every idea lists what it is, where it comes from, how it fits what we already have, and
a rough size (S, M, L, XL).

---

## 1. A faster loop from editing to playing

### 1.1 Play from here (L) — done

Built as ForkFeatures section 20: Alt+P plays from the 3D camera, right-clicking a face
plays from that spot, and stopping returns the camera to the player.

**What:** One key saves the map, sends it to Unity over the live link, enters Play mode
and spawns the player at the TrenchBroom camera's position and facing. Pressing Esc in
Unity (or a key in TrenchBroom) stops play and puts the TrenchBroom camera where the
player was.

**Precedent:** Unreal's viewport "Play From Here", CryEngine Sandbox's Ctrl+G game mode.

**Fit:** The live link already syncs the camera and runs commands. What's missing is a
"play at pose" command, a spawn override component in Unity, and sending the player's
pose back when play stops.

**Why first:** It's the biggest daily time saver of anything on this list.

### 1.2 Settle props with a physics simulation (M)
**What:** Select debris props and "Simulate in Unity": Unity runs physics for a few
seconds and sends the settled transforms back as one undoable entity edit.

**Precedent:** Unreal's Simulate mode with "Keep Simulation Changes".

**Fit:** Uses the live link both ways and stable entity IDs to match props. It suits a
junk-heavy look where hand-placing every can is tedious.

### 1.3 Playtest heatmaps (M)
**What:** Unity records deaths, damage taken, player paths and time spent in each area
during play. TrenchBroom loads the log and draws an overlay: a heat volume, paths as
lines, death markers that link to a replay timestamp.

**Precedent:** In-house telemetry tools at larger studios; uncommon in mapping tools.

**Fit:** A small Unity recorder system, a file format, and an overlay renderer like the
layer and decal overlays.

---

## 2. Texturing without unique textures

### 2.1 Hotspot / trim-sheet texturing (L)
**What:** Mark rectangles on a trim sheet ("hotspots"). Select faces and press
"Hotspot": each face gets the rectangle whose shape and size fit it best, UVs fitted and
rotated to match. Door frames, pipes, panel edges, stair nosings and trims come out
textured with no manual UV work.

**Precedent:** Source 2 Hammer hotspot materials; Insomniac's trim-sheet workflow.

**Fit:** Nearly the same pipeline as decals. Unity defines the rects (a HotspotSheet
asset, or rects drawn in an editor window), the catalog exports them, and TrenchBroom
reads them like decal atlases. Face UV fitting builds on the existing UV tools. Pairs
naturally with decals: trims for structure, decals for story.

### 2.2 Materials by function during blockout (S)
**What:** A dev-texture set where colour means function: floor, wall, cover height,
climbable, no-go, glass. A rule-based "art pass swap" replaces each function texture
with a material from a chosen kit.

**Precedent:** Common blockout practice: Valve's dev textures, Unreal's grid materials.

**Fit:** Reuses the replace-material operations; the rules live in a small per-kit table.

---

## 3. Splines (L)

**What:** One path tool for curves with handles, plus per-path type and settings.
Unity builds the meshes with the Splines package:
- cables and wires, with sag
- pipes and conduits
- railings and fences
- train and drone routes
- AI patrol routes
- camera rails

**Precedent:** Source 2 Hammer's path tool (used for ropes and cables), Unreal spline
meshes, Unity Splines.

**Fit:** A path entity storing control points and tangents, a TrenchBroom tool to edit
them (like the vertex tool, with tangent handles), a preview tube or line in the view,
and a Unity importer that turns the path into the matching mesh or component. Overhead
cable clutter alone makes this worth building.

---

## 4. Scatter painting (L)

**What:** A paint tool that scatters prefabs from the catalog onto surfaces: brush
radius, density, random scale and rotation, align to surface or to world up, an eraser
and a filter by prefab. A whole stroke is stored as one scatter entity holding packed
points, not hundreds of entities.

**Precedent:** Unreal's foliage mode, CryEngine vegetation painting, aimed at debris
instead of grass.

**Fit:** Uses the prefab catalog and its model previews. In Unity, one scatter entity
maps directly to instanced ECS entities, fast to bake and to render. TrenchBroom draws
the points as instanced previews.

---

## 5. Gameplay logic you can see

### 5.1 Hammer-style I/O connections (L)
**What:** Entities have outputs (`OnTrigger`, `OnOpen`, `OnDeath`, …) wired to inputs
on other entities (`Open`, `Enable`, `Spawn`, …) with a delay, a parameter and a
fire-once flag. A connection editor in the entity inspector, lines between connected
entities drawn in the view, and warnings for broken targets.

**Precedent:** Hammer 1's I/O system; CryEngine Flowgraph and Unreal Kismet for the
graph-based version.

**Fit:** Outputs and inputs are declared in the FGD, or better, exported from Unity
components through the catalog. In Unity, each connection becomes ECS event components
and a small dispatch system.

### 5.2 Volumes as first-class objects (M)
**What:** Typed brush volumes, each with its own colour, view filter and presets:
- trigger
- kill
- fog
- post-processing
- reverb
- nav modifier
- streaming
- music

**Precedent:** Unreal's volume types; Hammer's tool textures.

**Fit:** Brush entities already exist. This adds visual identity, filtering, and a
"create volume" menu that sets up the class and properties.

### 5.3 AI encounter authoring (XL)
**What:** Spawn groups, waves with conditions, cover nodes, patrol paths using the
spline tool, and the baked Unity navmesh drawn as an overlay. Unreachable areas show
while you build, and AI sight cones can be previewed.

**Precedent:** idStudio's encounter tools in DOOM (2016) and Eternal; the Creation Kit's
navmesh view.

**Fit:** Navmesh export over the live link or as a catalog file; encounter entities
with I/O hooks from 5.1.

---

## 6. Metrics and scale (S–M)

**What:**
- A ghost player capsule that follows the cursor or sits where you click.
- Jump height and reach arcs drawn from the selected ledge.
- Doorway, corridor and step checks against player and AI sizes.
- A "metrics gym" set of prefabs and a test map.

**Precedent:** Metrics gyms are standard practice in level design; Hammer's player-sized
ruler.

**Fit:** Player sizes and movement values come from Unity (the catalog or a settings
asset), so the editor always matches the real controller.

---

## 7. Map check (M)

**What:** One validation panel; clicking an issue focuses it in the view. Checks
include:
- leaks
- missing materials or prefabs
- decals that cover nothing
- broken I/O targets
- duplicate entities on top of each other
- entities outside the world
- per-room budgets (light count, triangles, draw calls) reported back from Unity

**Precedent:** Unreal's Map Check; Hammer's Check for Problems.

**Fit:** Most of the data already exists: the decal renderer knows what a decal covers,
the catalog knows what assets exist, and the room creator knows the rooms. Budgets come
over the live link.

---

## 8. Modular kits and instances

### 8.1 Snap sockets (M)
**What:** Prefabs carry named sockets, such as a corridor end or a doorway. Dragging one
piece near another snaps it to the nearest matching socket, at the right position and
rotation.

**Precedent:** Modular kit snapping in Unreal and in Bethesda's editors.

**Fit:** Sockets are authored on Unity prefabs as child markers and exported in the
catalog; placement snapping hooks into the prefab placement tool.

### 8.2 Brush instances with overrides (L)
**What:** A reusable brush group, such as a stairwell or a vending nook, placed many
times. Changes to the source update every copy, and each copy can override chosen
properties.

**Precedent:** Hammer's `func_instance`; Unreal's level instances.

**Fit:** Builds on TrenchBroom's linked groups, adding overrides and a source file.

---

## 9. Lighting and environment (M each)

- **Light overlap view.** A view mode colouring each area by how many lights reach it,
  to stay within the forward or Forward+ light limits.
- **Probe volumes.** Light-probe and reflection-probe volumes as typed volumes (5.2),
  exported to Unity's probe components.
- **Time of day and sky preview.** Sky and sun settings from Unity, shown in the 3D view
  as a sky colour and sun direction.
- **Light linking.** Which layers each light affects, if the URP setup supports it
  through rendering layers.

---

## 10. Smaller day-to-day improvements

- **Command palette (S).** Ctrl+P or F3: fuzzy search across every action, material,
  prefab, decal and named entity. Precedent: Blender's F3 search, VS Code.
- **Camera bookmarks (S).** Ctrl+1…9 saves the camera, 1…9 returns to it; saved in the
  map.
- **Note entities (S).** Sticky notes for review comments, with author, status and text,
  drawn as labels and listed in a panel. Precedent: Unreal's Note actor.
- **Layers as DOTS SubScenes (M).** Each layer exports as its own SubScene: streaming
  boundaries for free, and fewer merge conflicts when two people work on one map.
- **Visual map diff (L).** Compare the map with any git revision and highlight added,
  removed and changed brushes and entities in the view. Almost no editor has this.

---

## Suggested order

1. **Play from here (1.1).** The biggest daily time saver; mostly the live link.
2. **Map check (7).** Cheap, and catches problems before Unity does.
3. **Hotspot texturing (2.1).** Reuses the decal atlas pipeline and cuts down unique
   textures.
4. **Splines (3), then scatter (4).** The biggest jump in how finished a level looks.
5. **I/O connections (5.1) and volumes (5.2).** They turn geometry into gameplay; the
   encounter tools (5.3) build on them.
6. The smaller items in 10 fit in between larger ones.
