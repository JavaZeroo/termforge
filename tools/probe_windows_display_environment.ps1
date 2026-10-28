$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Collections.Generic;public static class TFDisplays {
 [StructLayout(LayoutKind.Sequential)] public struct RECT{public int L,T,R,B;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] public struct INFO{public int Size;public RECT Monitor,Work;public uint Flags;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string Device;}
 public delegate bool CB(IntPtr m,IntPtr d,ref RECT r,IntPtr p);
 [DllImport("user32.dll")]public static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,CB callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern bool GetMonitorInfo(IntPtr h,ref INFO i);
 [DllImport("shcore.dll")]public static extern int GetDpiForMonitor(IntPtr h,int type,out uint x,out uint y);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr hwnd);
 [DllImport("user32.dll")]public static extern IntPtr MonitorFromWindow(IntPtr hwnd,uint flags);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr hwnd,int command);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr hwnd,out RECT rect);
 public static string WindowDevice(IntPtr hwnd){var i=new INFO();i.Size=Marshal.SizeOf(i);if(!GetMonitorInfo(MonitorFromWindow(hwnd,2),ref i))throw new InvalidOperationException("GetMonitorInfo failed");return i.Device;}
 public static RECT WindowRect(IntPtr hwnd){RECT r;if(!GetWindowRect(hwnd,out r))throw new InvalidOperationException("GetWindowRect failed");return r;}
 public static object[] Read(){var rows=new List<object>();CB cb=delegate(IntPtr h,IntPtr dc,ref RECT r,IntPtr p){var i=new INFO();i.Size=Marshal.SizeOf(i);GetMonitorInfo(h,ref i);uint x,y;int hr=GetDpiForMonitor(h,0,out x,out y);rows.Add(new{device=i.Device,left=i.Monitor.L,top=i.Monitor.T,right=i.Monitor.R,bottom=i.Monitor.B,primary=(i.Flags&1)!=0,dpi_x=x,dpi_y=y,dpi_hr=hr});return true;};EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,cb,IntPtr.Zero);return rows.ToArray();}
}
'@
[TFDisplays]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
Add-Type -AssemblyName System.Windows.Forms
$Displays=@([TFDisplays]::Read())
$Form=New-Object Windows.Forms.Form
$Form.Text='TermForge R2 isolated actual DPI probe';$Form.Width=640;$Form.Height=400;$Form.StartPosition='Manual'
$Samples=@()
try{
 foreach($M in @($Displays)+@($Displays[0])){
  $Form.Location=New-Object Drawing.Point(($M.left+40),($M.top+40));$Form.Show();[TFDisplays]::ShowWindow($Form.Handle,5) | Out-Null;[Windows.Forms.Application]::DoEvents();Start-Sleep -Milliseconds 700;[Windows.Forms.Application]::DoEvents()
  $Actual=[TFDisplays]::WindowDevice($Form.Handle);$Rect=[TFDisplays]::WindowRect($Form.Handle)
  $Samples+=@{device=$M.device;actual_device=$Actual;target_matches=($Actual -eq $M.device);window_dpi=[TFDisplays]::GetDpiForWindow($Form.Handle);window_rect=@($Rect.L,$Rect.T,$Rect.R,$Rect.B);visible=[TFDisplays]::IsWindowVisible($Form.Handle);session_id=(Get-Process -Id $PID).SessionId;time_utc=[DateTime]::UtcNow.ToString('o')}
 }
}finally{$Form.Close();$Form.Dispose()}
@{schema='termforge-actual-display-probe-v1';time_utc=[DateTime]::UtcNow.ToString('o');session_id=(Get-Process -Id $PID).SessionId;monitors=$Displays;window_samples=$Samples;distinct_actual_dpi=(@($Samples.window_dpi | Sort-Object -Unique).Count -gt 1);limitation='Infrastructure probe only; full issue31 TermForge behaviors require separate product evidence'} | ConvertTo-Json -Depth 8
