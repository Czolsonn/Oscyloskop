import subprocess, os, re, time
LT=r'C:\Users\polus\AppData\Local\Programs\ADI\LTspice\LTspice.exe'
SIM=r'C:\Users\polus\OneDrive\Pulpit\Projekty\Oscyloskop\Symulacje'
def rd(name):
    return open(os.path.join(SIM,name),encoding='latin1',newline='').read()
def wr(name,txt):
    open(os.path.join(SIM,name),'w',encoding='latin1',newline='').write(txt)
def run(asc, timeout=600):
    """asc: file name in SIM. returns log text"""
    subprocess.run('taskkill /IM LTspice.exe /F',shell=True,capture_output=True)
    base=asc[:-4]
    for e in ('.net','.log','.raw','.op.raw'):
        p=os.path.join(SIM,base+e)
        if os.path.exists(p): os.remove(p)
    subprocess.run([LT,'-netlist',os.path.join(SIM,asc)],timeout=120)
    assert os.path.exists(os.path.join(SIM,base+'.net')),'netlist failed'
    subprocess.run([LT,'-b',os.path.join(SIM,base+'.net')],timeout=timeout)
    b=open(os.path.join(SIM,base+'.log'),'rb').read()
    if b[:2] in (b'\xff\xfe',) or b[1:2]==b'\x00': t=b.decode('utf-16',errors='ignore')
    else: t=b.decode('latin1')
    return t.replace('\r','')
def meas(log):
    out={}
    for m in re.finditer(r'^(\w+): .*?=\(?([-\d.eE+]+)(dB)?',log,re.M):
        out[m.group(1).lower()]=float(m.group(2))
    return out
