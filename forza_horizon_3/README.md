# Forza Horizon 3

Fixes the startup crash of Forza Horizon 3 (PC, UWP package
`Microsoft.OpusPG_8wekyb3d8bbwe`) on machines that expose more than one GPU,
such as a laptop with an NVIDIA dGPU, an AMD/Intel iGPU and a virtual display
(SudoMaker Virtual Display Adapter for Sunshine/Apollo streaming).

## Cause

At startup the game's `TargetHardwareProfiler.dll` benchmarks every DXGI adapter,
up to four, to pick graphics presets. With several adapters (here the RTX 3070,
the AMD iGPU, and the iGPU listed a second time) the extra benchmarks end with
resources the profiler never null-checks, so it crashes. The crash site moves
with each run, so patching individual null checks does not hold; D3D12 device
and resource creation itself never fails.

## Fix

A one-byte patch limits the profiler to the first adapter: `cmp rax, 4` becomes
`cmp rax, 1` at RVA `0x33765` (file offset `0x32b65`), plus a refreshed PE
checksum. No adapter has to be disabled, so streaming setups keep working.

```powershell
python patch_thp.py "D:\Forza Horizon 3\TargetHardwareProfiler.dll"
```

The script only patches the known original
(SHA-256 `17d19f72…44d3302`), produces the tested build
(`e0f8af02…3244015`), and keeps the original as
`TargetHardwareProfiler.dll.orig`. To undo, copy that file back.

Tested 2026-10-12 on the Zephyrus G15 (DODI repack): the profiler finishes in
about 8 seconds and the game runs past startup with no crash.

## Notes

- The profiler now measures whichever GPU DXGI lists first. Set
  `forza_x64_release_final.exe` to **High performance** in Settings → Display →
  Graphics so that is the dGPU, otherwise it may pick presets for the iGPU.
- Forza Horizon 4 ships a similar `TargetHardwareProfiler.dll` (reported memory
  leak); not yet examined.
