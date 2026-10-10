# Immortals of Aveum

Immortals of Aveum has its own FG toggle but only ever asks for 2x, and ships a
Streamline runtime too old for multi-frame generation (2.4.0). No bootstrap is
needed. The shipped INI uses Mode 1 (Dynamic MFG).

## Install (manual)

Paths are relative to the game folder, e.g. `D:\steamapps\common\ImmortalsOfAveum\x64\Windows`.
Back up every file you replace first.

| Copy | To | What |
|---|---|---|
| `mod\P3\Binaries\Win64\winmm.dll` | `P3\Binaries\Win64\` | dlss_mfg_sm86 proxy |
| `mod\P3\Binaries\Win64\dlss_mfg_sm86.ini` | `P3\Binaries\Win64\` | MFG mode (Mode 1, Dynamic MFG) |
| `mod\P3\Binaries\Win64\dlssg_sm86.ini` | `P3\Binaries\Win64\` | dlssg_sm86 settings (`Optimized=3`, 4x ceiling) |
| `version.dll` from the [dlssg_for_sm86](../../../../externals/dlssg_for_sm86/version.dll) | `P3\Binaries\Win64\` | dlssg_sm86 itself |
| Streamline SDK 2.14.1 `bin\x64`: the eight runtime DLLs | `Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64\` | **required**: the game ships 2.4.0, which has no multi-frame code ([UE5 notes](../README.md)) |

`ImmortalsOfAveum-Win64-Shipping.exe` statically imports `version`, `winmm` and `dxgi`.

## In game

- Turn Frame Generation on in the game's settings.

## Optional offline / fast startup

Startup handling is now a separate [Game Startup](../../../../game_startup/README.md)
DLL, independent of DLSS/MFG and usable on AMD/Proton. Copy its `game_startup.dll`
and Aveum `game_startup.ini` beside the game EXE. Keep this mod's updated
`winmm.dll`: it loads the startup DLL when present. Do not overwrite it with the
startup-only winmm loader, which is for installs without MFG.

```ini
[Startup]
AutoOffline=1
SkipIntro=1
SkipWarnings=1
```

- `AutoOffline` selects the game's own offline mode, advances the title screen
  after license initialization, and handles the EA connection-error **Play
  Offline** dialog if it appears. It stops once the main menu is reached; it
  does not automatically Continue/New Game or override later online choices.
- `SkipIntro` skips only `companylogo.mp4`, `publisherlogo.mp4`, and
  `enginelogo.mp4` in `P3\Content\Movies`. The DLL suppresses their read-only
  file lookups/opens in this process; it does not rename or delete files.
  Story cutscenes and loading screens remain untouched.
- `SkipWarnings` hides the startup seizure/save-icon notices and advances their
  own completion flow. **Optimizing Game Files stays visible**.

Set a key to `0` in `game_startup.ini` to disable that feature, then restart.
All three default to **off** in the standalone mod's generic config and apply
only to implemented games (currently Aveum). They do not bypass EA/platform entitlement checks,
activation requirements, or first-run agreements, and do not change account or
network settings. No separate injector is needed.

Verified with the original logo files present and probe mode off: the log lists
all three `Startup Aveum: skipped ...` messages, followed by offline-mode
selection and `main menu reached; startup automation finished`.

## Check after launch

- `P3\Binaries\Win64\dlss_mfg_sm86.log`: `Streamline game, engine=Unreal`,
  `Runtime ...\sl.dlss_g.dll version=2.14.1.0`, both hooks `MH_OK`, then
  `Options callerVersion=1 mode=1 ... effectiveMode=3 dynamicOverride=1 result=0` and
  `State ... presented=4` (Mode 2: `effectiveGenerated=3 override=1`).

Verified live at 120 Hz on an RTX 3070 Laptop: Mode 1 (`dynamicOverride=1`, picks 4x,
~111 fps) and Mode 2 at 4x (`presented=4`, ~95 fps in a different scene).
