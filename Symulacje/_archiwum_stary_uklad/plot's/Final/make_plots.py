"""Redraws the figures of the final simulation from the CSV files in data/.
Usage:  python make_plots.py
"""
import os, json, csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
DAT = os.path.join(HERE, 'data')
VMID, VDDA, LSB = 1.65, 3.3, 3.3 / 4096


def load(name):
    with open(os.path.join(DAT, name)) as f:
        hdr = f.readline().strip().split(',')
    a = np.loadtxt(os.path.join(DAT, name), delimiter=',', skiprows=1)
    return {h: a[:, i] for i, h in enumerate(hdr)}


# ---------------------------------------------------------------- 1: signals
SIG = {1: ('sine 1 kHz, 2 V amplitude', 'G = 20', 'G = 20', 1e3, 'ms'),
       2: ('sine 1 MHz, 40 V amplitude', 'G = 1', 'G = 1', 1e6, 'µs'),
       3: ('square 1 kHz, ±40 V', 'G = 1', 'G = 1', 1e3, 'ms'),
       4: ('logic square 100 kHz, 0…5 V', 'G = 10', 'G = 10', 1e6, 'µs'),
       5: ('12 V DC + 1 V sine 10 kHz', 'G = 3', 'G = 20', 1e6, 'µs')}


def panel(ax, d, kac, title, tscale, unit, first):
    t = d['t_s'] * tscale
    ax.axhspan(0, VDDA, color='#e8f2e8', zorder=0)
    ax.axhline(VMID, color='0.6', lw=0.8, ls=':')
    ax.plot(t, d['v_out_kAC%d' % kac], color='C0', lw=1.3, label='V(v_out), ADC pin')
    ax.set_ylim(-0.2, 3.5)
    ax.set_ylabel('ADC pin [V]', color='C0')
    ax.set_xlabel('time [%s]' % unit)
    ax.set_title(title, fontsize=9)
    ax2 = ax.twinx()
    vin = d['in_kAC%d' % kac]
    ax2.plot(t, vin, color='C3', lw=0.9, alpha=0.75, label='V(in), input')
    span = max(vin.max() - vin.min(), 1e-3)
    ax2.set_ylim(vin.min() - 0.15 * span, vin.max() + 0.15 * span)
    ax2.set_ylabel('input [V]', color='C3')
    if first:
        h1, l1 = ax.get_legend_handles_labels(); h2, l2 = ax2.get_legend_handles_labels()
        ax.legend(h1 + h2, l1 + l2, fontsize=7, loc='lower left')


fig, axs = plt.subplots(5, 2, figsize=(11, 15))
for s in range(1, 6):
    d = load('final_1_sig%d.csv' % s)
    name, gdc, gac, sc, unit = SIG[s]
    panel(axs[s - 1, 0], d, 0, '%d) %s – DC coupling, %s' % (s, name, gdc), sc, unit, s == 1)
    panel(axs[s - 1, 1], d, 1, '%d) %s – AC coupling, %s' % (s, name, gac), sc, unit, False)
fig.suptitle('Final simulation 1: output at the ADC pin for five input signals (green band = ADC range 0…3.3 V)', fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.98))
fig.savefig(os.path.join(HERE, 'Final_1_signals.png'), dpi=140)
plt.close(fig)

# zoom on the edge of the square wave (compensation)
d = load('final_1_sig3.csv')
fig, ax = plt.subplots(figsize=(7, 4))
t = d['t_s'] * 1e6
m = (t > 480) & (t < 560)
ax.plot(t[m], d['v_out_kAC0'][m], label='V(v_out), DC coupling')
ax.plot(t[m], d['v_out_kAC1'][m], '--', label='V(v_out), AC coupling')
ax.set_xlabel('time [µs]'); ax.set_ylabel('ADC pin [V]'); ax.grid(alpha=0.3); ax.legend()
ax.set_title('Final simulation 1, signal 3: falling edge of the ±40 V square wave (G = 1)')
fig.tight_layout(); fig.savefig(os.path.join(HERE, 'Final_1_square_edge.png'), dpi=140); plt.close(fig)

# ---------------------------------------------------------------- 2: AC response
d = load('final_2_AC_response.csv')
f = d['f_Hz']
GAINS = [('1e+09', 'G = 1', 'C0'), ('499', 'G = 3', 'C1'), ('110', 'G = 10', 'C2'), ('52.3', 'G = 20', 'C3')]
fig, (a1, a2) = plt.subplots(2, 1, figsize=(10, 9))
for rg, lab, c in GAINS:
    for kac, ls in ((0, '-'), (1, '--')):
        y = d['dB_Rg%s_kAC%d' % (rg, kac)]
        a1.semilogx(f, y, ls, color=c, lw=1.4, label='%s, %s' % (lab, 'DC coupling' if kac == 0 else 'AC coupling'))
        ref = np.interp(1e3, f, y)
        a2.semilogx(f, (10 ** ((y - ref) / 20) - 1) * 100, ls, color=c, lw=1.4)
a1.set_ylabel('V(v_out) / V(in) [dB]'); a1.set_ylim(-50, 0); a1.grid(which='both', alpha=0.3); a1.legend(ncol=4, fontsize=7.5, loc='lower center')
a1.set_title('Final simulation 2: frequency response from the BNC input to the ADC pin')
a2.axvline(1e6, color='0.5', ls=':'); a2.axhspan(-1, 1, color='#e8f2e8')
a2.set_ylim(-10, 5); a2.set_xlim(1, 5e6); a2.grid(which='both', alpha=0.3)
a2.set_ylabel('gain relative to 1 kHz [%]'); a2.set_xlabel('frequency [Hz]')
a2.set_title('Zoom: deviation from the 1 kHz gain (green band = ±1 %, dotted line = 1 MHz)', fontsize=10)
fig.tight_layout(); fig.savefig(os.path.join(HERE, 'Final_2_AC_response.png'), dpi=140); plt.close(fig)

# ---------------------------------------------------------------- 3: offset trim
d = load('final_3_offset_trim.csv')
t = d['t_s'] * 1e3
fig, axs = plt.subplots(1, 2, figsize=(11, 4.6), sharey=True)
for ax, kac, name in ((axs[0], 0, 'DC coupling'), (axs[1], 1, 'AC coupling')):
    ax.axhspan(0, VDDA, color='#e8f2e8', zorder=0)
    ax.axhline(VMID, color='0.5', lw=0.8, ls=':')
    b = d['v_out_trim0_kAC%d' % kac]; a = d['v_out_trim1_kAC%d' % kac]
    ax.plot(t, b, color='C3', label='before (no trim): zero line %.3f V' % b.mean())
    ax.plot(t, a, color='C0', label='after (DAC trim): zero line %.3f V' % a.mean())
    ax.axhline(b.mean(), color='C3', lw=0.8, ls='--'); ax.axhline(a.mean(), color='C0', lw=0.8, ls='--')
    ax.set_title('%s, G = 20, input: sine 1 kHz, 2 V amplitude' % name, fontsize=10)
    ax.set_xlabel('time [ms]'); ax.legend(fontsize=8, loc='lower right'); ax.set_ylim(-0.2, 4.3)
axs[0].set_ylabel('ADC pin V(v_out) [V]')
fig.suptitle('Final simulation 3: offset of node N and its correction with the DAC (green band = ADC range)', fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.95)); fig.savefig(os.path.join(HERE, 'Final_3_offset_trim.png'), dpi=140); plt.close(fig)
print('done')
