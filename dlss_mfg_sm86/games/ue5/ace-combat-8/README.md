# ACE COMBAT 8

AC8 has no in-game frame generation setting, its DLSS quality dropdown is off by
one, and its renderer never enables UE dynamic resolution. The bootstrap fixes the
last two. The core DLL (`dinput8.dll`) provides MFG and adds a **Frame Generation**
row (Off / 2x / 3x / 4x / Auto) to Settings > Graphics, applied with the game's
"Apply them?" confirmation; with `Mode=0` that row decides. The row is the
consoles' unused graphics-mode option: the game shows and saves its raw value
(`LiveSystemSaveGame.GraphicsData.GraphicsMode`), the INI's choice is written
there while Settings is closed, and the value there once Settings has closed and
"Apply them?" has been answered is applied. Not in the mission pause menu (its
list has no free row, and the main menu's System pages do not run there).

## Install (manual)

Paths are relative to the game folder, e.g. `D:\steamapps\common\ACE COMBAT 8`.
Back up every file you replace first.

| Copy | To | What |
|---|---|---|
| `mod\start_protected_game.exe` | `start_protected_game.exe` | bootstrap — replaces the game's launcher stub (carries the offline-mode module) |
| `mod\ac8_bootstrap.ini` | `ac8_bootstrap.ini` | bootstrap settings |
| `mod\Game\Binaries\Win64\dinput8.dll` | `Game\Binaries\Win64\` | dlss_mfg_sm86 proxy |
| `mod\Game\Binaries\Win64\dlss_mfg_sm86.ini` | `Game\Binaries\Win64\` | MFG mode |
| `version.dll`, `dlssg_sm86.ini` and the `dlssg_sm86\` folder from [dlssg_for_sm86](../../../../externals/dlssg_for_sm86) | `Game\Binaries\Win64\` | dlssg_sm86 0.4.0 itself (`dlssg_sm86\` carries the runtime, backend and kernel packs; `sr\` adds the INT8 super-resolution); the bootstrap writes its `dlssg_sm86.ini` from `[SM86.*]` |

Requirements:

- **Streamline.** AC8 ships 2.7.30 in
  `Game\Plugins\Marketplace\nvidia\StreamlineCore\Binaries\ThirdParty\Win64`: enough
  for Mode 2. Mode 1 (Dynamic) needs the 2.14.1 runtime there
  ([UE5 notes](../README.md)).
- **Proxy name.** `dinput8.dll` is a static import of `AceCombat8.exe`. Don't use
  `winmm.dll` (taken by the Artifact loader on this setup) or `version.dll`
  (dlssg_sm86).

## What the bootstrap does

`start_protected_game.exe` reads `ac8_bootstrap.ini`, then starts
`Game\Binaries\Win64\AceCombat8.exe` suspended:

- `FixDLSSPresets` — patches the 7 `PerfQualityValue` call sites in memory so every
  DLSS dropdown entry maps to its real render scale. A game update that moves the
  code disables the patch instead of corrupting it.
- `[ConsoleVariables]` → Engine.ini: `r.Streamline.DLSSG.Enable` (keep it at 1: the
  in-game row's Off works through the core DLL), the fallback `FramesToGenerate`, Reflex mode, any other CVar.
  `key=@remove` deletes a key. `RemoveScreenPercentage` drops fixed render-scale
  overrides so the DLSS preset controls it.
- `[SM86.*]` → `Game\Binaries\Win64\dlssg_sm86.ini` (section name without the prefix).
- `LockEngineIni` — keeps Engine.ini read-only while the game runs.
- `EnableDynamicResolution` — AC8's D3D12 renderer never sets
  `GRHISupportsDynamicResolution`, so UE treats dynamic resolution as unsupported and
  `r.DynamicRes.*` does nothing (the state is enabled and unpaused, but the viewport
  never applies it). The bootstrap finds the flag through
  `FDefaultDynamicResolutionState::IsSupported`, which returns it — verifying that
  function's bytes first, so a game update skips the fix — and sets it once the game
  runs. A data write, not a code patch.
- Offline mode (the bootstrap starts the game without Easy Anti-Cheat). Two
  conveniences need code inside the game: a module (`offline/ac8_offline.cpp`,
  built as a DLL) is embedded in the exe, which maps it into the suspended game's
  memory itself (no file is written or loaded by name) and starts it with an
  APC on the game's main thread, before the game's own code. The keys reach it
  as environment variables (`AC8_BOOTSTRAP_SKIP_INTRO`,
  `AC8_BOOTSTRAP_HIDE_ONLINE_MENU`); its log is
  `Game\Binaries\Win64\ac8_offline.log`.
  - `SkipIntro` — the logo/legal notice screens and the title screen ("PRESS THE
    ENTER KEY") are skipped and the main menu appears without its fade-in. The
    game's own sign-in step opens where the title would, so it signs in and loads
    the save as after that key press. Data edits made before the game uses them:
    the notice list, the title's menu-table entry, three animations' ranges. The
    shader preparation screen and the ~4 s sign-in background stay.
  - `HideOnlineMenu` — ACE COMBAT ONLINE and ACE COMBAT STORE (greyed out for
    good offline) are hidden by the game itself: just before the main menu's list
    is created (its `OnInitialized`, through a ProcessEvent hook), their rows in
    `DT_LiveMenuMainTopCommands` get a feature the game never unlocks with
    `InvalidAppearance=Invisible` — how it hides CAMPAIGN TRAINING until unlocked
    — and their `StringArgs` ("Online", "Store", which grey a row out whatever
    else it says) are emptied. The game builds a four-row menu with its own
    keyboard, gamepad and mouse navigation and descriptions.
- `[Debug] UE4SS=1` — loads UE4SS from `Game\Binaries\Win64\ue4ss\UE4SS.dll`
  (installed separately, without its `dwmapi.dll` proxy) for inspecting the game
  during mod work. Off by default.
- `[Debug] AgentControl=1` — lets a tool drive the game while it runs in the
  background, through `\\.\pipe\ac8_agent` (`offline/agent_control.cpp`: keys,
  virtual mouse, virtual XInput pad). The game is kept believing it is in front,
  so it takes that input and keeps frame generation on; the menus act on the
  pad, not on keyboard messages. Off by default.

Besides the preset patch, that module and (when asked) UE4SS it injects no code
and has no frame generation logic; `dlss_mfg_sm86.ini` decides the mode. With
Mode 0, the in-game row decides.

## Mode 3 with dynamic resolution

Set DLSS to **Performance or higher** in game. DLSS reports the range dynamic
resolution may use per mode: Performance, Balanced and Quality allow 50–100%;
Ultra Performance is created at exactly 33% and reports 33–33%, so resolution
can't move there. The shipped INI uses Mode 1 (Dynamic MFG); for Mode 3 it is set
up with `DynamicResolution=1` and a 50–95% range. The maximum stays
well below 100%: with dynamic resolution on, the DLSS plugin picks its mode from the
current resolution, so starting at 100% selects DLAA, whose 99–100% range then pins
resolution at 100% for good. The 50% minimum is
real: lowering the minimum DLSS reports (tested by patching it to 33%) froze the
image as soon as resolution went below 50%; DLSS stops producing frames for inputs
outside its mode's range. Measured (RTX 3070 Laptop, 1440p,
120 Hz): 4x holds 120 fps from a 30 fps base with resolution at 80% in normal
flight (GPU ~29 ms of the 31.7 ms goal; the game's CPU threads ~7 ms per frame);
in the heaviest scenes the 50% floor needs 33.5–35 ms, ~107–114 fps.

## Check after launch

- `Game\Binaries\Win64\dlss_mfg_sm86.log`: `Streamline game, engine=Unreal`,
  `Start Mode=1 ...`, then `Hook ... MH_OK` for both functions and `dynamicOverride=1`
  once frame generation starts (verified live: Dynamic ran mostly 4x; `override=1` in
  Mode 2). In Mode 3:
  `Mode3 found r.DynamicRes...` and `resolution=driven`.
- `Game\Binaries\Win64\dlssg_sm86\logs\backend_<pid>.jsonl`: `"multi_frame_count":3`
  for 4x.

`tools\verify_live_presets.py <pid>` checks that all seven preset patches are
applied in the running game. [VERIFICATION.md](VERIFICATION.md) has the earlier
test history (Mode 1 with the single-mode build on Streamline 2.14.1).
