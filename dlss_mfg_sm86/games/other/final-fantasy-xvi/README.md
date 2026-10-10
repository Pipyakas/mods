# Final Fantasy XVI

FF16 has its own FG toggle but only ever asks for 2x, and ships a Streamline runtime
too old for multi-frame generation (2.4.10). No bootstrap is needed. The shipped INI
uses Mode 1 (Dynamic MFG).

**VRAM.** The extra generated frames need more VRAM than 2x. On an 8 GB GPU
(RTX 3070 Laptop, 1440p) Streamline's VRAM budget runs out soon after launch: it
returns `eWarnOutOfVRAM` (result 39), and the mod falls back to the game's 2x
(`... rejected result=39; override disabled`). Switching FG off and on in game
retries. The game was also GPU-bound there: 2x and 4x both gave ~72 fps. With
VRAM to spare, 4x and Mode 1 run as in other games.

## Install (manual)

Paths are relative to the game folder, e.g. `D:\steamapps\common\FINAL FANTASY XVI`.
Back up every file you replace first.

| Copy | To | What |
|---|---|---|
| `mod\winmm.dll` | game folder | dlss_mfg_sm86 proxy |
| `mod\dlss_mfg_sm86.ini` | game folder | MFG mode (Mode 1, Dynamic MFG) |
| `mod\dlssg_sm86.ini` | game folder | dlssg_sm86 settings (`Optimized=3`, 4x ceiling) |
| `version.dll` from the [fork root](../../../../version.dll) | game folder | dlssg_sm86 itself |
| Streamline SDK 2.14.1 `bin\x64`: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.dlss.dll`, `sl.pcl.dll`, `sl.reflex.dll`, `nvngx_dlssg.dll` | game folder | **required**: the game ships 2.4.10, which has no multi-frame code ([notes](../README.md)) |

`ffxvi.exe` statically imports both `version` and `winmm`.

## In game

- Turn DLSS Frame Generation on in the graphics settings. The frame rate limit can't
  exceed the refresh rate while FG is on.

## Check after launch

- `dlss_mfg_sm86.log` in the game folder: `Runtime ...\sl.dlss_g.dll version=2.14.1.0`,
  both hooks `MH_OK`, then `Options callerVersion=1 mode=1 ... effectiveMode=3
  dynamicOverride=1 result=0` and `State ... presented=4`.

Verified live at 120 Hz: Mode 2 at 4x (`override=1 result=0`, `presented=4`) and
Mode 1 (`dynamicOverride=1`, `presented=4`) right after launch; later in the session
the VRAM fallback above.
