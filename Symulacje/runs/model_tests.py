"""Builds and runs Model_test_*.asc: one small test schematic per new model.
Usage (from Symulacje/):  python runs/model_tests.py
"""
import sys, os
sys.path.insert(0, 'runs')
from lt import *

N = chr(92) + 'n'


class Asc:
    def __init__(self, w=1400, h=700):
        self.L = ['Version 4', 'SHEET 1 %d %d' % (w, h)]

    def flag(self, x, y, net):
        self.L.append('FLAG %d %d %s' % (x, y, net))

    def sym(self, kind, x, y, name, value, pins, nets, extra=()):
        self.L.append('SYMBOL %s %d %d R0' % (kind, x, y))
        self.L.append('SYMATTR InstName ' + name)
        if value is not None:
            self.L.append('SYMATTR Value ' + value)
        for e in extra:
            self.L.append(e)
        for (px, py), net in zip(pins, nets):
            self.flag(x + px, y + py, net)

    def V(self, x, y, name, value, p, n='0'):
        self.sym('voltage', x, y, name, value, [(0, 16), (0, 96)], [p, n])

    def R(self, x, y, name, value, a, b):
        self.sym('res', x, y, name, value, [(16, 16), (16, 96)], [a, b])

    def text(self, x, y, lines):
        self.L.append('TEXT %d %d Left 2 !%s' % (x, y, N.join(lines)))

    def save(self, fname):
        wr(fname, '\r\n'.join(self.L) + '\r\n')


TMUX_PINS = [(-48, 112), (-64, -64), (48, 112), (-64, 0), (16, 112), (0, -96), (-64, 32), (64, -16), (-64, -32), (-16, 112)]

# ------------------------------------------------------------- TMUX1104
a = Asc()
# D held at 1.625 V, each S pin through 1 k to 2.625 V -> 1 mA through the selected switch
a.sym('TMUX1104', 700, 300, 'U1', None, TMUX_PINS,
      ['a0', 's1', '0', 's3', 'en', 'vdd', 's4', 'd', 's2', 'a1'])
a.V(100, 100, 'VDD', '5', 'vdd')
a.V(200, 100, 'VD', '1.625', 'd')
a.V(300, 100, 'VS', '2.625', 'vs')
for i, s in enumerate(('s1', 's2', 's3', 's4')):
    a.R(400 + 80 * i, 450, 'R%d' % (i + 1), '1k', 'vs', s)
a.V(100, 400, 'VA0', 'PULSE(0 3.3 1u 2n 2n 1u 2u)', 'a0')
a.V(200, 400, 'VA1', 'PULSE(0 3.3 2u 2n 2n 2u 4u)', 'a1')
a.V(300, 400, 'VEN', 'PULSE(3.3 0 4.5u 2n 2n 1u 10u)', 'en')
a.text(100, 650, ['.tran 0 5u 0 1n',
                  '.meas TRAN ron1 FIND (V(s1)-V(d))/I(R1) AT 0.8u',
                  '.meas TRAN ron2 FIND (V(s2)-V(d))/I(R2) AT 1.8u',
                  '.meas TRAN ron3 FIND (V(s3)-V(d))/I(R3) AT 2.8u',
                  '.meas TRAN ron4 FIND (V(s4)-V(d))/I(R4) AT 3.8u',
                  '.meas TRAN ioff_s1_at2u FIND I(R1) AT 1.8u',
                  '.meas TRAN ioff_all_en0 FIND I(R1)+I(R2)+I(R3)+I(R4) AT 5u',
                  '.meas TRAN t_s1_off WHEN I(R1)=0.5m FALL=1',
                  '.meas TRAN t_s2_on WHEN I(R2)=0.5m RISE=1',
                  '.meas TRAN bbm PARAM (t_s2_on-t_s1_off)'])
a.save('Model_test_TMUX1104.asc')

# ------------------------------------------------------------- AQY212S
a = Asc()
# GPIO 3.3 V -> 390 ohm -> LED; output switches 10 mA from 1 V through 100 ohm
a.sym('AQY212S', 600, 300, 'K1', None, [(-48, -16), (-48, 16), (48, -16), (48, 16)], ['led', '0', 'o1', '0'])
a.V(100, 100, 'VGPIO', 'PULSE(0 3.3 1m 1u 1u 3m 10m)', 'gpio')
a.R(200, 100, 'R1', '390', 'gpio', 'led')
a.V(300, 100, 'VT', '1', 'vt')
a.R(400, 100, 'RL', '100', 'vt', 'o1')
a.text(100, 450, ['.tran 0 6m 0 1u',
                  '.meas TRAN i_led FIND I(R1) AT 3m',
                  '.meas TRAN v_led FIND V(led) AT 3m',
                  '.meas TRAN ron FIND V(o1)/I(RL) AT 3m',
                  '.meas TRAN roff FIND V(o1)/I(RL) AT 0.5m',
                  '.meas TRAN t_on WHEN V(o1)=0.5 FALL=1',
                  '.meas TRAN t_off WHEN V(o1)=0.5 RISE=1',
                  '.meas TRAN ton PARAM t_on-1m',
                  '.meas TRAN toff PARAM t_off-4m'])
a.save('Model_test_AQY212S.asc')

# ------------------------------------------------------------- ESD2CAN36-Q1
a = Asc()
a.sym('ESD2CAN36-Q1', 600, 300, 'D1', None, [(-48, -16), (-48, 16), (0, 48)], ['io1', '0', '0'])
a.V(100, 100, 'VIO', '0', 'io1')
a.text(100, 450, ['.dc VIO -45 45 0.01',
                  '.meas DC i_at_24V FIND -I(VIO) AT 24',
                  '.meas DC i_at_32V FIND -I(VIO) AT 32',
                  '.meas DC i_at_36V FIND -I(VIO) AT 36',
                  '.meas DC v_at_10mA WHEN -I(VIO)=10m',
                  '.meas DC v_at_m10mA WHEN -I(VIO)=-10m'])
a.save('Model_test_ESD2CAN36.asc')

# same circuit, AC: capacitance from the current at 1 MHz
a.L = [l for l in a.L if not l.startswith('TEXT') and not l.startswith('SYMATTR Value 0')]
i = a.L.index('SYMATTR InstName VIO')
a.L.insert(i + 1, 'SYMATTR Value DC {vb} AC 1')
for vb in (0, 24, 32):
    a2 = Asc(); a2.L = list(a.L)
    a2.text(100, 450, ['.ac list 1e6', '.param vb=%d' % vb,
                       '.meas AC c_pF_at_%dV FIND Im(I(VIO))/(2*pi*1e6)*1e12 AT 1e6' % vb])
    a2.save('Model_test_ESD2CAN36_C%d.asc' % vb)

if __name__ == '__main__':
    for f in ('Model_test_TMUX1104.asc', 'Model_test_AQY212S.asc', 'Model_test_ESD2CAN36.asc',
              'Model_test_ESD2CAN36_C0.asc', 'Model_test_ESD2CAN36_C24.asc', 'Model_test_ESD2CAN36_C32.asc'):
        log = run(f)
        print('=====', f)
        print('\n'.join(l for l in log.split('\n') if ('=' in l and ':' in l) or 'rror' in l or 'Fatal' in l or 'ignored' in l.lower()))
