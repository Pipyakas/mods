# mods

My own game mods and the tools they share. Each mod has its own folder and
README; projects by others that the mods build on are tracked as Git submodules
under [`externals/`](externals).

| Folder | What |
|---|---|
| [`dlss_mfg_sm86/`](dlss_mfg_sm86/README.md) | Dynamic multi-frame generation on top of dlssg_sm86, for any Streamline DLSS-G game, with per-game mods (ACE COMBAT 8, Halo: Campaign Evolved, Ninja Gaiden 2 Black, ...) under [`games/`](dlss_mfg_sm86/games) |
| [`game_startup/`](game_startup/README.md) | Startup fixes loaded through a proxy DLL (Immortals of Aveum) |
| [`forza_horizon_3/`](forza_horizon_3/README.md) | Forza Horizon 3 startup-crash fix on multi-GPU machines (one-byte TargetHardwareProfiler.dll patch) |
| [`externals/dlssg_for_sm86/`](https://github.com/sdli1995/dlssg_for_sm86) | [sdli1995/dlssg_for_sm86](https://github.com/sdli1995/dlssg_for_sm86): DLSS Frame Generation on RTX 30 (SM86) and RTX 20 (SM75), which dlss_mfg_sm86 needs (`version.dll`, `alternatives\`, [INI reference](externals/dlssg_for_sm86/docs/INSTALL.en.md)) |

## dlss_mfg_sm86

dlssg_sm86 only does what the game asks for, and most games ask for 2x or have no
FG setting at all. dlss_mfg_sm86 fills those gaps: a proxy DLL (named like the main
mod's proxies — `winmm`, `dinput8`, `dbghelp`, ...) that turns the game's own FG
requests into

- **Mode 1** — NVIDIA Dynamic MFG: the runtime picks the multiplier, or
- **Mode 2** — a fixed count picked from the display refresh rate (2x up to 60 Hz,
  3x up to 90 Hz, 4x above), so streaming to a 60 Hz virtual display and playing
  locally at 120 Hz each get the right multiplier, or
- **Mode 3** (experimental) — the most generated frames for the refresh rate, with
  Unreal's dynamic resolution filling the GPU time left.

It works in any Streamline DLSS-G game with a generic drop-in install; Unreal
games also get dynamic resolution and console-variable pins. Games that need more
(an FG switch, a newer Streamline runtime, a bug fix) are grouped by engine, and
each has a copy-ready `mod/` folder with its working files, including its
`dlssg_sm86` settings (`Optimized=3`, the FG Lite `Preset=C` and the INT8
super-resolution route). Installation is manual.

## Externals

Clone with the submodules:

```powershell
git clone --recurse-submodules https://github.com/Pipyakas/mods.git
# or, in an existing clone
git submodule update --init
```

[`workflows/update-externals.yml`](.github/workflows/update-externals.yml) checks
every submodule's upstream branch hourly (and on demand from the Actions tab) and
commits the new pointer when it has moved. Locally, `git pull` then
`git submodule update` follows it; `git submodule update --remote` pulls upstream
directly. To track another project:

```powershell
git submodule add <url> externals/<name>
```

GitHub pauses scheduled workflows after 60 days without repository activity;
re-enable it from the Actions tab if that happens.
