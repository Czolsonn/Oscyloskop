"""
Pilot rejestratora 4-kanałowego (NUCLEO-G474RE) - sterowanie przez wirtualny COM ST-LINK.

Zastępuje przyciski i enkodery płytki na czas testów na samej Nucleo:
RUN/STOP/SINGLE, tryb AUTO/NORM, kanał/zbocze/poziom wyzwalania, podstawa czasu, kanały,
nadpróbkowanie ADC i generator sygnału testowego na PA4 (gniazdo A2).

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


class Remote(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Rejestrator 4ch - pilot")
        self.ser = None
        self.rx = queue.Queue()
        self.alive = False
        self.tb_labels = list(TB_DEFAULT)

        self.v_port = tk.StringVar()
        self.v_mode = tk.StringVar(value="AUTO")
        self.v_trig = tk.IntVar(value=1)
        self.v_edge = tk.StringVar(value="R")
        self.v_level = tk.IntVar(value=1650)
        self.v_lauto = tk.BooleanVar(value=True)
        self.v_tb = tk.StringVar(value="20us")
        self.v_ovs = tk.BooleanVar(value=True)
        self.v_on = [tk.BooleanVar(value=True) for _ in range(4)]
        self.v_gshape = tk.StringVar(value="SIN")
        self.v_gfreq = tk.StringVar(value="10k")
        self.v_show_s = tk.BooleanVar(value=False)
        self.st = {}                       # pola linii #S do wyświetlenia

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

        # akwizycja
        f = ttk.LabelFrame(left, text="Akwizycja")
        f.pack(fill="x", **pad)
        for t in ("RUN", "STOP", "SINGLE"):
            ttk.Button(f, text=t, width=8, command=lambda c=t: self.send(c)).pack(side="left", **pad)
        f = ttk.LabelFrame(left, text="Tryb wyzwalania")
        f.pack(fill="x", **pad)
        for t in ("AUTO", "NORM"):
            ttk.Radiobutton(f, text=t, value=t, variable=self.v_mode,
                            command=lambda: self.send("MODE " + self.v_mode.get())).pack(side="left", **pad)

        # wyzwalanie
        f = ttk.LabelFrame(left, text="Wyzwalanie")
        f.pack(fill="x", **pad)
        r = ttk.Frame(f); r.pack(fill="x")
        ttk.Label(r, text="Kanał:").pack(side="left")
        for i in range(1, 5):
            ttk.Radiobutton(r, text=f"CH{i}", value=i, variable=self.v_trig,
                            command=self._send_trig).pack(side="left")
        r = ttk.Frame(f); r.pack(fill="x")
        ttk.Label(r, text="Zbocze:").pack(side="left")
        ttk.Radiobutton(r, text="narastające ↑", value="R", variable=self.v_edge,
                        command=self._send_trig).pack(side="left")
        ttk.Radiobutton(r, text="opadające ↓", value="F", variable=self.v_edge,
                        command=self._send_trig).pack(side="left")
        r = ttk.Frame(f); r.pack(fill="x")
        ttk.Label(r, text="Poziom [mV]:").pack(side="left")
        self.sc_level = tk.Scale(r, from_=0, to=3300, orient="horizontal", length=220,
                                 variable=self.v_level, resolution=10)
        self.sc_level.pack(side="left")
        self.sc_level.bind("<ButtonRelease-1>", lambda e: self._send_level())
        ttk.Checkbutton(r, text="AUTO (środek)", variable=self.v_lauto,
                        command=self._send_level).pack(side="left")

        # podstawa czasu
        f = ttk.LabelFrame(left, text="Podstawa czasu")
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
        f = ttk.LabelFrame(left, text="Kanały widoczne (CH1/CH2 = płytka, CH3/CH4 = DNP)")
        f.pack(fill="x", **pad)
        pins = ["PB1", "PB15", "PA0 (A0)", "PA1 (A1)"]
        for i in range(4):
            tk.Checkbutton(f, text=f"CH{i+1} {pins[i]}", variable=self.v_on[i], fg=CH_COLORS[i],
                           command=lambda i=i: self.send(f"CH {i+1} " + ("ON" if self.v_on[i].get() else "OFF"))
                           ).pack(side="left", **pad)

        # generator
        f = ttk.LabelFrame(left, text="Generator testowy na PA4 (gniazdo A2)")
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
            l = tk.Label(r, text=f"CH{i+1}: -", fg=CH_COLORS[i], font=("Consolas", 11, "bold"), width=16, anchor="w")
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

    def _send_trig(self):
        self.send(f"TRIG {self.v_trig.get()} {self.v_edge.get()}")

    def _send_level(self):
        self.send("LEVEL AUTO" if self.v_lauto.get() else f"LEVEL {self.v_level.get()}")

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

    def _parse_status(self, line):
        st = dict(kv.split("=", 1) for kv in line[3:].split() if "=" in kv)
        self.st = st
        run = st.get("run") == "1"
        state = st.get("st", "?")
        color = {"TRIG": "green", "AUTO": "#c77700", "WAIT": "#c77700", "STOP": "red"}.get(state, "black")
        self.l_state.config(text=f"{'RUN' if run else 'STOP'}  {st.get('mode', '')}  ·  {state}", fg=color)
        try:
            fs = int(st.get("fs", "0"))
            fsm = int(st.get("fsm", "0"))
            f = float(st.get("f", "0"))
        except ValueError:
            fs, fsm, f = 0, 0, 0.0
        self.l_info.config(text=(
            f"T/dz = {st.get('tdiv', '?')}    fs = {fs/1e6:.4f} MSa/s (zmierzona {fsm/1e6:.4f})"
            f"    nadpróbkowanie x{st.get('ovs', '?')}    blok {st.get('n', '?')} próbek\n"
            f"wyzwalanie CH{st.get('trig', '?')} {'↑' if st.get('edge') == 'R' else '↓'}"
            f"  poziom {st.get('lvl', '?')} mV{' (auto)' if st.get('level') == '-1' else ''}"
            f"    f = {f/1000:.3f} kHz    obraz {st.get('fps', '?')} kl/s\n"
            f"generator: {st.get('gen', '?').replace(',', ' ')} Hz"))
        vpp = st.get("vpp", "").split(",")
        on = st.get("on", "1111")
        for i in range(4):
            if i < len(vpp) and vpp[i]:
                txt = f"CH{i+1}: {int(vpp[i])/1000:.2f} Vpp" if on[i] == "1" else f"CH{i+1}: off"
                self.l_vpp[i].config(text=txt)
        # synchronizacja kontrolek ze stanem urządzenia (bez wysyłania komend)
        if st.get("mode") in ("AUTO", "NORM"):
            self.v_mode.set(st["mode"])
        if st.get("trig", "").isdigit():
            self.v_trig.set(int(st["trig"]))
        if st.get("edge") in ("R", "F"):
            self.v_edge.set(st["edge"])
        if st.get("level") == "-1":
            self.v_lauto.set(True)
        elif st.get("level", "").isdigit():
            self.v_lauto.set(False)
            self.v_level.set(int(st["level"]))
        if "tdiv" in st:
            self.v_tb.set(st["tdiv"])
        if "ovsen" in st:
            self.v_ovs.set(st["ovsen"] == "1")
        for i in range(4):
            if i < len(on):
                self.v_on[i].set(on[i] == "1")


if __name__ == "__main__":
    Remote().mainloop()
