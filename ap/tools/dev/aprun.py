"""Run the AP test build hidden with scripted input on a COPY of the player's save.
python aprun.py <run_dir> <seconds> "<SSBU_SEQ>" [KEY=VALUE ...]"""
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'B2recomp'))
BUILD = os.path.join(ROOT, 'build')
EXE = os.environ.get('APRUN_EXE') or os.path.join(BUILD, 'aptest', 'game_native.exe')
run_dir, seconds, seq = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3]
if os.path.exists(run_dir): shutil.rmtree(run_dir)
shots = os.path.join(run_dir, 'shots'); os.makedirs(shots)
shutil.copytree(os.environ.get('APRUN_SAVE') or os.path.join(BUILD, 'play', 'userdata', 'save'), os.path.join(run_dir, 'userdata', 'save'))   # APRUN_SAVE: another save folder to start from
env = {k: v for k, v in os.environ.items() if not k.startswith(('SSBU_', 'RECOMPS_'))}
env.update({
    'SSBU_ROMFS': os.path.join(ROOT, 'targets', 'romfs'), 'SSBU_USERDATA': os.path.join(run_dir, 'userdata'),
    'SSBU_SHOW': '0', 'SSBU_VOLUME': '0', 'SSBU_FOCUS_NOTIFY': '0', 'SSBU_SEQ': seq,
    'SSBU_GRAPHICS_CONFIG': os.path.join(ROOT, 'config', 'graphics.ini'), 'SSBU_CONFIG': os.path.join(ROOT, 'config', 'controllers.ini'),
    'SSBU_SHOT_STEPS': '1', 'SSBU_SHOT_DIR': shots, 'SSBU_MODS': 'none',
})
fonts = os.path.join(ROOT, 'targets', 'sysdata', 'fonts')
if os.path.isdir(fonts): env['SSBU_FONTS'] = fonts
env.update(a.split('=', 1) for a in sys.argv[4:])
with open(os.path.join(run_dir, 'stdout.txt'), 'wb') as out:
    code = subprocess.call([EXE, os.path.join(ROOT, 'targets', 'exefs', 'main.elf'), seconds], cwd=BUILD, env=env,
                           stdout=out, stderr=subprocess.STDOUT, timeout=int(seconds) + 120)
print('exit', code, 'shots', sorted(os.listdir(shots)))
