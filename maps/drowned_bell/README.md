# The Drowned Bell

Original Quake single-player map, standard `.map` format.

Open **drowned_bell.map** in TrenchBroom with the Quake game configuration.
The map references the existing `medieval.wad`, `wizard.wad`, and `metal.wad`
under `C:/Users/thele/Downloads/quake_map_source-master/gfx`.
If moving the source to another machine, update its `wad` paths to those WADs.

The route crosses a flooded nave, loops through a tomb cloister and its upper
choir for the silver key, then enters the sanctuary for the gold key. The altar
control lowers the bell platform into a second canal crossing. Returning to
the nave unlocks the gold sluiceworks gate; a stair through the final encounter
opens the upper return grate and reaches the exit shrine.

There are four optional secrets: the underwater offering beneath the north
gallery, the shootable demon panel beside the choir landing, the tomb canopy
reached from above, and the recess behind the sanctuary altar. Secrets are
not required to reach either key or the exit.

Difficulty filtering provides 24 / 34 / 36 monsters on easy / normal / hard.
Shamblers replace selected hell knights on normal and hard. Combat supplies
and weapons are distributed along the required route, with quad, red armor,
megahealth and a rocket launcher as optional rewards.

Source verification: opened in TrenchBroom, interior views inspected,
convex brush and entity clearance checks completed, texture names and target
links checked. No compilation or gameplay testing was performed, as requested.
Play duration, lighting and encounter balance remain unmeasured in-game.

`build_map.py` is the reproducible authoring source. It writes the `.map` and
source QA data only. It does not invoke any compiler. `tb_control.py` drives
the documented TrenchBroom command-line client for editor inspection.

The editor uses socket `drowned-bell`.
