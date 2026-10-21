"""Thesis figures: one small LTspice file per figure (Fig*.asc + Fig*.plt), built from the master
'Analog Front End.asc', simulated, then photographed from the LTspice waveform window (runs/ltshot.ps1).
Usage (from Symulacje/):  python runs/afe_figs.py [build|run|shot|all]
"""
import sys, os, re, shutil, subprocess
sys.path.insert(0, 'runs')
from lt import *

N = chr(92) + 'n'
OUT = os.path.join(SIM, "plot's", 'AFE_v2', 'LTspice_figures')
INI = r'C:\Users\polus\AppData\Roaming\LTspice.ini'

# name, input expression, gsel, kAC, analysis, plot ranges
#   tran plots: (t_start, t_step, t_stop, t_prefix, in_min, in_step, in_max)
F = [
    ('Fig01_AC_response', None, None, None, 'ac', None),
    ('Fig02_sine_24V_G1', '24*sin(2*pi*1k*time)', 0, 0, '.tran 0 3m 0 0.5u', (0, 0.5e-3, 3e-3, 'm', -30, 10, 30)),
    ('Fig03_square_24V_G1', '24*tanh(30*sin(2*pi*10k*time))', 0, 0, '.tran 0 300u 0 50n', (0, 50e-6, 300e-6, '\xb5', -30, 10, 30)),
    ('Fig04_sine_2V_G10', '2*sin(2*pi*1k*time)', 3, 0, '.tran 0 3m 0 0.5u', (0, 0.5e-3, 3e-3, 'm', -3, 1, 3)),
    ('Fig05_ripple_DC_G1_vs_AC_G10', '24+sin(2*pi*10k*time)', '{3*k}', '{k}', '.tran 0 300u 0 50n', (0, 50e-6, 300e-6, '\xb5', 22.5, 0.5, 25.5)),
    ('Fig06_overrange_40V_G1', '40*sin(2*pi*1k*time)', 0, 0, '.tran 0 3m 0 0.5u', (0, 0.5e-3, 3e-3, 'm', -50, 10, 50)),
    ('Fig07_sine_1MHz_32V_G1', '32*sin(2*pi*1Meg*time)', 0, 0, '.tran 0 12u 0 1n', (9e-6, 0.5e-6, 12e-6, '\xb5', -40, 10, 40)),
    ('Fig08_square_1MHz_32V_G1', '32*tanh(100*sin(2*pi*1Meg*time))', 0, 0, '.tran 0 12u 0 1n', (9e-6, 0.5e-6, 12e-6, '\xb5', -40, 10, 40)),
    ('Fig09_edge_100kHz_32V_G1', '32*tanh(100*sin(2*pi*100k*time))', 0, 0, '.tran 0 30u 0 1n', (24.6e-6, 0.2e-6, 26.0e-6, '\xb5', -40, 10, 40)),
]


def base():
    d = rd('Analog Front End.asc')
    d = '\n'.join(l for l in d.split('\n') if not (l.startswith('TEXT') and ('!.meas' in l or l.split(' ', 5)[-1].startswith(';'))))
    return d


def build_one(name, vin, gsel, kac, ana):
    d = base()
    if ana == 'ac':
        d = d.replace('!.ac dec 100 10 50Meg', '!.ac dec 50 0.1 50Meg')
        d = d.replace('!.param gsel=0 kAC=0', '!.param gsel=0 kAC=0' + N + '.step param gsel list 0 1 2 3' + N + '.step param kAC list 0 1')
    else:
        d = d.replace('SYMBOL voltage 0 160 R0', 'SYMBOL bv 0 160 R0')
        d = re.sub(r'SYMATTR InstName V1\nSYMATTR Value SINE\(0 40 50k\)\nSYMATTR Value2 AC 1\nSYMATTR SpiceLine Rser=50 Cpar=100p',
                   lambda m: 'SYMATTR InstName B1\nSYMATTR Value V=' + vin, d)
        assert 'InstName B1' in d
        d = d.replace('!.ac dec 100 10 50Meg', '!' + ana)
        p = '.param gsel=%s kAC=%s' % (gsel, kac)
        if '{k}' in str(kac):
            p += N + '.step param k list 0 1'
        d = d.replace('!.param gsel=0 kAC=0', '!' + p)
        if 'overrange' in name:
            # Gear needs > 10 min with saturated op-amps; trapezoidal gives a clean result for this 1 kHz case
            d = d.replace(' method=gear', '')
    wr(name + '.asc', d)


