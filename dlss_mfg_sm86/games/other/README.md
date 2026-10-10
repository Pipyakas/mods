# Other engines

Games on in-house engines that integrate NVIDIA's Streamline SDK directly, with no
other game here sharing the engine. The core DLL needs nothing engine-specific: it
logs `Streamline game, engine=other`, so the Unreal console-variable features (Mode 3
dynamic resolution, `[ConsoleVariables]`) are off.

The Streamline version table in the [UE5 notes](../ue5/README.md) applies: below 2.7,
replace the game's Streamline runtime DLLs with the SDK 2.14.1 release ones
(`bin\x64`; the full SDK zip also has `sl.dlss.dll`). Leave `nvngx_dlss.dll` (the
DLSS upscaling model) as shipped, and back up the originals first.

## Games

- [Final Fantasy XVI](final-fantasy-xvi/README.md)
