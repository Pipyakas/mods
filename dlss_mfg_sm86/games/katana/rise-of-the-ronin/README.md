# Rise of the Ronin

Rise of the Ronin has its own FG toggle but only ever asks for 2x, and ships a
Streamline runtime too old for multi-frame generation (2.4.15). No bootstrap is
needed.

The shipped INI uses **Mode 1** (Dynamic MFG). Its Mode 2 bands are Halo's: 3x at
120 Hz (`LowRefreshMaxHz=80`, `MediumRefreshMaxHz=120`). Measured at 120 Hz on an
RTX 3070 Laptop (GPU-bound, GPU ~97%): Mode 1 picks 4x (~116 fps); fixed 4x gave
~116 fps from a ~29 fps base with visible artifacts; 3x gives ~114 fps from
~38 fps. On that 8 GB GPU, Mode 1 once ran out of VRAM and fell back to 2x (~49 fps).

## Install (manual)

Paths are relative to the game folder, e.g. `D:\steamapps\common\Ronin`.
Back up every file you replace first.

| Copy | To | What |
|---|---|---|
| `mod\winmm.dll` | game folder | dlss_mfg_sm86 proxy |
| `mod\dlss_mfg_sm86.ini` | game folder | MFG mode (Mode 1, Dynamic MFG; Mode 2 set to 3x at 120 Hz) |
| `mod\dlssg_sm86.ini` | game folder | dlssg_sm86 settings (`Optimized=3`, 4x ceiling) |
| `alternatives\dinput8.dll` from the [dlssg_for_sm86](../../../../externals/dlssg_for_sm86/alternatives/) | game folder | dlssg_sm86 itself |
| Streamline SDK 2.14.1 `bin\x64`: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.dlss.dll`, `sl.pcl.dll`, `sl.reflex.dll`, `nvngx_dlssg.dll` | game folder | **required**: the game ships 2.4.15, which has no multi-frame code ([Katana notes](../README.md)) |

`Ronin.exe` statically imports `winmm`, `dinput8` and `dbghelp` but not `version`,
so dlssg_sm86 goes in as its `dinput8.dll` alternative.

## In game

- Turn Frame Generation on in the graphics settings. The game turns FG off in menus
  and loading screens; each on request is rewritten again.
- At 7–7.7 GB VRAM use on an 8 GB GPU, a rejected override (`result=39`) falls back
  to 2x; toggling FG retries.

## Check after launch

- `dlss_mfg_sm86.log` in the game folder: `Runtime ...\sl.dlss_g.dll version=2.14.1.0`,
  both hooks `MH_OK`, then `Options callerVersion=1 mode=1 ... effectiveMode=3
  dynamicOverride=1 result=0` (Mode 2: `effectiveGenerated=2 override=1`, then
  `State ... presented=3`).

Verified live at 120 Hz: Mode 2 at 4x (`presented=4`, ~116 fps) and 3x
(`presented=3`, ~114 fps); Mode 1 (`dynamicOverride=1`, `presented=4`).
