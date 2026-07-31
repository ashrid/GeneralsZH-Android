# Generals Zero Hour Mod File Priority

This document describes the behavior of this Android fork. It distinguishes
the original retail SAGE behavior from the fork's deliberate archive-override
rules.

## Android Fork Hierarchy

For a requested asset path, the effective priority is:

| Priority | Source | Behavior |
| --- | --- | --- |
| 1 | Loose file in `GameData/` | The local file system is checked before archives. Loose files must preserve the archive's internal path, for example `GameData/Data/INI/Armor.ini`. |
| 2 | Loose file in the selected mod directory | A selected `GameData/Mods/<ModName>/Data/...` file is checked after the primary GameData root but before every archive. |
| 3 | Selected mod archive | An archive anywhere under `GameData/Mods/<ModName>/`, or a directly selected `-mod /path/to/file.big`, loads after retail archives and overrides matching retail archive entries. |
| 4 | Retail archives | The primary GameData archive scan supplies the base Generals and Zero Hour `.big` files. |

Only one mod selection is active at a time: either a mod directory or a direct
`.big` archive. The Android Mods picker writes the selected directory to
`mod.txt`; an Intent extra can select a mod for one launch.

## Archive Isolation

The primary Android archive scan deliberately excludes the top-level
`GameData/Mods/` subtree. An archive in an inactive mod folder is not mounted
and cannot supply missing assets or override retail data. The selected mod is
mounted later by `loadMods()`.

An arbitrary `.big` placed directly in `GameData/` is different: it belongs to
the primary scan and is always active. Put switchable mod archives under
`GameData/Mods/<ModName>/`, not in the GameData root.

## INIZH.big Rule

Retail installs can contain a duplicate `Data/INI/INIZH.big`, which is skipped
during primary discovery to avoid CRC mismatches. That skip does not apply to a
selected mod, so a mod-provided `Data/INI/INIZH.big` can override retail data.

## Retail Versus This Fork

The EA retail source checks local loose files before archives. Retail archive
collisions use the first loaded archive, so historical advice often recommends
an alphabetically early archive name such as `0Mod.big`.

This fork intentionally differs for archive collisions: later-loaded archives
win so Zero Hour and selected mod data override earlier base archives. Do not
use an early filename prefix to control priority in the Android fork; place the
archive in the selected mod directory instead.

## Android Storage

On Android 16, use the in-app Mods picker and its Storage Access Framework
folder importer. Direct ADB writes to the app-owned `Android/data` directory
are not a supported installation path.

## Source Evidence

- [EA FileSystem.cpp](https://github.com/electronicarts/CnC_Generals_Zero_Hour/blob/0a05454d8574207440a5fb15241b98ad0b435590/GeneralsMD/Code/GameEngine/Source/Common/System/FileSystem.cpp)
  shows that local files are queried before archive files.
- `Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp`
  performs primary and selected-mod archive discovery.
- `Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp` loads the
  selected mod after archive initialization and applies archive override order.
