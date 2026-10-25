# Astral Fortress

An original single-player map for the original Quake. The Orrery Court is the central landmark: its open star well, suspended armillary, and raised gallery tie together the tomb cloister, north planetarium, eastern mechanism hall, and Meridian vault.

## Route

1. Enter through the south gatehouse and survey the Orrery Court. The north stair is sealed by a silver lock.
2. Explore the west tomb cloister, climb to the upper choir, and take the silver key. The choir arch returns to the court from above.
3. Open the north seal, reach the planetarium, and press its Meridian control. It opens both eastern shutters, creating a low route and a high return into the court.
4. Cross the mechanism hall, climb the star tower for the gold key, then return through the opened shutter to the court.
5. Unlock the gold seal into the vault. Fight across its split levels and heart pit, then reach the exit at the far stair.

There are four optional secrets: the flooded offering below the Orrery, the choir keeper's plate, the west rune cache, and the final vault cache. The map uses Quake's silver and gold keys, four weapons, multiple armor tiers, health and ammunition pickups, and monsters placed on the room's different levels.

## Files and assets

- `astral_fortress.map` is the Valve-format Quake source map currently open in TrenchBroom.
- `build_map.py` regenerates the source map and the non-compiling source-check reports in `qa/`.
- `qa/` contains editor viewport captures and generated source-check reports.
- The source references `base.wad`, `jr_med.wad`, `medieval.wad`, `wizard.wad`, and `metal.wad` from `C:\Users\thele\Downloads\quake_map_source-master\gfx`. Change `GFX` in `build_map.py` if those WADs live elsewhere.

No BSP was compiled, per the request.
