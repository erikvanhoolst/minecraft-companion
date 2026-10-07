#!/usr/bin/env python3
"""Drive a headless eden-cli session: live console commands, .btn input scripts, screenshots.

usage:
  duo.py cmd <command...>        write one console command, wait for its reply (RUN dir from $MC_RUN)
  duo.py btn <line...>           append a .btn script (one step per argument, use quotes)
  duo.py shot [name]             take screen1/screen2 screenshots, copy them to <name>-screen{1,2}.png
  duo.py log [n]                 tail the emulator log
Environment: MC_RUN (default /tmp/mc): holds cmd.in, cmd.in.out, p.btn, p.shot, p-screen*.png, stdout.log
"""
import os, sys, time, shutil, subprocess

RUN = os.environ.get('MC_RUN', '/tmp/mc')
CMD = os.path.join(RUN, 'cmd.in')
OUT = CMD + '.out'
PREFIX = os.path.join(RUN, 'p')
LOG = os.path.join(RUN, 'stdout.log')

def atomic_write(path, text):
    tmp = path + '.tmp'
    with open(tmp, 'w') as f:
        f.write(text)
    os.replace(tmp, path)

def out_size():
    try: return os.path.getsize(OUT)
    except FileNotFoundError: return 0

EDEN_LOG = os.environ.get('EDEN_LOG', os.path.expanduser('~/.local/share/eden/log/eden_log.txt'))
LOG_ONLY = {'objfind': 'DSMod objfind', 'cluster': 'DSMod cluster', 'vfind': 'DSMod vfind',
            'findi': 'DSMod find', 'findf': 'DSMod find', 'ptrto': 'DSMod ptrto', 'mgrfind': 'DSMod mgrfind'}

def new_log_lines(path, offset, marker):
    if not os.path.exists(path):
        return []
    with open(path, 'rb') as f:
        f.seek(offset)
        return [l for l in f.read().decode(errors='replace').splitlines() if marker in l]

def cmd(args, timeout=240.0):
    before = out_size()
    log_before = os.path.getsize(EDEN_LOG) if os.path.exists(EDEN_LOG) else 0
    marker = LOG_ONLY.get(args[0])
    fire_and_forget = args[0] in {'reload', 'page', 'tap', 'drag', 'writeb', 'action', 'watch', 'snap', 'isnap'}
    atomic_write(CMD, ' '.join(args) + '\n')
    deadline = time.time() + timeout
    consumed = False
    done = False
    while time.time() < deadline:
        time.sleep(0.1)
        if not consumed and os.path.exists(CMD) and os.path.getsize(CMD) == 0:
            consumed = True
        if not consumed:
            continue
        if fire_and_forget:
            done = True
        elif marker:
            if new_log_lines(EDEN_LOG, log_before, marker):
                done = True
        elif out_size() > before:
            done = True
        if done:
            time.sleep(0.3)
            break
    if not done:
        print(f'[duo] timeout waiting for reply (consumed={consumed})', file=sys.stderr)
    text = ''
    if os.path.exists(OUT):
        with open(OUT, 'rb') as f:
            f.seek(before)
            text = f.read().decode(errors='replace')
    if marker:
        text += '\n'.join(l[l.find('DSMod'):] for l in new_log_lines(EDEN_LOG, log_before, marker)) + '\n'
    print(text, end='')

def btn(lines):
    atomic_write(PREFIX + '.btn', '\n'.join(lines) + '\n')
    print(f'[duo] {len(lines)} step(s) queued')

def shot(name=None, wait=4.0):
    for s in ('1', '2'):
        p = f'{PREFIX}-screen{s}.png'
        if os.path.exists(p): os.remove(p)
    open(PREFIX + '.shot', 'w').close()
    deadline = time.time() + wait
    while time.time() < deadline and not os.path.exists(f'{PREFIX}-screen1.png'):
        time.sleep(0.1)
    time.sleep(0.3)
    outs = []
    for s in ('1', '2'):
        p = f'{PREFIX}-screen{s}.png'
        if os.path.exists(p):
            if name:
                dst = f'{name}-screen{s}.png'; shutil.copy(p, dst); outs.append(dst)
            else: outs.append(p)
    print(' '.join(outs) if outs else '[duo] no screenshot produced')

def log(n=40):
    subprocess.run(['tail', '-n', str(n), LOG])

if __name__ == '__main__':
    a = sys.argv[1:]
    if not a: print(__doc__); sys.exit(1)
    if a[0] == 'cmd': cmd(a[1:])
    elif a[0] == 'btn': btn(a[1:])
    elif a[0] == 'shot': shot(a[1] if len(a) > 1 else None)
    elif a[0] == 'log': log(int(a[1]) if len(a) > 1 else 40)
    else: print(__doc__); sys.exit(1)
