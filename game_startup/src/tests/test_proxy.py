import ctypes
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = root.parent / 'dlss_mfg_sm86' / 'src' / 'generate_proxies.py'
spec = importlib.util.spec_from_file_location('proxy_metadata', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
proxy_path = root / 'build' / 'proxies' / 'winmm' / 'winmm.dll'
expected = module.exports((module.SYSTEM / 'winmm.dll').read_bytes())
actual = dict(module.exports(proxy_path.read_bytes()))
kernel = ctypes.WinDLL('kernel32')
kernel.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
kernel.GetProcAddress.restype = ctypes.c_void_p
proxy = ctypes.WinDLL(str(proxy_path))
for ordinal, names in expected:
    assert ordinal in actual and set(names).issubset(actual[ordinal]), (ordinal, names)
    assert kernel.GetProcAddress(proxy._handle, ordinal), ordinal
original = ctypes.WinDLL(str(module.SYSTEM / 'winmm.dll'))
proxy.timeGetTime.restype = original.timeGetTime.restype = ctypes.c_ulong
assert abs(proxy.timeGetTime() - original.timeGetTime()) < 1000
assert proxy.waveOutGetNumDevs() == original.waveOutGetNumDevs()
print('PASS:', len(expected), 'winmm named/ordinal exports; multimedia/time APIs forwarded')
