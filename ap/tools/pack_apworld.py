"""Packs ap/apworld/bayonetta2 into bayonetta2.apworld: python pack_apworld.py <output folder>"""
import os
import sys
import zipfile

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'apworld', 'bayonetta2')
out = os.path.join(sys.argv[1], 'bayonetta2.apworld')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != '__pycache__']
        for f in files:
            p = os.path.join(root, f)
            z.write(p, os.path.join('bayonetta2', os.path.relpath(p, SRC)))
print('wrote', out)
