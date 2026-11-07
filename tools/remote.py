"""
Pilot rejestratora 4-kanałowego (NUCLEO-G474RE) - sterowanie przez wirtualny COM ST-LINK.

Narzędzie tymczasowe do testów: wysyła te same komendy, które generują przyciski i enkodery płytki
(RUN/STOP/SINGLE, AUTO/NORM, kanał/zbocze/poziom wyzwalania, podstawa czasu, kanały, wzmocnienie,
AC/DC, skala), a do tego steruje generatorem testowym na PA4 (gniazdo A2) i pokazuje stan urządzenia.

Uruchomienie:  python tools/remote.py
Wymaga:        pip install pyserial   (Tkinter jest w standardowym Pythonie)
Protokół:      linie tekstowe, odpowiedź "OK"/"ERR ..." + linia stanu "#S klucz=wartość ..." (co 1 s i po komendzie).
"""
import queue
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext

import serial
import serial.tools.list_ports

BAUD = 209700
TB_DEFAULT = ["1us", "2us", "5us", "10us", "20us", "50us", "100us", "200us",
              "500us", "1ms", "2ms", "5ms", "10ms", "20ms", "50ms"]
CH_COLORS = ["#d4b106", "#1a9fd6", "#d6457a", "#4caf50"]
PINS = ["PB1", "PB15", "PA0 (A0)", "PA1 (A1)"]
GAINS = ["1", "2", "5", "10"]
IN2ADC = {"1": 0.04737, "2": 0.09455, "5": 0.2348, "10": 0.4630}   # wejście BNC -> pin ADC
VREF = 3.3


