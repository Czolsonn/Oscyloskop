"""Builds the two simulation copies of the master 'Analog Front End.asc' (4-channel design, 2026-10-19)."""
import sys, re
sys.path.insert(0, 'runs')
from lt import *
N = chr(92) + 'n'
d0 = rd('Analog Front End.asc')
if 'SYMATTR InstName C5\nSYMATTR Value 10p' in d0:
    d0 = d0.replace('SYMATTR InstName C5\nSYMATTR Value 10p', 'SYMATTR InstName C5\nSYMATTR Value 12.5p')
    wr('Analog Front End.asc', d0)


def strip_meas(d):
    return '\n'.join(l for l in d.split('\n') if not l.startswith('TEXT') or '!.meas' not in l)


def text(d, y, s):
    return d.rstrip('\n') + '\nTEXT -304 %d Left 2 %s\n' % (y, s)


# ---------------------------------------------------------------- 1: frequency response
d = strip_meas(d0)
d = d.replace('!.ac dec 100 10 50Meg', '!.ac dec 50 0.1 50Meg')
d = d.replace('!.param gsel=0 kAC=0', '!.param gsel=0 kAC=0' + N + '.step param gsel list 0 1 2 3' + N + '.step param kAC list 0 1')
d = text(d, 600, '!' + N.join(['.meas AC g1k FIND mag(V(v_out)) AT 1k',
                               '.meas AC err100k FIND (mag(V(v_out))/g1k-1)*100 AT 100k',
                               '.meas AC err1M FIND (mag(V(v_out))/g1k-1)*100 AT 1Meg',
                               '.meas AC f_hi WHEN mag(V(v_out))=g1k/sqrt(2) FALL=last',
                               '.meas AC f_lo WHEN mag(V(v_out))=g1k/sqrt(2) RISE=1']))
d = text(d, 840, ';SIM 1 - FREQUENCY RESPONSE BNC -> ADC pin, 4 gains x DC/AC coupling (8 curves). Plot V(v_out).' + N +
         'g1k = gain at 1 kHz (input -> ADC), err = deviation from 1 kHz in %, f_hi / f_lo = upper / lower -3 dB (f_lo only in AC mode).')
wr('Analog Front End_v2_1_AC.asc', d)

# ---------------------------------------------------------------- 2: signals, AC/DC, clipping
VIN = ('if(sig==1, 24*sin(2*pi*1k*time), if(sig==2, 24*tanh(30*sin(2*pi*10k*time)), if(sig==3, 2*sin(2*pi*1k*time),'
       ' if(sig<=5, 24+sin(2*pi*10k*time), if(sig==6, 40*sin(2*pi*1k*time), 10*sin(2*pi*1k*time))))))')
d = strip_meas(d0)
# input source -> behavioural source selected by sig
d = d.replace('SYMBOL voltage 0 160 R0', 'SYMBOL bv 0 160 R0')
d = re.sub(r'SYMATTR InstName V1\nSYMATTR Value SINE\(0 40 50k\)\nSYMATTR Value2 AC 1\nSYMATTR SpiceLine Rser=50 Cpar=100p',
           lambda m: 'SYMATTR InstName B1\nSYMATTR Value V=' + VIN, d)
assert 'V=if(sig' in d
d = d.replace('!.ac dec 100 10 50Meg', '!.tran 0 {tstop} 0 {tstop/4000}')
d = d.replace('!.param gsel=0 kAC=0', '!' + N.join([
    '.step param sig list 1 2 3 4 5 6 7',
    '.param gsel=table(sig, 1,0, 2,0, 3,3, 4,0, 5,3, 6,0, 7,3)',
    '.param kAC=table(sig, 1,0, 2,0, 3,0, 4,0, 5,1, 6,0, 7,0)',
    '.param tstop=table(sig, 1,3m, 2,300u, 3,3m, 4,300u, 5,300u, 6,3m, 7,3m)',
    ]))
d = text(d, 600, '!' + N.join(['.meas TRAN vo_max MAX V(v_out)', '.meas TRAN vo_min MIN V(v_out)',
                               '.meas TRAN vo_avg AVG V(v_out)', '.meas TRAN vo_pp PARAM vo_max-vo_min',
                               '.meas TRAN in_pp PP V(n001)', '.meas TRAN bufo_max MAX V(bufo)', '.meas TRAN bufo_min MIN V(bufo)']))
d = text(d, 900, ';SIM 2 - INPUT SIGNALS, AC/DC MODE, CLIPPING (transient). Plot V(n001) = BNC input and V(v_out) = ADC pin (0...3.25 V).' + N +
         '  sig=1  sine 1 kHz, 24 V amplitude              G1  DC   (main use: +-24 V industrial signal)' + N +
         '  sig=2  square 10 kHz, +-24 V, about 1 us edges  G1  DC   (divider compensation: flat top = OK)' + N +
         '  sig=3  sine 1 kHz, 2 V amplitude               G10 DC   (small signal, highest gain)' + N +
         '  sig=4  24 V DC + 1 V sine 10 kHz              G1  DC   (ripple on a supply: DC mode keeps the 24 V)' + N +
         '  sig=5  the same                                G10 AC   (AC mode removes the 24 V, so G10 shows the ripple)' + N +
         '  sig=6  sine 1 kHz, 40 V amplitude              G1  DC   (over range: must clip at 0 / 3.3 V and stay in phase)' + N +
         '  sig=7  sine 1 kHz, 10 V amplitude              G10 DC   (over range x3 at G10: clipping and recovery)')
wr('Analog Front End_v2_2_signals.asc', d)
print('written')
