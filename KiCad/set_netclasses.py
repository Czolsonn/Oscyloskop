"""Sets net classes, track/via presets and basic design rules in Oscyloskop.kicad_pro.
Run with KiCad CLOSED (KiCad rewrites the .kicad_pro when it exits):
    python KiCad/set_netclasses.py
Nets are classified from the schematic netlist, so run it again after re-annotation or new nets.
"""
import json, os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.join(HERE, 'Oscyloskop')
PRO = os.path.join(PRJ, 'Oscyloskop.kicad_pro')
CLI = r'C:\Program Files\KiCad\10.0\bin\kicad-cli.exe'

# name: (track width, clearance, via diameter, via drill, colour)
CLASSES = {
    'Power_In': (0.8, 0.2, 0.8, 0.4, 'rgba(220, 40, 40, 1.000)'),
    'Power':    (0.5, 0.2, 0.8, 0.4, 'rgba(240, 140, 0, 1.000)'),
    'Input':    (0.3, 0.6, 0.6, 0.3, 'rgba(200, 0, 200, 1.000)'),
    'Analog':   (0.25, 0.25, 0.6, 0.3, 'rgba(0, 160, 80, 1.000)'),
}
DEFAULT = (0.25, 0.2, 0.6, 0.3)
POWER_NAMES = {'GND', '+5V', '+3.3V', 'VDDA'}
VMID_PARTS = {'U10', 'R55', 'R56', 'R33', 'C17', 'C19', 'C21', 'C29'}   # VMID buffer on the Power Input sheet


def netlist():
    out = os.path.join(tempfile.gettempdir(), 'osc_netclass.net')
    subprocess.run([CLI, 'sch', 'export', 'netlist', os.path.join(PRJ, 'Oscyloskop.kicad_sch'), '-o', out],
                   check=True, capture_output=True)
    t = open(out, encoding='utf-8').read()
    sheet, val = {}, {}
    for m in re.finditer(r'\(comp\s+\(ref "([^"]+)"\)\s+\(value "([^"]*)"\).*?\(sheetpath\s+\(names "([^"]*)"', t, re.S):
        val[m.group(1)], sheet[m.group(1)] = m.group(2), m.group(3)
    nets = []
    for m in re.finditer(r'\(net\s+\(code "\d+"\)\s+\(name "([^"]*)"\)(.*?)\n\t\t\)', t, re.S):
        nodes = re.findall(r'\(node\s+\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)(?:\s+\(pinfunction "([^"]*)"\))?', m.group(2))
        nets.append((m.group(1), nodes))
    return nets, sheet, val


def classify(name, nodes, sheet, val):
    refs = {r for r, _, _ in nodes}
    pins = {(r, f or p) for r, p, f in nodes}
    if name.startswith('unconnected-'):
        return None
    if any(val.get(r) == 'Conn_Coaxial' and f.startswith('In') for r, f in pins):
        return 'Input'
    if refs & {'J5', 'J6', 'F1'} and name != 'GND' or ('U2', 'VI_3') in pins:
        return 'Power_In'
    if name in POWER_NAMES or ('U2', 'VO_2') in pins or 'R41' in refs:
        return 'Power'
    if refs & VMID_PARTS or name == '1.625V':
        return 'Analog'
    if all(sheet.get(r, '').startswith('/Analog Input') for r in refs):
        return 'Analog'
    if re.fullmatch(r'CH\d', name):   # filter output -> ADC pin of the Nucleo
        return 'Analog'
    return None


def main():
    nets, sheet, val = netlist()
    assign = {}
    for name, nodes in nets:
        c = classify(name, nodes, sheet, val)
        if c:
            assign[name] = c
    d = json.load(open(PRO, encoding='utf-8'))
    ns = d['net_settings']
    base = next(c for c in ns['classes'] if c['name'] == 'Default')
    base.update(track_width=DEFAULT[0], clearance=DEFAULT[1], via_diameter=DEFAULT[2], via_drill=DEFAULT[3])
    classes = [base]
    for i, (n, (w, cl, vd, vdr, col)) in enumerate(CLASSES.items()):
        c = dict(base)
        c.update(name=n, track_width=w, clearance=cl, via_diameter=vd, via_drill=vdr, priority=i, pcb_color=col)
        classes.append(c)
    ns['classes'] = classes
    ns['netclass_patterns'] = [{'netclass': c, 'pattern': n} for n, c in sorted(assign.items(), key=lambda x: (x[1], x[0]))]
    ds = d['board']['design_settings']
    ds['track_widths'] = [0.0, 0.25, 0.3, 0.5, 0.8]
    ds['via_dimensions'] = [{'diameter': 0.0, 'drill': 0.0}, {'diameter': 0.6, 'drill': 0.3}, {'diameter': 0.8, 'drill': 0.4}]
    ds['rules'].update(min_clearance=0.15, min_track_width=0.2, min_via_diameter=0.5, min_through_hole_diameter=0.3,
                       min_copper_edge_clearance=0.5)
    shutil.copy(PRO, PRO + '.bak_netclass_' + time.strftime('%Y%m%d_%H%M%S'))
    json.dump(d, open(PRO, 'w', encoding='utf-8', newline='\n'), indent=2)
    from collections import Counter
    print('net classes set:', dict(Counter(assign.values())), '(other nets: Default)')
    for c in CLASSES:
        print(' ', c, ':', ', '.join(sorted(n for n, k in assign.items() if k == c))[:400])


if __name__ == '__main__':
    if any(os.path.exists(os.path.join(PRJ, f)) for f in ('~Oscyloskop.kicad_pro.lck',)) and '--force' not in sys.argv:
        sys.exit('KiCad seems to be open (lock file found). Close KiCad first, or pass --force.')
    main()
