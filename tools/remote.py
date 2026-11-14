"""
Pilot rejestratora 4-kanałowego (NUCLEO-G474RE) - wirtualny panel płytki przez wirtualny COM ST-LINK.

Narzędzie tymczasowe do testów: przyciski i enkodery wysyłają DOKŁADNIE te komendy, które generuje
płytka (ui.c), więc zachowanie jest takie samo jak przy prawdziwym panelu. Dodatkowo: skala ADC
(test na samej Nucleo), kalibracja zera, stan urządzenia i terminal.

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
CH_COLORS = ["#d4b106", "#1a9fd6", "#d6457a", "#4caf50"]


class Remote(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Rejestrator 4ch - wirtualny panel (tymczasowy)")
        self.ser = None
        self.rx = queue.Queue()
        self.alive = False

        self.v_port = tk.StringVar()
        self.v_adc = tk.BooleanVar(value=False)
        self.v_show_s = tk.BooleanVar(value=False)

        self._build()
        self._refresh_ports()
        self.after(50, self._poll_rx)

    # ------------------------------------------------------------------ GUI
    def _enc(self, parent, name, title, hint):
        f = ttk.LabelFrame(parent, text=title)
        f.pack(side="left", padx=4, pady=3, fill="y")
        r = ttk.Frame(f); r.pack()
        ttk.Button(r, text="⟲", width=3, command=lambda: self.send(f"{name}ROT -")).pack(side="left", padx=2)
        ttk.Button(r, text="wciśnij", width=7, command=lambda: self.send(f"{name}PUSH")).pack(side="left", padx=2)
        ttk.Button(r, text="⟳", width=3, command=lambda: self.send(f"{name}ROT +")).pack(side="left", padx=2)
        ttk.Label(f, text=hint, foreground="gray", justify="center").pack(pady=2)

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

        # panel jak na płytce
        f = ttk.LabelFrame(self, text="Panel płytki")
        f.pack(fill="x", **pad)
        r = ttk.Frame(f); r.pack(fill="x", pady=2)
        for i in range(4):
            tk.Button(r, text=f"CH{i+1}", width=6, bg=CH_COLORS[i], fg="black",
                      command=lambda i=i: self.send(f"CHBTN {i+1}")).pack(side="left", padx=3)
        ttk.Label(r, text="(wł. → aktywny → wył.)", foreground="gray").pack(side="left", padx=4)
        ttk.Button(r, text="RUN/STOP", command=lambda: self.send("RUNBTN")).pack(side="left", padx=6)
        ttk.Button(r, text="RUN/STOP długo (SINGLE)", command=lambda: self.send("SINGLE")).pack(side="left")
        ttk.Button(r, text="AUTO", command=lambda: self.send("MODE T")).pack(side="left", padx=6)
        r = ttk.Frame(f); r.pack(fill="x")
        self._enc(r, "T", "Enkoder T (Trigger)", "obrót: poziom wyzwalania\nwciśnięcie: menu\nw menu: wybór / zmiana")
        self._enc(r, "X", "Enkoder X (oś czasu)", "obrót: T/dz albo pozycja\nwciśnięcie: przełącz")
        self._enc(r, "Y", "Enkoder Y (oś napięcia)", "obrót: V/dz albo offset\nkanału aktywnego\nwciśnięcie: przełącz")

        # kalibracja i diagnostyka
        f = ttk.LabelFrame(self, text="Kalibracja i diagnostyka")
        f.pack(fill="x", **pad)
        ttk.Button(f, text="Kalibruj zero (wejścia zwarte do masy, DC)",
                   command=lambda: self.send("CALZERO")).pack(side="left", **pad)
        ttk.Button(f, text="Usuń kalibrację", command=lambda: self.send("CALCLEAR")).pack(side="left", **pad)
        self.v_cal1k = tk.BooleanVar(value=True)
        ttk.Checkbutton(f, text="1 kHz na J13", variable=self.v_cal1k,
                        command=lambda: self.send("CAL1K " + ("ON" if self.v_cal1k.get() else "OFF"))).pack(side="left", **pad)
        ttk.Checkbutton(f, text="skala: pin ADC (bez płytki)", variable=self.v_adc,
                        command=lambda: self.send("SCALE " + ("ADC" if self.v_adc.get() else "IN"))).pack(side="left", **pad)
        ttk.Button(f, text="Pomiary CH1", command=lambda: self.send("MEAS 1")).pack(side="left", **pad)

        # stan
        f = ttk.LabelFrame(self, text="Stan urządzenia (linia #S)")
        f.pack(fill="x", **pad)
        self.l_state = tk.Label(f, text="-", font=("Segoe UI", 14, "bold"))
        self.l_state.pack(anchor="w", padx=6)
        self.l_info = tk.Label(f, text="", font=("Consolas", 10), justify="left", anchor="w")
        self.l_info.pack(fill="x", padx=6)
        r = ttk.Frame(f); r.pack(fill="x", padx=6, pady=2)
        self.l_ch = []
        for i in range(4):
            l = tk.Label(r, text=f"CH{i+1}", fg=CH_COLORS[i], font=("Consolas", 10, "bold"), justify="left", anchor="nw", width=24)
            l.pack(side="left")
            self.l_ch.append(l)

        # terminal
        f = ttk.LabelFrame(self, text="Terminal")
        f.pack(fill="both", expand=True, **pad)
        self.log = scrolledtext.ScrolledText(f, height=12, font=("Consolas", 9))
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
                else:
                    self._log(line + "\n")
        except queue.Empty:
            pass
        self.after(50, self._poll_rx)

    @staticmethod
    def _v(mv):
        v = int(mv) / 1000.0
        if abs(v) < 1:
            return f"{v*1000:.0f} mV"
        return f"{v:.2f} V" if abs(v) < 10 else f"{v:.1f} V"

    def _parse_status(self, line):
        st = dict(kv.split("=", 1) for kv in line[3:].split() if "=" in kv)
        try:
            run = st.get("run") == "1"
            state = st.get("st", "?")
            color = {"TRIG": "green", "AUTO": "#c77700", "WAIT": "#c77700", "STOP": "red"}.get(state, "black")
            self.l_state.config(text=f"{'RUN' if run else 'STOP'}  {st.get('mode', '')}  ·  {state}"
                                     f"{'   · MENU' if st.get('menu') == '1' else ''}", fg=color)
            fs, fsm, f = int(st.get("fs", "0")), int(st.get("fsm", "0")), float(st.get("f", "0"))
            xm = "pozycja (hpos " + st.get("hpos", "0") + " px)" if st.get("xm") == "P" else "T/dz"
            ym = "offset" if st.get("ym") == "P" else "V/dz"
            self.l_info.config(text=(
                f"T/dz = {st.get('tdiv', '?')}   fs = {fs/1e6:.4f} MSa/s (zmierzona {fsm/1e6:.4f})"
                f"   nadpróbk. x{st.get('ovs', '?')}   blok {st.get('n', '?')}   obraz {st.get('fps', '?')} kl/s\n"
                f"wyzwalanie CH{st.get('trig', '?')} {'↑' if st.get('edge') == 'R' else '↓'}"
                f"  poziom {self._v(st.get('lvl', '0'))}{' (auto)' if st.get('lauto') == '1' else ''}"
                f"   f = {f/1000:.3f} kHz\n"
                f"enkoder X: {xm}   enkoder Y: {ym} (kanał aktywny CH{st.get('act', '?')})"
                f"   skala: {'pin ADC' if st.get('scale') == 'ADC' else 'wejście BNC'}"
                f"   pomiary: {'CH' + st.get('meas') if st.get('meas', '0') != '0' else 'wył.'}"
                f"   bateria: {(str(round(int(st.get('vbat', '-1')) / 1000, 2)) + ' V, ' + st.get('bat', '?') + ' %') if int(st.get('bat', '-1')) >= 0 else 'brak (USB)'}"))
            self.v_adc.set(st.get("scale") == "ADC")
            self.v_cal1k.set(st.get("cal1k", "1") == "1")
            on = st.get("on", "0000")
            g, cpl, pr = st.get("g", "").split(","), st.get("cpl", "DDDD"), st.get("pr", "").split(",")
            vd, of, vpp = st.get("vdiv", "").split(","), st.get("off", "").split(","), st.get("vpp", "").split(",")
            for i in range(4):
                if on[i] != "1":
                    self.l_ch[i].config(text=f"CH{i+1}: wył.")
                    continue
                act = " ◄ aktywny" if st.get("act") == str(i + 1) else ""
                self.l_ch[i].config(text=(
                    f"CH{i+1} {'DC' if cpl[i] == 'D' else 'AC'} x{pr[i]}{act}\n"
                    f"{self._v(vd[i])}/dz  offset {self._v(of[i])}\n"
                    f"wzmocnienie G{g[i]}   {self._v(vpp[i])}pp"))
        except (ValueError, IndexError, KeyError):
            pass


if __name__ == "__main__":
    Remote().mainloop()
