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

## Layout

```
Source/HL2Blockout/        game module (character, game mode, blockout actor)
Plugins/PBCharacterMovement/  vendored movement plugin (MIT, Project Borealis)
Content/Python/            editor scripts that generate the PointInsertion map
Config/                    default map, game mode, HL2 gravity, Enhanced Input
```

## Credits

Movement: [Project Borealis - PBCharacterMovement](https://github.com/ProjectBorealis/PBCharacterMovement) (MIT).
Half-Life 2 and City 17 are trademarks of Valve Corporation; this is a fan greybox with no Valve assets.
