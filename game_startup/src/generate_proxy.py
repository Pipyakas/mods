"""Reuse the proven export generator, emitting only our independent winmm proxy."""
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root.parent / 'dlss_mfg_sm86' / 'src' / 'generate_proxies.py'
spec = importlib.util.spec_from_file_location('proxy_metadata', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.KINDS = ('winmm',)
module.PROXIES = root / 'build' / 'proxies'
module.generate()
entries = module.exports((module.SYSTEM / 'winmm.dll').read_bytes())
names = [f'"{aliases[0]}"' if aliases else 'nullptr' for _, aliases in entries]
header = module.PROXIES / 'winmm' / 'proxy_exports.h'
with header.open('a') as out:
    out.write('static const char* const kProxyNames[] = {' + ','.join(names) + '};\n')
