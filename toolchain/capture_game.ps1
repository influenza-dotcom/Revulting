# Launch NetHackV, optionally send keys, and capture ONLY the game's own window
# (never the whole desktop).
param(
    [string]$Exe   = "C:\Users\dalla\Vulture\VultureNetHack\NetHackV-console.exe",
    [string]$Shot  = "C:\Users\dalla\Vulture\shots\shot.png",
    [int]$WaitSec  = 16,
    [string[]]$Keys = @(),
    [int]$KeyDelayMs = 900,
    [switch]$KeepOpen,
    [int]$AttachPid = 0
)

Add-Type -AssemblyName System.Windows.Forms, System.Drawing

Add-Type @"
using System;using System.Text;using System.Collections.Generic;
using System.Runtime.InteropServices;using System.Drawing;
public class Win {
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern int  GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll")] static extern int  GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SetActiveWindow(IntPtr h);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);

  // Windows blocks SetForegroundWindow from background processes unless the
  // caller shares an input queue with the target - attach, then set.
  public static bool Focus(IntPtr h) {
    uint target = GetWindowThreadProcessId(h, IntPtr.Zero);
    uint self   = GetCurrentThreadId();
    AttachThreadInput(self, target, true);
    ShowWindow(h, 5);
    SetForegroundWindow(h);
    SetActiveWindow(h);
    SetFocus(h);
    AttachThreadInput(self, target, false);
    return GetForegroundWindow() == h;
  }

  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    System.Threading.Thread.Sleep(120);
    mouse_event(0x0002, 0, 0, 0, IntPtr.Zero);   // LEFTDOWN
    System.Threading.Thread.Sleep(60);
    mouse_event(0x0004, 0, 0, 0, IntPtr.Zero);   // LEFTUP
  }

  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);

  // SDL2 needs a real scancode on the message, not just the virtual key
  public static void Key(byte vk) {
    byte scan = (byte) MapVirtualKey(vk, 0);      // MAPVK_VK_TO_VSC
    keybd_event(vk, scan, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    keybd_event(vk, scan, 2, IntPtr.Zero);        // KEYEVENTF_KEYUP
  }

  public static void ShiftKey(byte vk) {
    byte sh = (byte) MapVirtualKey(0x10, 0);      // VK_SHIFT
    byte scan = (byte) MapVirtualKey(vk, 0);
    keybd_event(0x10, sh, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(vk, scan, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    keybd_event(vk, scan, 2, IntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(0x10, sh, 2, IntPtr.Zero);
  }
  public static void CtrlKey(byte vk) {
    byte ct = (byte) MapVirtualKey(0x11, 0);      // VK_CONTROL
    byte scan = (byte) MapVirtualKey(vk, 0);
    keybd_event(0x11, ct, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(vk, scan, 0, IntPtr.Zero);
    System.Threading.Thread.Sleep(60);
    keybd_event(vk, scan, 2, IntPtr.Zero);
    System.Threading.Thread.Sleep(40);
    keybd_event(0x11, ct, 2, IntPtr.Zero);
  }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  delegate bool EnumProc(IntPtr h, IntPtr l);

  public static IntPtr GameWindow(int pid) {
    IntPtr best = IntPtr.Zero; int bestArea = 0;
    EnumWindows((h,l) => {
      int p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        RECT r; GetWindowRect(h, out r);
        int a = (r.R-r.L)*(r.B-r.T);
        if (a > bestArea) { bestArea = a; best = h; }
      }
      return true;
    }, IntPtr.Zero);
    return best;
  }
  public static string Title(IntPtr h){ var sb=new StringBuilder(300); GetWindowText(h,sb,300); return sb.ToString(); }

  // PrintWindow with PW_RENDERFULLCONTENT (2) works for most GPU-composited windows
  public static Bitmap Grab(IntPtr h) {
    RECT r; GetWindowRect(h, out r);
    int w = r.R-r.L, ht = r.B-r.T;
    if (w <= 0 || ht <= 0) return null;
    var bmp = new Bitmap(w, ht);
    using (var g = Graphics.FromImage(bmp)) {
      IntPtr dc = g.GetHdc();
      PrintWindow(h, dc, 2);
      g.ReleaseHdc(dc);
    }
    return bmp;
  }
  // fallback: copy the screen region the window occupies
  public static Bitmap GrabRegion(IntPtr h) {
    RECT r; GetWindowRect(h, out r);
    int w = r.R-r.L, ht = r.B-r.T;
    if (w <= 0 || ht <= 0) return null;
    var bmp = new Bitmap(w, ht);
    using (var g = Graphics.FromImage(bmp))
      g.CopyFromScreen(new Point(r.L, r.T), Point.Empty, new Size(w, ht));
    return bmp;
  }
}
"@ -ReferencedAssemblies System.Drawing, System.Windows.Forms -ErrorAction Stop

$dir = Split-Path $Shot -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

if ($AttachPid -gt 0) {
    $p = Get-Process -Id $AttachPid -ErrorAction Stop
} else {
    $work = Split-Path $Exe -Parent
    Remove-Item "$work\vulture_log.txt" -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $Exe -WorkingDirectory $work -PassThru
    Start-Sleep -Seconds $WaitSec
}

if ($p.HasExited) { Write-Output "PROCESS EXITED before capture (code=$($p.ExitCode))"; exit 1 }

$h = [Win]::GameWindow($p.Id)
if ($h -eq [IntPtr]::Zero) { Write-Output "NO VISIBLE WINDOW for pid $($p.Id)"; if(-not $KeepOpen){Stop-Process -Id $p.Id -Force}; exit 1 }
Write-Output "window handle=$h  title='$([Win]::Title($h))'"

$focused = [Win]::Focus($h)
Write-Output "focused=$focused"
Start-Sleep -Milliseconds 700

# Keys are given as single chars, or VK_ names like "ENTER"/"ESC"/"SPACE".
$vk = @{ "ENTER"=0x0D; "ESC"=0x1B; "SPACE"=0x20; "TAB"=0x09; "BKSP"=0x08;
         "UP"=0x26; "DOWN"=0x28; "LEFT"=0x25; "RIGHT"=0x27;
         "F9"=0x78; "F10"=0x79; "F11"=0x7A; "F12"=0x7B }
foreach ($k in $Keys) {
    if ($k -like "CLICK:*") {
        $xy = $k.Substring(6).Split(",")
        [Win]::Click([int]$xy[0], [int]$xy[1])
    } elseif ($k -like "MOVE:*") {
        # Park the cursor WITHOUT clicking.  The level view scrolls whenever the
        # mouse rests in a ~20px screen-edge hotspot, which silently walks the
        # viewport off the map and makes captures look broken - always park
        # centre-screen before grabbing a shot.
        $xy = $k.Substring(5).Split(",")
        [Win]::SetCursorPos([int]$xy[0], [int]$xy[1]) | Out-Null
    } elseif ($k -like "TYPE:*") {
        foreach ($ch in $k.Substring(5).ToCharArray()) {
            [Win]::Key([byte][char]([string]$ch).ToUpper())
            Start-Sleep -Milliseconds 90
        }
    } elseif ($k -like "WAIT:*") {
        Start-Sleep -Milliseconds ([int]$k.Substring(5))
    } elseif ($k -like "SHIFT:*") {
        [Win]::ShiftKey([byte][char]($k.Substring(6)).ToUpper())
    } elseif ($k -like "CTRL:*") {
        [Win]::CtrlKey([byte][char]($k.Substring(5)).ToUpper())
    } elseif ($vk.ContainsKey($k)) {
        [Win]::Key([byte]$vk[$k])
    } elseif ($k.Length -eq 1) {
        # letters/digits: VK code is the uppercase ASCII value
        [Win]::Key([byte][char]([string]$k).ToUpper())
    } else {
        [System.Windows.Forms.SendKeys]::SendWait($k)
    }
    Start-Sleep -Milliseconds $KeyDelayMs
}
Start-Sleep -Milliseconds 900

$bmp = [Win]::Grab($h)
# PrintWindow returns an all-black image for some GPU surfaces; detect and fall back
$blank = $true
if ($bmp) {
    for ($y = 0; $y -lt $bmp.Height -and $blank; $y += 37) {
        for ($x = 0; $x -lt $bmp.Width -and $blank; $x += 37) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.R -gt 12 -or $c.G -gt 12 -or $c.B -gt 12) { $blank = $false }
        }
    }
}
if ($blank) {
    Write-Output "(PrintWindow gave a blank surface - using window-region capture)"
    if ($bmp) { $bmp.Dispose() }
    [Win]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 700
    $bmp = [Win]::GrabRegion($h)
}
$bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "captured: $Shot ($($bmp.Width)x$($bmp.Height), $((Get-Item $Shot).Length) bytes)"
$bmp.Dispose()

if ($KeepOpen) { Write-Output "left running, pid=$($p.Id)" }
else { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