def plt_one(name, ana, rng):
    if ana == 'ac':
        s = ['[AC Analysis]', '{', '   Npanes: 1', '   {',
             '      traces: 1 {524290,0,"V(v_out)"}',
             "      X: ('M',0,0.1,0,5e+07)",
             "      Y[0]: (' ',0,0.001,10,1)",
             "      Y[1]: (' ',0,-360,30,0)",
             '      Log: 1 2 0', '      PltMag: 1', '      PltPhi: 0 0', '   }', '}']
    else:
        t0, ts, t1, pre, a, b, c = rng
        X = "      X: ('%s',0,%g,%g,%g)" % (pre, t0, ts, t1)
        s = ['[Transient Analysis]', '{', '   Npanes: 2', '   Active Pane: 1', '   {',
             '      traces: 1 {524290,0,"V(v_out)"}', X,
             "      Y[0]: (' ',0,-0.5,0.5,3.5)",
             "      Y[1]: ('m',0,1e+308,0.01,-1e+308)",
             "      Volts: (' ',0,0,0,-0.5,0.5,3.5)",
             '      Log: 0 0 0', '   },', '   {',
             '      traces: 1 {524291,0,"V(in)"}', X,
             "      Y[0]: (' ',0,%g,%g,%g)" % (a, b, c),
             "      Y[1]: ('m',0,1e+308,0.01,-1e+308)",
             "      Volts: (' ',0,0,0,%g,%g,%g)" % (a, b, c),
             '      Log: 0 0 0', '   }', '}']
    open(os.path.join(SIM, name + '.plt'), 'w', encoding='latin1', newline='\r\n').write('\n'.join(s) + '\n')


def shot(name):
    os.makedirs(OUT, exist_ok=True)
    png = os.path.join(OUT, name + '.png')
    subprocess.run(['powershell', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(SIM, 'runs', 'ltshot.ps1'),
                    '-raw', os.path.join(SIM, name + '.raw'), '-png', png], timeout=120)
    from PIL import Image
    im = Image.open(png)
    w, h = im.size
    im.crop((12, 140, w - 12, h - 52)).save(png)   # drop title bar, menu, toolbar and status line
    return png


def white_ini(on):
    bak = INI + '.bak_claude'
    if on:
        shutil.copy(INI, bak)
        t = open(INI, encoding='latin1').read()
        for k, v in (('WaveColor0', 16777215), ('WaveColor1', 0), ('WaveColor2', 12582912),
                     ('WaveColor3', 255), ('Grid', 12632256), ('DataPenWidth', 2),
                     ('grid', 'true'), ('InActiveAxis', 0), ('WaveformViewerFontSize', 14)):
            t = re.sub(r'^%s=.*$' % k, '%s=%s' % (k, v), t, flags=re.M)
        open(INI, 'w', encoding='latin1', newline='').write(t)
    else:
        shutil.copy(bak, INI)
        os.remove(bak)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    for name, vin, g, k, ana, rng in F:
        if what in ('build', 'all'):
            build_one(name, vin, g, k, ana)
            plt_one(name, ana, rng)
        if what in ('run', 'all'):
            L = run(name + '.asc', timeout=1800)
            print(name, 'ok' if os.path.exists(os.path.join(SIM, name + '.raw')) else 'NO RAW', [l for l in L.split('\n') if 'rror' in l][:3])
    if what in ('shot', 'all'):
        white_ini(True)
        try:
            for name, *_ in F:
                print(shot(name))
        finally:
            white_ini(False)
