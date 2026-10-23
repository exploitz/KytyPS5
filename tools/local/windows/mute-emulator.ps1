param([int]$Seconds = 0, [switch]$Unmute)
# Mutes the emulator's audio in Windows (its session in the volume mixer), so unattended tests make no sound while
# the PC is in use for something else. The emulator still mixes and submits its audio exactly as before: the mute is
# Windows-side, so measurements are unchanged. -Seconds N: keep watching N seconds and mute every new emulator
# process (each test boots one). -Unmute: the reverse. Local tool.
$code = @'
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumerator {}
[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator { int EnumAudioEndpoints(int f, int s, out IntPtr c); int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice dev); }
[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice { int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o); }
[Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 { int GetAudioSessionControl(IntPtr g, int s, out IntPtr c); int GetSimpleAudioVolume(IntPtr g, int s, out IntPtr v); int GetSessionEnumerator(out IAudioSessionEnumerator e); }
[Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator { int GetCount(out int n); int GetSession(int i, out IAudioSessionControl2 s); }
[Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
  int GetState(out int s); int GetDisplayName(out IntPtr n); int SetDisplayName(IntPtr n, IntPtr c); int GetIconPath(out IntPtr p); int SetIconPath(IntPtr p, IntPtr c);
  int GetGroupingParam(out Guid g); int SetGroupingParam(IntPtr g, IntPtr c); int RegisterAudioSessionNotification(IntPtr n); int UnregisterAudioSessionNotification(IntPtr n);
  int GetSessionIdentifier(out IntPtr id); int GetSessionInstanceIdentifier(out IntPtr id); int GetProcessId(out uint pid); int IsSystemSoundsSession(); int SetDuckingPreference(bool o);
}
[Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ISimpleAudioVolume { int SetMasterVolume(float v, IntPtr c); int GetMasterVolume(out float v); int SetMute(bool m, IntPtr c); int GetMute(out bool m); }
public static class Mixer {
  public static int Mute(uint[] pids, bool mute) {
    var en = (IMMDeviceEnumerator)new MMDeviceEnumerator(); IMMDevice dev; en.GetDefaultAudioEndpoint(0, 1, out dev);
    var iid = typeof(IAudioSessionManager2).GUID; object o; dev.Activate(ref iid, 23, IntPtr.Zero, out o);
    IAudioSessionEnumerator se; ((IAudioSessionManager2)o).GetSessionEnumerator(out se); int n; se.GetCount(out n); int done = 0;
    for (int i = 0; i < n; i++) { IAudioSessionControl2 s; se.GetSession(i, out s); uint pid; s.GetProcessId(out pid);
      if (Array.IndexOf(pids, pid) < 0) continue; ((ISimpleAudioVolume)s).SetMute(mute, IntPtr.Zero); done++; }
    return done;
  }
}
'@
Add-Type -TypeDefinition $code
$deadline = (Get-Date).AddSeconds($Seconds)
do {
	$pids = @(Get-Process kyty_emulator -ErrorAction SilentlyContinue | ForEach-Object { [uint32]$_.Id })
	if ($pids.Count) { $n = [Mixer]::Mute($pids, -not $Unmute); if ($n) { "{0} {1} session(s) of {2}" -f $(if ($Unmute) { 'unmuted' } else { 'muted' }), $n, ($pids -join ',') } }
	if ($Seconds) { Start-Sleep 5 }
} while ((Get-Date) -lt $deadline)