class Remote(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Rejestrator 4ch - pilot (tymczasowy)")
        self.ser = None
        self.rx = queue.Queue()
        self.alive = False
        self.tb_labels = list(TB_DEFAULT)
        self.dragging = False

        self.v_port = tk.StringVar()
        self.v_mode = tk.StringVar(value="AUTO")
        self.v_trig = tk.IntVar(value=1)
        self.v_edge = tk.StringVar(value="R")
        self.v_level = tk.DoubleVar(value=0.0)
        self.v_lauto = tk.BooleanVar(value=True)
        self.v_tb = tk.StringVar(value="20us")
        self.v_ovs = tk.BooleanVar(value=True)
        self.v_adc = tk.BooleanVar(value=False)          # skala: pin ADC (test na Nucleo)
        self.v_on = [tk.BooleanVar(value=True) for _ in range(4)]
        self.v_gain = [tk.StringVar(value="1") for _ in range(4)]
        self.v_cpl = [tk.StringVar(value="DC") for _ in range(4)]
        self.v_gshape = tk.StringVar(value="SIN")
        self.v_gfreq = tk.StringVar(value="10k")
        self.v_show_s = tk.BooleanVar(value=False)

        self._build()
        self._refresh_ports()
        self.after(50, self._poll_rx)

    # ------------------------------------------------------------------ GUI
    def _build(self):
        pad = dict(padx=4, pady=3)
        top = ttk.Frame(self)
        top.pack(fill="x", **pad)
        ttk.Label(top, text="Port:").pack(side="left")
        self.cb_port = ttk.Combobox(top, textvariable=self.v_port, width=40, state="readonly")
        self.cb_port.pack(side="left", padx=4)
        ttk.Button(top, text="Odśwież", command=self._refresh_ports).pack(side="left")
        self.b_conn = ttk.Button(top, text="Połącz", command=self._toggle_conn)
        self.b_conn.pack(side="left", padx=4)
        self.l_conn = ttk.Label(top, text="rozłączony", foreground="gray")
        self.l_conn.pack(side="left", padx=8)

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, **pad)
        left = ttk.Frame(body)
        left.pack(side="left", fill="y")
        right = ttk.Frame(body)
        right.pack(side="left", fill="both", expand=True, padx=6)

        # akwizycja i tryb
        f = ttk.LabelFrame(left, text="Akwizycja (przycisk RUN/STOP, długo = SINGLE; przycisk AUTO)")
        f.pack(fill="x", **pad)
        for t in ("RUN", "STOP", "SINGLE"):
            ttk.Button(f, text=t, width=8, command=lambda c=t: self.send(c)).pack(side="left", **pad)
        ttk.Separator(f, orient="vertical").pack(side="left", fill="y", padx=6)
        for t in ("AUTO", "NORM"):
            ttk.Radiobutton(f, text=t, value=t, variable=self.v_mode,
                            command=lambda: self.send("MODE " + self.v_mode.get())).pack(side="left")

        # wyzwalanie
        f = ttk.LabelFrame(left, text="Wyzwalanie (enkoder X: poziom, wciśnięcie = AUTO; T wciśnięcie = zbocze)")
        f.pack(fill="x", **pad)
        r = ttk.Frame(f); r.pack(fill="x")
        ttk.Label(r, text="Źródło:").pack(side="left")
        for i in range(1, 5):
            ttk.Radiobutton(r, text=f"CH{i}", value=i, variable=self.v_trig,
                            command=lambda: self.send(f"TRIG {self.v_trig.get()}")).pack(side="left")
        ttk.Label(r, text="   Zbocze:").pack(side="left")
        ttk.Radiobutton(r, text="↑", value="R", variable=self.v_edge,
                        command=lambda: self.send("EDGE R")).pack(side="left")
        ttk.Radiobutton(r, text="↓", value="F", variable=self.v_edge,
                        command=lambda: self.send("EDGE F")).pack(side="left")
        r = ttk.Frame(f); r.pack(fill="x")
        self.l_lvl = ttk.Label(r, text="Poziom [V]:")
        self.l_lvl.pack(side="left")
        self.sc_level = tk.Scale(r, from_=-35, to=35, orient="horizontal", length=240,
                                 variable=self.v_level, resolution=0.05)
        self.sc_level.pack(side="left")
        self.sc_level.bind("<ButtonPress-1>", lambda e: setattr(self, "dragging", True))
        self.sc_level.bind("<ButtonRelease-1>", self._level_release)
        ttk.Checkbutton(r, text="AUTO (środek)", variable=self.v_lauto,
                        command=self._send_level).pack(side="left")
        ttk.Button(r, text="−", width=2, command=lambda: self.send("LEVEL -")).pack(side="left")
        ttk.Button(r, text="+", width=2, command=lambda: self.send("LEVEL +")).pack(side="left")

        # podstawa czasu
        f = ttk.LabelFrame(left, text="Podstawa czasu (enkoder T)")
        f.pack(fill="x", **pad)
        ttk.Button(f, text="◄ szybciej", command=lambda: self.send("TB -")).pack(side="left", **pad)
        self.cb_tb = ttk.Combobox(f, textvariable=self.v_tb, values=self.tb_labels, width=7, state="readonly")
        self.cb_tb.pack(side="left", **pad)
        self.cb_tb.bind("<<ComboboxSelected>>", lambda e: self.send("TB " + self.v_tb.get()))
        ttk.Label(f, text="/dz").pack(side="left")
        ttk.Button(f, text="wolniej ►", command=lambda: self.send("TB +")).pack(side="left", **pad)
        ttk.Checkbutton(f, text="nadpróbkowanie ADC", variable=self.v_ovs,
                        command=lambda: self.send("OVS " + ("ON" if self.v_ovs.get() else "OFF"))).pack(side="left")

        # kanały
        f = ttk.LabelFrame(left, text="Kanały (przyciski CH: wł. / aktywny / wył.; enkoder Y: wzmocnienie, wciśnięcie = AC/DC)")
        f.pack(fill="x", **pad)
        self.l_act = []
        for i in range(4):
            r = ttk.Frame(f); r.pack(fill="x")
            l = tk.Label(r, text=" ", width=2, fg=CH_COLORS[i], font=("Segoe UI", 10, "bold"))
            l.pack(side="left")
            self.l_act.append(l)
            tk.Checkbutton(r, text=f"CH{i+1} {PINS[i]:9s}", variable=self.v_on[i], fg=CH_COLORS[i], width=14,
                           anchor="w", command=lambda i=i: self.send(f"CH {i+1} " + ("ON" if self.v_on[i].get() else "OFF"))
                           ).pack(side="left")
            ttk.Button(r, text="przycisk CH", command=lambda i=i: self.send(f"CHBTN {i+1}")).pack(side="left", padx=2)
            ttk.Label(r, text="  x").pack(side="left")
            cb = ttk.Combobox(r, textvariable=self.v_gain[i], values=GAINS, width=3, state="readonly")
            cb.pack(side="left")
            cb.bind("<<ComboboxSelected>>", lambda e, i=i: self.send(f"GAIN {i+1} {self.v_gain[i].get()}"))
            for c in ("DC", "AC"):
                ttk.Radiobutton(r, text=c, value=c, variable=self.v_cpl[i],
                                command=lambda i=i: self.send(f"COUP {i+1} {self.v_cpl[i].get()}")).pack(side="left")
        r = ttk.Frame(f); r.pack(fill="x", pady=2)
        ttk.Checkbutton(r, text="skala: napięcie na pinie ADC (test na samej Nucleo, bez płytki)",
                        variable=self.v_adc, command=self._send_scale).pack(side="left")

        # generator
        f = ttk.LabelFrame(left, text="Generator testowy na PA4 (gniazdo A2) - tylko Nucleo")
        f.pack(fill="x", **pad)
        ttk.Combobox(f, textvariable=self.v_gshape, values=["SIN", "SQR", "TRI", "OFF"],
                     width=5, state="readonly").pack(side="left", **pad)
        ttk.Entry(f, textvariable=self.v_gfreq, width=8).pack(side="left", **pad)
        ttk.Label(f, text="Hz (np. 500, 10k, 100k)").pack(side="left")
        ttk.Button(f, text="Ustaw", command=lambda: self.send(
            f"GEN {self.v_gshape.get()} {self.v_gfreq.get()}")).pack(side="left", **pad)

        # stan urządzenia
        f = ttk.LabelFrame(right, text="Stan urządzenia (linia #S)")
        f.pack(fill="x", **pad)
        self.l_state = tk.Label(f, text="-", font=("Segoe UI", 16, "bold"))
        self.l_state.pack(anchor="w", padx=6)
        self.l_info = tk.Label(f, text="", font=("Consolas", 10), justify="left", anchor="w")
        self.l_info.pack(fill="x", padx=6, pady=4)
        self.l_vpp = []
        r = ttk.Frame(f); r.pack(fill="x", padx=6, pady=2)
        for i in range(4):
            l = tk.Label(r, text=f"CH{i+1}: -", fg=CH_COLORS[i], font=("Consolas", 11, "bold"), width=17, anchor="w")
            l.pack(side="left")
            self.l_vpp.append(l)

        # konsola
        f = ttk.LabelFrame(right, text="Terminal")
        f.pack(fill="both", expand=True, **pad)
        self.log = scrolledtext.ScrolledText(f, height=18, font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)
        r = ttk.Frame(f); r.pack(fill="x")
        self.e_cmd = ttk.Entry(r)
        self.e_cmd.pack(side="left", fill="x", expand=True, padx=2, pady=2)
        self.e_cmd.bind("<Return>", lambda e: self._send_entry())
        ttk.Button(r, text="Wyślij", command=self._send_entry).pack(side="left")
        ttk.Button(r, text="HELP", command=lambda: self.send("HELP")).pack(side="left")
        ttk.Checkbutton(r, text="pokazuj #S", variable=self.v_show_s).pack(side="left", padx=4)
        ttk.Button(r, text="Wyczyść", command=lambda: self.log.delete("1.0", "end")).pack(side="left")

    # ------------------------------------------------------------------ port
    def _refresh_ports(self):
        ports = list(serial.tools.list_ports.comports())
        names = [f"{p.device} - {p.description}" for p in ports]
        self.cb_port["values"] = names
        stl = [n for n in names if "STLink" in n or "STMicro" in n]
        if stl:
            self.v_port.set(stl[0])
        elif names and not self.v_port.get():
            self.v_port.set(names[0])

    def _toggle_conn(self):
        if self.ser:
            self.alive = False
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
            self.b_conn["text"] = "Połącz"
            self.l_conn.config(text="rozłączony", foreground="gray")
            return
        dev = self.v_port.get().split(" - ")[0]
        if not dev:
            return
        try:
            self.ser = serial.Serial(dev, BAUD, timeout=0.1)
        except Exception as e:
            self._log(f"!! Nie mogę otworzyć {dev}: {e}\n"
                      "   (port zajęty? zamknij PuTTY / konsolę szeregową CubeIDE)\n")
            return
        self.alive = True
        threading.Thread(target=self._reader, daemon=True).start()
        self.b_conn["text"] = "Rozłącz"
        self.l_conn.config(text=f"połączony {dev} @ {BAUD}", foreground="green")
        self.send("TBLIST")
        self.send("STAT")

    def _reader(self):
        buf = b""
        while self.alive and self.ser:
            try:
                data = self.ser.read(256)
            except Exception as e:
                self.rx.put(f"!! błąd odczytu: {e}")
                break
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.rx.put(line.decode("latin-1").rstrip("\r"))

    def send(self, cmd):
        if not self.ser:
            self._log("!! najpierw Połącz\n")
            return
        self._log(f"> {cmd}\n")
        self.ser.write((cmd + "\n").encode("ascii", "replace"))

    def _send_entry(self):
        c = self.e_cmd.get().strip()
        if c:
            self.send(c)
            self.e_cmd.delete(0, "end")

    def _level_release(self, _e):
        self.dragging = False
        self.v_lauto.set(False)
        self._send_level()

    def _send_level(self):
        self.send("LEVEL AUTO" if self.v_lauto.get() else f"LEVEL {self.v_level.get():.3f}")

    def _send_scale(self):
        self.send("SCALE " + ("ADC" if self.v_adc.get() else "IN"))

    # ------------------------------------------------------------------ odbiór
    def _log(self, s):
        self.log.insert("end", s)
        self.log.see("end")
        if int(self.log.index("end").split(".")[0]) > 3000:
            self.log.delete("1.0", "1000.0")

    def _poll_rx(self):
        try:
            while True:
                line = self.rx.get_nowait()
                if line.startswith("#S "):
                    self._parse_status(line)
                    if self.v_show_s.get():
                        self._log(line + "\n")
                elif line.startswith("#TB "):
                    parts = line.split()
                    if len(parts) == 3:
                        i = int(parts[1])
                        while len(self.tb_labels) <= i:
                            self.tb_labels.append("?")
                        self.tb_labels[i] = parts[2]
                        self.cb_tb["values"] = self.tb_labels
                else:
                    self._log(line + "\n")
        except queue.Empty:
            pass
        self.after(50, self._poll_rx)

    @staticmethod
    def _fmt_v(mv):
        v = mv / 1000.0
        return f"{v:.2f} V" if abs(v) < 10 else f"{v:.1f} V"

    def _parse_status(self, line):
        st = dict(kv.split("=", 1) for kv in line[3:].split() if "=" in kv)
        run = st.get("run") == "1"
        state = st.get("st", "?")
        color = {"TRIG": "green", "AUTO": "#c77700", "WAIT": "#c77700", "STOP": "red"}.get(state, "black")
        self.l_state.config(text=f"{'RUN' if run else 'STOP'}  {st.get('mode', '')}  ·  {state}", fg=color)
        try:
            fs, fsm, f = int(st.get("fs", "0")), int(st.get("fsm", "0")), float(st.get("f", "0"))
            lvl = int(st.get("lvl", "0"))
        except ValueError:
            fs, fsm, f, lvl = 0, 0, 0.0, 0
        adc = st.get("scale") == "ADC"
        gains = st.get("g", "1,1,1,1").split(",")
        cpl = st.get("cpl", "DDDD")
        on = st.get("on", "1111")
        act = st.get("act", "1")
        tc = int(st.get("trig", "1")) if st.get("trig", "").isdigit() else 1
        self.l_info.config(text=(
            f"T/dz = {st.get('tdiv', '?')}    fs = {fs/1e6:.4f} MSa/s (zmierzona {fsm/1e6:.4f})"
            f"    nadpróbkowanie x{st.get('ovs', '?')}    blok {st.get('n', '?')} próbek\n"
            f"wyzwalanie CH{tc} {'↑' if st.get('edge') == 'R' else '↓'}  poziom {self._fmt_v(lvl)}"
            f"{' (auto)' if st.get('lauto') == '1' else ''}    f = {f/1000:.3f} kHz    obraz {st.get('fps', '?')} kl/s\n"
            f"skala: {'pin ADC' if adc else 'wejście BNC'}    kanał aktywny CH{act}"
            f"    generator: {st.get('gen', '?').replace(',', ' ')} Hz"))
        vpp = st.get("vpp", "").split(",")
        for i in range(4):
            if i < len(vpp) and vpp[i].lstrip("-").isdigit():
                txt = f"CH{i+1}: {self._fmt_v(int(vpp[i]))}pp" if on[i] == "1" else f"CH{i+1}: off"
                self.l_vpp[i].config(text=txt)
        # synchronizacja kontrolek ze stanem urządzenia (bez wysyłania komend)
        if st.get("mode") in ("AUTO", "NORM"):
            self.v_mode.set(st["mode"])
        self.v_trig.set(tc)
        if st.get("edge") in ("R", "F"):
            self.v_edge.set(st["edge"])
        self.v_lauto.set(st.get("lauto") == "1")
        if adc:
            lo, hi = 0.0, VREF
        else:
            g = gains[tc - 1] if tc - 1 < len(gains) else "1"
            hi = round(VREF / 2 / IN2ADC.get(g, 0.04737), 1)
            lo = -hi
        self.sc_level.config(from_=lo, to=hi)
        self.l_lvl.config(text="Poziom [V, pin ADC]:" if adc else "Poziom [V, wejście]:")
        if not self.dragging:
            self.v_level.set(lvl / 1000.0)
        if "tdiv" in st:
            self.v_tb.set(st["tdiv"])
        if "ovsen" in st:
            self.v_ovs.set(st["ovsen"] == "1")
        self.v_adc.set(adc)
        for i in range(4):
            if i < len(on):
                self.v_on[i].set(on[i] == "1")
            if i < len(gains):
                self.v_gain[i].set(gains[i])
            if i < len(cpl):
                self.v_cpl[i].set("DC" if cpl[i] == "D" else "AC")
            self.l_act[i].config(text="►" if str(i + 1) == act else " ")


if __name__ == "__main__":
    Remote().mainloop()
