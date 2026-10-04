# Half-Life 2 Unreal Scene

Unreal Engine 5.7 C++ project (`HL2Blockout`) that uses
[PBCharacterMovement](https://github.com/ProjectBorealis/PBCharacterMovement) for Half-Life 2 / Source-style
movement and a code-generated greybox inspired by Half-Life 2's opening chapter, *Point Insertion*.

## Requirements

- Unreal Engine **5.7.1**
- A C++ toolchain supported by UE 5.7 (Visual Studio 2022 with the "Game development with C++" workload on Windows,
  Xcode on macOS, or the bundled clang toolchain on Linux)

PBCharacterMovement is vendored as source under `Plugins/PBCharacterMovement` (v3.1.0, MIT). Upstream only ships
UE 5.5 binaries, so it is compiled from source together with the game module on first build.

## Getting started

1. Right-click `HL2Blockout.uproject` -> **Generate Visual Studio project files** (or open the `.uproject` and
   accept the prompt to rebuild missing modules).
2. Build the `HL2BlockoutEditor` target (Development Editor) and launch the editor.
3. On first launch `Content/Python/init_unreal.py` creates `/Game/Maps/PointInsertion`, drops the blockout actor
   and a PlayerStart inside the train, and saves the map. This is the project's default editor and game map.
   To regenerate it manually, run in the Output Log (Python):
   ```python
   import hl2_blockout; hl2_blockout.create_map(overwrite=True)
   ```
4. Press **Play**. You spawn inside the arriving train.

`HL2GameMode` is the global default game mode. If a level has no PlayerStart it spawns the blockout (when none
exists) and starts you on the train, so even an empty level is playable.

## Controls (HL2 defaults)

| Action | Key |
| --- | --- |
| Move | W A S D |
| Look | Mouse |
| Jump | Space |
| Crouch (hold) | Left Ctrl |
| Sprint | Left Shift |
| Walk | Left Alt |
| Flashlight | F |
| Noclip | V |

Mouse sensitivity, Y inversion and hold/toggle crouch are exposed on `AHL2Character` (`Config/DefaultGame.ini`).
`MouseSensitivity` is degrees per mouse count, like HL2's `m_yaw * sensitivity` (default `0.066` = HL2 sensitivity 3);
`Config/DefaultInput.ini` sets the engine mouse axis scale to 1 so this value is not scaled again.
Input actions are created at runtime; assign your own Enhanced Input assets on a Blueprint child to override them.

Movement (bunnyhopping, air strafing, crouch jumping, jump boost, HL2 friction and step-up) comes
from `UPBPlayerMovement`. World gravity is set to HL2's `sv_gravity 600` (`-1143 cm/s^2`).

## The blockout

`AHL2PointInsertionBlockout` builds all geometry in its construction script from Hammer units
(`1 HU = 1.905 cm`) using instanced engine cubes/cylinders, one instanced component per palette material:

The footprint follows the Point Insertion overview map (+X from the rail yard towards the plaza, +Y from the tracks
towards the apartments):

1. **Rail yard (Start)** - tunnel portals, six tracks, signal gantry and box, tenement, arched warehouse, parked freight.
2. **Arrival platform** - long pitched canopy, benches, Breencast screens, the arriving train (spawn in the middle car).
3. **Train shed** - glazed barrel-vault shed with island/side platforms and parked trains; skylit building beside it.
4. **Security** - queue railings, Combine scanner gate, CP desk, interrogation room, corridor to the hall.
5. **Station hall** - arched roof, great window, pillars, ticket booths, Breencast screen, mezzanine, steps to the plaza.
6. **City 17 plaza** - Combine wall, Breen monument ringed by trees, domed civic building and clock tower, elevated
   railway, barricades, APC.
7. **Street -> courtyard -> Resistance apartments** - barricaded street, alley into the courtyard, four-storey block
   with a switchback stairwell to the roof.
8. **Rooftops -> attic -> End** - drop to the neighbouring roof, plank, stepped pitched roof, water-tower roof, plank
   into the attic window, stairs down into the end room; factory smokestacks nearby. Fire escapes lead back up from
   each alley.
9. **Skyline** - distant City 17 blocks and the Citadel.

Details panel options: `Palette` (per-material colours), `bIncludeSkyAndLighting` (sun, sky light, atmosphere, fog),
`bSpawnPhysicsProps` (cans, crates, oil drums spawned on BeginPlay) and a **Rebuild Blockout** button.

Edit layout in `Source/HL2Blockout/Private/HL2PointInsertionBlockout.cpp` (`Build*` functions), recompile, and the
placed actor regenerates.

## Optional: Neighborhoods blockout

`AHL2NeighborhoodsBlockout` is a second, geometry-only greybox: a sprawling, overgrown storybook old town (warm
plaster, crooked timber houses with steep roofs, dormers, towers, bridges, ruins). It does not replace Point
Insertion, is not generated on startup and adds **no gameplay systems** - blockers, hidden rooms, upgrade ledges
and the maintenance robot are static placeholder shapes (bright magenta `Marker` material). Houses are solid shells
with decorative doors and cannot be entered.

Create it from the Output Log (Python), then open `/Game/Maps/Neighborhoods` and press **Play** (spawn: hub plaza):
```python
import hl2_blockout; hl2_blockout.create_neighborhoods_map(overwrite=True)
```
Or drag an `HL2NeighborhoodsBlockout` actor into any level that uses `HL2GameMode`.

Layout (+Y north, hub at the origin, roughly 13,000 HU square):

- **Central hub** - market square around a great camphor tree, well, stalls, notice board / workbench / robot dock
  placeholders. Six pathways leave it: **Lantern Road** (N), **Tram Steps** (NE), **Rubble Row** (E), **Canal Walk**
  (S), **Windmill Path** (SW), **Grove Path** (W).
- **Roads** - Lantern Road, Maple Lane, Sparrow Close, Rubble Row and Quarry Lane; three T-junctions (Maple Lane,
  Sparrow Close, Quarry Lane) and three cul-de-sacs (C1-C3) with tree islands.
- **Districts** - dense north suburbs (shrine, water tower, allotments), Clocktower Hill with an overgrown tram,
  rubble terraces with houses built on/into a ruined apartment block, a fallen tower block and quarry, the canal
  quarter (promenade, warehouses, raised drawbridge, broken footbridge, waterwheel, lock tower), the grove with a
  house grown around a giant tree, and windmill fields.
- **Counts** - 30 houses plus terrace/row frontages, 7 hidden-room placeholders (H1-H7, crawl-height entrances),
  4 blocker placeholders (B1 rubble, B2 brambles, B3 collapsed tunnel, B4 drawbridge), side paths, an ivy-curtain
  secret path, ledges and bridges for ~10 distinct routes.

Without noclip, the rubble terraces (behind B1) and the far canal bank (B4 / footbridge gap) are not reachable
with the standard movement; everything else is.

Edit layout in `Source/HL2Blockout/Private/HL2NeighborhoodsBlockout.cpp`.

## Layout

```
Source/HL2Blockout/        game module (character, game mode, blockout actors)
Plugins/PBCharacterMovement/  vendored movement plugin (MIT, Project Borealis)
Content/Python/            editor scripts that generate the PointInsertion / Neighborhoods maps
Config/                    default map, game mode, HL2 gravity, Enhanced Input
```

## Credits

Movement: [Project Borealis - PBCharacterMovement](https://github.com/ProjectBorealis/PBCharacterMovement) (MIT).
Half-Life 2 and City 17 are trademarks of Valve Corporation; this is a fan greybox with no Valve assets.
