# Opens a .raw in LTspice (its .plt with the same name is applied) and saves a PNG of the LTspice window.
param([string]$raw, [string]$png, [int]$w = 1500, [int]$h = 950, [int]$wait = 6)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W {
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
 [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 public struct RECT { public int L, T, R, B; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
Get-Process LTspice -ErrorAction SilentlyContinue | Stop-Process -Force
$p = Start-Process "C:\Users\polus\AppData\Local\Programs\ADI\LTspice\LTspice.exe" -ArgumentList "`"$raw`"" -PassThru
$t = 0; while ($p.MainWindowHandle -eq 0 -and $t -lt 40) { Start-Sleep -Milliseconds 250; $p.Refresh(); $t++ }
$hw = $p.MainWindowHandle
[W]::ShowWindow($hw, 1) | Out-Null
[W]::MoveWindow($hw, 0, 0, $w, $h, $true) | Out-Null
Start-Sleep -Seconds $wait
$r = New-Object W+RECT; [W]::GetWindowRect($hw, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
[W]::PrintWindow($hw, $dc, 2) | Out-Null
$g.ReleaseHdc($dc); $g.Dispose(); $bmp.Save($png); $bmp.Dispose()
Stop-Process -Id $p.Id -Force
"saved $png"
