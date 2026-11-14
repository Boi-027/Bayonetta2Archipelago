"""Packs ap/apworld/bayonetta2 into bayonetta2.apworld: python pack_apworld.py <output folder>

The zip is the same byte for byte for the same files (sorted names, fixed dates), so the Archipelago launcher can tell
an installed .apworld that is out of date by comparing it with a fresh one."""
import os
import sys
import zipfile

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'apworld', 'bayonetta2')
out = os.path.join(sys.argv[1], 'bayonetta2.apworld')
files = []
for root, dirs, names in os.walk(SRC):
    dirs[:] = sorted(d for d in dirs if d != '__pycache__')
    files += [os.path.join(root, f) for f in names if not f.endswith('.pyc')]
tmp = out + '.tmp'
with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(files, key=lambda p: os.path.relpath(p, SRC).replace(os.sep, '/')):
        info = zipfile.ZipInfo('bayonetta2/' + os.path.relpath(p, SRC).replace(os.sep, '/'), (2020, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        with open(p, 'rb') as f:
            z.writestr(info, f.read())
os.replace(tmp, out)
print('wrote', out)
