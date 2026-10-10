# Katana Engine games

Koei Tecmo's Katana Engine integrates DLSS-G through NVIDIA's Streamline SDK directly
(no engine plugin). The core DLL needs nothing engine-specific: it sees a Streamline
game, engine=other, so the Unreal console-variable features (Mode 3 dynamic
resolution, `[ConsoleVariables]`) are off.

**Where files go.** dlssg_sm86 (`version.dll` + `dlssg_sm86.ini`), the
dlss_mfg_sm86 proxy and `dlss_mfg_sm86.ini` go next to the game EXE in the game
folder's root.

**Streamline runtime.** The runtime is in `dll\release` (`sl.interposer.dll`,
`sl.common.dll`, `sl.dlss_g.dll`, `sl.dlss.dll`, `sl.pcl.dll`, `sl.reflex.dll`,
`nvngx_dlssg.dll`). The version table in the [UE5 notes](../ue5/README.md) applies:
below 2.7, replace these seven files with the Streamline SDK 2.14.1 release ones
(`bin\x64`; the full SDK zip has `sl.dlss.dll`). Leave `nvngx_dlss.dll` (the DLSS
upscaling model) as shipped. Back up the originals first.

## Games

- [Dynasty Warriors: Origins](dynasty-warriors-origins/README.md)
- [Rise of the Ronin](rise-of-the-ronin/README.md)

**Where files go** above assumes the EXE imports `version`; Rise of the Ronin's
doesn't, so dlssg_sm86 goes in as one of its alternative proxies (`dinput8.dll`).
