# Game Startup

Standalone x64 startup conveniences. No DLSS, Streamline, NVIDIA, or frame
generation dependency. Supports **Immortals of Aveum**. (ACE COMBAT 8's startup skips live in its offline bootstrap, `dlss_mfg_sm86/games/ue5/ace-combat-8`.)

## Install Aveum (Windows or Proton)

Copy `games/immortals-of-aveum/mod/P3/Binaries/Win64/` to the same directory
beside `ImmortalsOfAveum-Win64-Shipping.exe`. Back up existing files first.

- `game_startup.dll`: the independent mod.
- `game_startup.ini`: Aveum profile, all three options enabled.
- `winmm.dll`: tiny standalone loader forwarding the real Windows/Wine winmm.

If **dlss_mfg_sm86** is already installed, keep its `winmm.dll` and use the
updated MFG build: it loads `game_startup.dll` separately when present. Do not
overwrite an existing MFG/other mod's winmm with the standalone loader.

For Steam on **Bazzite/Proton**, set launch options:

```sh
WINEDLLOVERRIDES="winmm=n,b" %command%
```

This loads the native proxy, which forwards to Wine's system winmm. Do **not**
install DLSS DLLs or replace graphics runtimes just for this mod. Proton/D1
compatibility must be verified on the target machine; Windows testing alone
does not establish it.

## Configuration

Generic `config/game_startup.ini` defaults everything to **off**. The Aveum
profile enables:

```ini
[Startup]
AutoOffline=1
SkipIntro=1
SkipWarnings=1
```

- `AutoOffline`: select the game's own offline mode and reach the main menu
  without the EA-error dialog/Play Offline click or a title-screen keypress.
  Startup automation stops there; never automatically Continue/New Game.
- `SkipIntro`: suppress reads of the three company/publisher/engine MP4 logos
  in `P3/Content/Movies`, without renaming or deleting game files.
- `SkipWarnings`: hide the startup seizure warning and save-icon notice while
  allowing the game's own completion flow to run immediately. **Optimizing
  Game Files stays visible**; story movies/gameplay warnings are untouched.

Restart after changes. No account/network edits and no entitlement/activation
or first-run-agreement bypass. With seizure warnings disabled, flashing-light
risks still apply; consult the game's original warning before playing.

`game_startup.log` records the startup actions. Unsupported games are unchanged.

## Build

Run `build.cmd` on Windows with Python, x64 MSVC/Windows SDK and MASM. Set
`VSINSTALLDIR` if Visual Studio is not at the script's default location.
Shared MinHook, Unreal reflection and proxy export generator sources come from
the sibling `dlss_mfg_sm86` tree; this is a source reuse, not a runtime dependency.
The build runs startup/config/path and winmm forwarding regression tests.
