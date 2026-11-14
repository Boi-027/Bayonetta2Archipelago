"""Makes the Bayonetta 2 Archipelago release from the committed sources (HEAD):

    python ap/tools/make_release.py [--out=<folder>] [--no-zip]

Writes <out>/Bayonetta2Archipelago/ (default <out> = build_release/ at the top of the project):
  Bayonetta 2 Archipelago.exe   the launcher (installs, plays, uninstalls)
  B2recomp/                     B2recomp's builder package: the tracked source plus every tool the build needs
                                (B2recomp/scripts/make_package.py does that part: llvm-mingw, SDL3, Vulkan headers, glslang,
                                Python with Pillow and lz4, licences; it refuses to finish with any game or build file in it)
  ap/                           the apworld, the tools that make the Archipelago mod from the player's own files, the launcher source
  README.md, LICENSE
and beside it Bayonetta2Archipelago-<version>.zip and bayonetta2.apworld (for the people who only generate).
Nothing of the game is in it: everything game-derived is made on the player's PC at install.
"""
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
AP = os.path.dirname(HERE)
ROOT = os.path.dirname(AP)
B2 = os.path.join(ROOT, 'B2recomp')
OUT = next((os.path.abspath(a[6:]) for a in sys.argv[1:] if a.startswith('--out=')), os.path.join(ROOT, 'build_release'))
REL = os.path.join(OUT, 'Bayonetta2Archipelago')
FORBIDDEN_EXT = ('.elf', '.nso', '.nca', '.nsp', '.xci', '.keys', '.cpk', '.npdm', '.dat', '.bmp', '.pchtxt', '.ips', '.wem', '.bnk')


def git(*args, binary=False):
    r = subprocess.run(['git', *args], cwd=ROOT, capture_output=True)
    if r.returncode:
        sys.exit('git %s: %s' % (' '.join(args), r.stderr.decode(errors='replace')))
    return r.stdout if binary else r.stdout.decode('utf-8')


def extract(tree, dst):
    """the committed files of a folder of the repository (HEAD:<tree>) into dst"""
    with tarfile.open(fileobj=io.BytesIO(git('archive', '--format=tar', 'HEAD:' + tree, binary=True))) as t:
        t.extractall(dst, filter='data')
        return len([m for m in t.getmembers() if m.isfile()])


def main():
    if git('status', '--porcelain', '--', 'ap', 'B2recomp', 'README.md').strip():
        print('note: uncommitted changes are not in the release (it is made from HEAD)')
    if os.path.exists(REL):
        shutil.rmtree(REL)
    os.makedirs(REL)

    # B2recomp's builder package, its source taken from this repository's B2recomp/ folder
    sys.path.insert(0, os.path.join(B2, 'scripts'))
    import make_package
    make_package.source_tree = lambda pkg: extract('B2recomp', pkg)
    argv = sys.argv
    sys.argv = [argv[0], '--out=' + REL, '--no-zip']
    try:
        if make_package.main():
            sys.exit('make_package.py failed')
    finally:
        sys.argv = argv

    n = extract('ap', os.path.join(REL, 'ap'))
    print('ap: %d files' % n)
    for f in ('README.md', 'LICENSE'):
        shutil.copy2(os.path.join(ROOT, f), os.path.join(REL, f))

    exe = os.path.join(REL, 'Bayonetta 2 Archipelago.exe')
    r = subprocess.run([sys.executable, os.path.join(HERE, 'build_launcher.py'), '--out=' + exe], capture_output=True, text=True)
    if r.returncode:
        sys.exit('build_launcher.py failed\n' + r.stdout[-2000:] + r.stderr[-2000:])
    print('launcher: %s' % exe)

    subprocess.run([sys.executable, os.path.join(HERE, 'pack_apworld.py'), OUT], check=True)
    with open(os.path.join(AP, 'apworld', 'bayonetta2', 'archipelago.json')) as f:
        version = json.load(f)['world_version']

    bad = []                                            # nothing of the game, anywhere in it
    for dp, dn, fn in os.walk(REL):
        for name in fn:
            rel = os.path.relpath(os.path.join(dp, name), REL).replace(os.sep, '/')
            if name.lower().endswith(FORBIDDEN_EXT) or rel.startswith(('B2recomp/build/', 'B2recomp/targets/', 'B2recomp/mods/', 'ap/research/', 'ap/screenshots/')):
                bad.append(rel)
    if bad:
        sys.exit('REFUSED: the release would contain %d file(s) that must not be shipped:\n  %s' % (len(bad), '\n  '.join(bad[:30])))
    print('checked: no game or build files')

    if '--no-zip' not in sys.argv:
        z = os.path.join(OUT, 'Bayonetta2Archipelago-%s.zip' % version)
        if os.path.exists(z):
            os.remove(z)
        with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for dp, dn, fn in os.walk(REL):
                for name in fn:
                    p = os.path.join(dp, name)
                    zf.write(p, os.path.relpath(p, OUT))
        print('zip: %s, %.0f MB' % (z, os.path.getsize(z) / 2 ** 20))
    return 0


if __name__ == '__main__':
    sys.exit(main())
