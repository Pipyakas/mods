r"""Patch Forza Horizon 3's TargetHardwareProfiler.dll to profile one GPU.

Usage: python patch_thp.py "D:\Forza Horizon 3\TargetHardwareProfiler.dll"

Backs the original up as TargetHardwareProfiler.dll.orig, then changes
`cmp rax, 4` to `cmp rax, 1` in the adapter loop (RVA 0x33765) and refreshes
the PE checksum. Refuses any file that is not the known original.
"""
import hashlib
import shutil
import struct
import sys
from pathlib import Path

ORIGINAL_SHA256 = "17d19f723ac87d2d00f8074d5efa776d6021b6d054c19b23def640fe844d3302"
PATCHED_SHA256 = "e0f8af02bf00e011a47a1cc3a33b17afc377dd1ee9c17da3c6d56a6bf3244015"
OFFSET = 0x32B65  # RVA 0x33765: 48 83 f8 04 -> 48 83 f8 01
EXPECTED = bytes.fromhex("4883f804")


def pe_checksum(data: bytearray, field: int) -> int:
    total = 0
    padded = data + b"\0" * (len(data) % 2)
    for i in range(0, len(padded), 2):
        if i in (field, field + 2):
            continue
        total += struct.unpack_from("<H", padded, i)[0]
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (total & 0xFFFF) + len(data)


def main() -> int:
    dll = Path(sys.argv[1])
    data = bytearray(dll.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest == PATCHED_SHA256:
        print("already patched")
        return 0
    if digest != ORIGINAL_SHA256 or data[OFFSET:OFFSET + 4] != EXPECTED:
        print(f"unknown DLL (sha256 {digest}); not patching")
        return 1
    data[OFFSET + 3] = 0x01
    field = struct.unpack_from("<I", data, 0x3C)[0] + 0x58
    struct.pack_into("<I", data, field, pe_checksum(data, field))
    if hashlib.sha256(data).hexdigest() != PATCHED_SHA256:
        print("patched result does not match the tested build; not writing")
        return 1
    backup = dll.with_name(dll.name + ".orig")
    if not backup.exists():
        shutil.copy2(dll, backup)
    dll.write_bytes(data)
    print(f"patched; original saved as {backup.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
