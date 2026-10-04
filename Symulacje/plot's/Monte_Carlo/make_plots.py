"""Makes the Monte Carlo figures and the summary table from the CSV files in data/.

Run:  python make_plots.py        (needs numpy and matplotlib)
Input : data/AC_*_meas.csv, data/AC_*_response.csv, data/OP_*.csv  (exported from the LTspice runs)
Output: PNG figures in this folder and MC_summary.csv
"""
import csv, glob, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
NOM = {"G1": 0.029663, "G20": 0.59684}          # hand-calculated gain at 1 kHz (A and A*G, G = 20.12)
LABEL = {
    "A": "R 0.1 %, C 5 %, strays ±30 %",
    "B": "R 1 %, C 5 %, strays ±30 %",
    "C": "R 0.1 %, C 1 %, strays ±10 %",
}
COL = {"A": "tab:blue", "B": "tab:red", "C": "tab:green"}
plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.alpha": 0.3, "figure.dpi": 100})

def read_csv(path):
    with open(path) as f:
        r = csv.reader(f); head = next(r)
        rows = [[float(x) if x not in ("", "nan", "None") else np.nan for x in row] for row in r]
    return head, np.array(rows)

def meas(case):
    h, a = read_csv(os.path.join(DATA, f"{case}_meas.csv")); return {k: a[:, i] for i, k in enumerate(h)}

def response(case):
    h, a = read_csv(os.path.join(DATA, f"{case}_response.csv")); return a[:, 0], a[:, 1:]

def op(case):
    h, a = read_csv(os.path.join(DATA, f"{case}.csv")); return {k: a[:, i] for i, k in enumerate(h)}

# ---- 1. frequency response of all runs, one figure per case ------------------------------------------
def fig_response(case, title):
    f, y = response(case)
    rel = y / y[0]                               # normalised to the first point (1 kHz)
    fig, ax = plt.subplots(2, 1, figsize=(9, 8))
    ax[0].semilogx(f, 20 * np.log10(rel), lw=0.8, alpha=0.7)
    ax[0].set(xlabel="frequency [Hz]", ylabel="gain relative to 1 kHz [dB]", title=title + " – all runs", ylim=(-8, 3))
    ax[0].axhline(-3, color="k", ls=":", lw=0.8)
    m = f <= 3e6
    ax[1].semilogx(f[m], (rel[m] - 1) * 100, lw=0.8, alpha=0.7)
    ax[1].set(xlabel="frequency [Hz]", ylabel="gain error relative to 1 kHz [%]", title="zoom up to 3 MHz", ylim=(-12, 8))
    for v in (-1, 1): ax[1].axhline(v, color="k", ls=":", lw=0.8)
    fig.tight_layout(); fig.savefig(os.path.join(HERE, f"MC_response_{case}.png"), dpi=150); plt.close(fig)

# ---- 2. comparison of tolerance sets (G = 1) ---------------------------------------------------------
def fig_compare():
    fig, ax = plt.subplots(1, 3, figsize=(15, 5), sharey=True)
    for a, k in zip(ax, "ABC"):
        f, y = response(f"AC_{k}_G1"); rel = (y / y[0] - 1) * 100; m = f <= 3e6
        a.fill_between(f[m], rel[m].min(axis=1), rel[m].max(axis=1), color=COL[k], alpha=0.25, label="min–max of 30 runs")
        a.semilogx(f[m], np.median(rel[m], axis=1), color=COL[k], label="median")
        a.set(title=LABEL[k], xlabel="frequency [Hz]", ylim=(-12, 8)); a.legend(loc="lower left")
    ax[0].set_ylabel("gain error relative to 1 kHz [%]")
    fig.suptitle("Monte Carlo, G = 1: effect of component tolerances on the flatness")
    fig.tight_layout(); fig.savefig(os.path.join(HERE, "MC_compare_tolerances_G1.png"), dpi=150); plt.close(fig)

# ---- 3. histograms -----------------------------------------------------------------------------------
def fig_hist():
    fig, ax = plt.subplots(1, 3, figsize=(15, 4.5))
    g = {k: (meas(f"AC_{k}_G1")["gain_1kHz"] / NOM["G1"] - 1) * 100 for k in "AB"}
    bins = np.linspace(min(v.min() for v in g.values()) - 0.1, max(v.max() for v in g.values()) + 0.1, 25)
    for k in "AB":
        ax[0].hist(g[k], bins=bins, alpha=0.6, color=COL[k], label=f"{LABEL[k]}  (σ={g[k].std():.2f} %)")
    ax[0].set(title="gain at 1 kHz, G = 1", xlabel="deviation from the hand-calculated 0.029663 [%]", ylabel="runs"); ax[0].legend(fontsize=8)
    for a, key, t in ((ax[1], "ratio_100k_to_1k", "gain at 100 kHz relative to 1 kHz"), (ax[2], "ratio_1M_to_1k", "gain at 1 MHz relative to 1 kHz")):
        r = {k: (meas(f"AC_{k}_G1")[key] - 1) * 100 for k in "ABC"}
        bins = np.linspace(min(v.min() for v in r.values()) - 0.2, max(v.max() for v in r.values()) + 0.2, 25)
        for k in "ABC":
            a.hist(r[k], bins=bins, alpha=0.5, color=COL[k], label=f"{LABEL[k]}  (σ={r[k].std():.2f} %)")
        a.set(title=t + ", G = 1", xlabel="deviation [%]"); a.legend(fontsize=7)
    fig.tight_layout(); fig.savefig(os.path.join(HERE, "MC_histograms_G1.png"), dpi=150); plt.close(fig)

# ---- 4. DC bias and offsets --------------------------------------------------------------------------
def fig_dc():
    fig, ax = plt.subplots(1, 3, figsize=(15, 4.8))
    d = op("OP_A_G1_DC")
    ax[0].boxplot([d["dN_mV"], d["dVref_mV"]], tick_labels=["N − 1.65 V", "Vref − 1.65 V"], showmeans=True)
    ax[0].set(title="bias nodes at zero input (DC mode)", ylabel="[mV]")
    ax[1].boxplot([op("OP_A_G1_DC")["dVout_mV"], op("OP_A_G20_DC")["dVout_mV"]], tick_labels=["G = 1", "G = 20"], showmeans=True)
    ax[1].set(title="output − 1.65 V at zero input, DC mode", ylabel="[mV]")
    ax[2].boxplot([op("OP_A_G1_ACm")["dVout_mV"], op("OP_A_G20_ACm")["dVout_mV"]], tick_labels=["G = 1", "G = 20"], showmeans=True)
    ax[2].set(title="output − 1.65 V at zero input, AC mode", ylabel="[mV]")
    fig.suptitle("Monte Carlo: DC levels, R 0.1 %, C 5 %")
    fig.tight_layout(); fig.savefig(os.path.join(HERE, "MC_dc_bias.png"), dpi=150); plt.close(fig)

# ---- 5. summary table --------------------------------------------------------------------------------
def summary():
    rows = [["case", "quantity", "runs", "mean", "std", "min", "max", "unit"]]
    def add(case, q, v, unit):
        v = v[~np.isnan(v)]
        rows.append([case, q, len(v), f"{v.mean():.5g}", f"{v.std():.3g}", f"{v.min():.5g}", f"{v.max():.5g}", unit])
    for p in sorted(glob.glob(os.path.join(DATA, "AC_*_meas.csv"))):
        c = os.path.basename(p)[:-9]; m = meas(c); nom = NOM["G20" if "G20" in c else "G1"]
        add(c, "gain 1 kHz deviation from nominal", (m["gain_1kHz"] / nom - 1) * 100, "%")
        add(c, "gain 100 kHz relative to 1 kHz", (m["ratio_100k_to_1k"] - 1) * 100, "%")
        add(c, "gain 1 MHz relative to 1 kHz", (m["ratio_1M_to_1k"] - 1) * 100, "%")
        add(c, "f(-3 dB)", m["f_minus3dB_MHz"], "MHz")
    for p in sorted(glob.glob(os.path.join(DATA, "OP_*.csv"))):
        c = os.path.basename(p)[:-4]; m = op(c)
        for k, q in (("dN_mV", "N - 1.65 V"), ("dVref_mV", "Vref - 1.65 V"), ("dVout_mV", "Vout - 1.65 V")): add(c, q, m[k], "mV")
    with open(os.path.join(HERE, "MC_summary.csv"), "w", newline="") as f: csv.writer(f).writerows(rows)

if __name__ == "__main__":
    fig_response("AC_A_G1", "G = 1, R 0.1 %, C 5 %, strays ±30 %")
    fig_response("AC_A_G20", "G = 20, R 0.1 %, C 5 %, strays ±30 %")
    fig_compare(); fig_hist(); fig_dc(); summary()
    print("done")
