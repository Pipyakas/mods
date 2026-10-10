# Dynasty Warriors: Origins

DW Origins has its own FG toggle but only ever asks for 2x, and ships a Streamline
runtime too old for multi-frame generation (2.4.10). No bootstrap is needed.

The shipped INI uses **Mode 1** (Dynamic MFG). Where the game's large battles
saturate the CPU (an 8-core laptop: all threads ~90%), Dynamic MFG stays at 1x-2x:
~77 fps at 120 Hz. Mode 2 (`Mode=2`, default bands: 2x at 60 Hz, 4x at 120 Hz) is
the better choice there. Fixed 4x holds 120 fps from a 30 fps base
and drops CPU load to ~35%; fixed 3x (`MediumRefreshMaxHz=120`) gave ~118 fps from
~40 fps, for lower latency.

## Install (manual)

Paths are relative to the game folder, e.g. `D:\steamapps\common\DWORIGINS`.
Back up every file you replace first.

| Copy | To | What |
|---|---|---|
| `mod\winmm.dll` | game folder | dlss_mfg_sm86 proxy |
| `mod\dlss_mfg_sm86.ini` | game folder | MFG mode (Mode 1, Dynamic MFG; Mode 2 for CPU-bound systems) |
| `mod\dlssg_sm86.ini` | game folder | dlssg_sm86 settings (`Optimized=3`, 4x ceiling) |
| `version.dll` from the [fork root](../../../../version.dll) | game folder | dlssg_sm86 itself |
| Streamline SDK 2.14.1 `bin\x64`: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.dlss.dll`, `sl.pcl.dll`, `sl.reflex.dll`, `nvngx_dlssg.dll` | `dll\release\` | **required**: the game ships 2.4.10, which has no multi-frame code ([Katana notes](../README.md)) |

`DWORIGINS.exe` statically imports both `version` and `winmm`.

## In game

- Turn Frame Generation on in the game's graphics settings.
- With FG on, the game's own frame rate limit must not cap the output below the
  refresh rate.

## Check after launch

- `dlss_mfg_sm86.log` in the game folder: `Runtime ...\dll\release\sl.dlss_g.dll
  version=2.14.1.0`, both hooks `MH_OK`, then `Options callerVersion=1 mode=1 ...
  effectiveMode=3 dynamicOverride=1 result=0` (Mode 2: `effectiveGenerated=3 override=1`,
  then `State ... presented=4`).

Verified live at 120 Hz: Mode 1 (`dynamicOverride=1`, mostly `presented=2`, ~77 fps),
Mode 2 at 3x (`presented=3`, ~118 fps) and at 4x (`presented=4`, 120 fps).
