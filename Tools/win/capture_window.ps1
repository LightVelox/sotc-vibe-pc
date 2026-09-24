param(
    [Parameter(Mandatory = $true)][string]$TitleContains,
    [Parameter(Mandatory = $true)][string]$OutFile
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CaptureNative {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

$found = [IntPtr]::Zero
$callback = [CaptureNative+EnumProc]{
    param($hWnd, $lParam)
    if (-not [CaptureNative]::IsWindowVisible($hWnd)) { return $true }
    $sb = New-Object System.Text.StringBuilder 512
    [void][CaptureNative]::GetWindowText($hWnd, $sb, 512)
    if ($sb.ToString().Contains($TitleContains)) { $script:found = $hWnd; return $false }
    return $true
}
[void][CaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
if ($found -eq [IntPtr]::Zero) { Write-Error "window containing '$TitleContains' not found"; exit 1 }

$rect = New-Object CaptureNative+RECT
[void][CaptureNative]::GetClientRect($found, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
[void][CaptureNative]::PrintWindow($found, $hdc, 3)
$graphics.ReleaseHdc($hdc)
$graphics.Dispose()
$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "captured ${width}x${height} -> $OutFile"
