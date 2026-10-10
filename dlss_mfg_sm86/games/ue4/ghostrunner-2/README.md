# Ghostrunner 2

Ghostrunner 2 (UE 4.27) has its own FG toggle but only ever asks for 2x, and ships
a Streamline runtime too old for multi-frame generation (2.2.0). No bootstrap is
needed. The shipped INI uses Mode 1 (Dynamic MFG).

## Install (manual)

Paths are relative to the game folder, e.g. `D:\steamapps\common\Ghostrunner 2`.
Back up every file you replace first.

| Copy | To | What |
|---|---|---|
| `mod\Ghostrunner2\Binaries\Win64\winmm.dll` | `Ghostrunner2\Binaries\Win64\` | dlss_mfg_sm86 proxy |
| `mod\Ghostrunner2\Binaries\Win64\dlss_mfg_sm86.ini` | `Ghostrunner2\Binaries\Win64\` | MFG mode (Mode 1, Dynamic MFG) |
| `mod\Ghostrunner2\Binaries\Win64\dlssg_sm86.ini` | `Ghostrunner2\Binaries\Win64\` | dlssg_sm86 settings (`Optimized=3`, 4x ceiling) |
| `version.dll` from the [fork root](../../../../version.dll) | `Ghostrunner2\Binaries\Win64\` | dlssg_sm86 itself |
| Streamline SDK 2.14.1 `bin\x64`: the eight runtime DLLs | `Ghostrunner2\Plugins\Streamline\Binaries\ThirdParty\Win64\` | **required**: the game ships 2.2.0, which has no multi-frame code ([UE5 notes](../../ue5/README.md)). 2.2.0 has no `sl.pcl.dll`, `sl.deepdvc.dll` or `nvngx_deepdvc.dll`; add them |

`Ghostrunner2-Win64-Shipping.exe` statically imports `version`, `winmm` and `dxgi`.

## In game

- Turn Frame Generation on in the game's settings.

## Check after launch

- `Ghostrunner2\Binaries\Win64\dlss_mfg_sm86.log`: `Streamline game, engine=Unreal`,
  `Runtime ...\sl.dlss_g.dll version=2.14.1.0`, both hooks `MH_OK`, then
  `Options callerVersion=1 mode=1 ... effectiveMode=3 dynamicOverride=1 result=0`
  (Mode 2: `effectiveGenerated=3 override=1`, then `State ... presented=4`).

Verified live at 120 Hz on an RTX 3070 Laptop: Mode 1 (`dynamicOverride=1`, mostly 3x,
sometimes 2x or 4x; GPU 100%, ~7.2 GB VRAM) and Mode 2 at 4x (`presented=4`, ~120 fps,
GPU ~90%, ~5 GB VRAM).
