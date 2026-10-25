# E1M10: The Sluice Works

A continuation map for Quake's Episode 1, *Dimension of the Doomed*. It's a base-style level
using only `base.wad`, like E1M1 (*The Slipgate Complex*), and it's built to about the same
size and complexity.

You arrive through the slipgate network at a coolant and sluice facility. Its bridge is
retracted and its exit gate is sealed by a relay core.

Open `e1m10.map` in TrenchBroom with the Quake game configuration. The worldspawn `wad` key
points to `C:/Users/thele/Downloads/quake_map_source-master/gfx/base.wad`. If that folder
moves, change `WAD` in `build_map.py` and rebuild.

## Route

The level is linear but folds back on itself, with spaces you see well before you reach them.

1. **Arrival gate.** Step out of the inactive slipgate and go north through the intake.
2. **Gallery.** Windows look down into the Sluice Hall, where Grunts patrol the floor and a
   sentry stands on the high catwalk you can't reach yet. Stairs drop into the hall.
3. **Sluice Hall.** A 256-unit slime canal cuts the hall in two, too wide to jump. Its bridge is
   retracted into the south lip, with only the edge showing.
4. **Pump room** (west, behind a door). Fight among the pumps and press the wall button on the
   control platform. The bridge slides out across the canal.
5. **Lift.** Cross the canal and take the lift down to the **underworks landing**.
6. **Flooded channel.** Walkways run either side of a water trough, crossed by grate bridges.
   A stair at the far end climbs into daylight.
7. **Yard** (open to the sky). It has a loading-dock canopy, a relay mast, and a raised terrace
   with an armory post off the east wall. The **silver keycard** sits on a plinth in the pool,
   reached by stepping stones. Picking it up opens an ambush closet in the north wall.
8. **Control tower.** Enter from the terrace and climb the stair along the north wall to the
   mezzanine, where the windows overlook the yard. A high corridor runs south.
9. **Catwalk return.** The corridor comes out on the Sluice Hall's upper catwalk, the one you
   saw from the gallery. Walk it above the canal to the **silver door** at the west end.
10. **Relay arena.** From the entry balcony, a stair descends to a slime moat. Reaching the
    floor opens monster closets on both side walls. The relay core stands on an island in the
    moat. **Shoot its button**: a bridge extends on the east side and the north gate opens.
11. **Exit slipgate** behind the gate, marked with an EXIT sign. It's a `trigger_changelevel`
    to `start`.

## Secrets (6)

| # | Where | How | Reward |
|---|---|---|---|
| S1 | Arrival chamber, east wall | Shoot the slightly different panel (`tech08_2`) | Green armor, shells |
| S2 | Flooded channel | Dive into the trough and swim south under the walkway into an air pocket | Megahealth, shells |
| S3 | Pump room, south wall | Shoot the odd panel (`twall2_1`) | Nailgun, nails |
| S4 | Sluice Hall upper catwalk | Jump 32 up into the alcove in the east wall | Quad Damage |
| S5 | Yard, north-east corner | Climb the crate stack to the alcove | Grenade launcher, rockets |
| S6 | Exit corridor, west wall | Shoot the panel (`tech06_2`) | Yellow armor, nails, shells |

None of the secrets is needed for the keycard or the exit.

## Compared with E1M1

| | E1M1 (id source) | E1M10 |
|---|---|---|
| Brushes | 1,257 | 1,064 |
| Footprint (units) | 2,248 × 3,504 × 896 | 3,616 × 3,024 × 992 |
| Monsters (easy / normal / hard) | 42 placed in total | 27 / 38 / 51 |
| Secrets | 6 | 6 |
| Lights | 190 | 126 |
| Doors / buttons / lifts | 14 / 6 / 2 | 14 / 2 / 1 (+3 secret doors) |

Monsters are Grunts and Rottweilers like E1M1, plus Enforcers and one Ogre for a later
Episode 1 map. Weapons on the main route are the super shotgun (guard post) and nailgun
(tower). Two more weapons are in secrets.

## Files

- `e1m10.map`: the source map, standard format.
- `build_map.py`: the reproducible generator, and the actual source for the map. It defines
  rooms as air volumes, builds a sealed shell around their union, then adds structure, detail,
  and population. `--stage 1..4` writes partial builds and `--cutaway` leaves out
  ceilings and sky for overview captures.
- `qa/source_checks.json`: source-level checks. Every monster, item and player spawn is inside
  the air volumes, clear of solid geometry and standing on a floor. Every `target` has a
  matching `targetname`. Detail brushes don't interpenetrate except at deliberate joints.
  Totals are recorded. It currently reports no problems.
- `tb.py` / `shots.sh`: drive a running TrenchBroom
  (`TrenchBroom.exe --command-line-control-socket e1m10`) through the documented command-line
  control client for loading and viewport captures.
- `progress/`: TrenchBroom viewport captures of each stage. These are blockout, structure,
  detail and the final populated map, from the same cameras, plus extra views of the final
  map. Overview shots use the cutaway build.

## Verification and limits

- The map was loaded in TrenchBroom (Quake config) at every stage. The editor log shows no
  brush, texture or model warnings.
- The map was **not compiled or playtested**, as requested. Movement constraints were reasoned
  from Quake's physics, not tested in game. The canal and moat are wider than a running jump,
  the crate steps rise 32 units, the S4 alcove is a 32-unit jump, and the water exits are
  8–16 units above the surface.
- Lighting values, combat balance and play time haven't been measured. Expect a light-tuning
  pass after the first compile.
- The worldspawn includes `_sunlight` keys for ericw-tools. The original id tools ignore them.
