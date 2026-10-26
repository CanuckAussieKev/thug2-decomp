param([Parameter(Mandatory = $true)][int]$ProcessId, [int]$Samples = 20, [int]$IntervalMs = 250)
# The Windows audio session(s) of a running game on the default playback device: mute, volume and the peak meter
# (what it actually plays, before the session's mute - so a muted game still shows a peak while it makes sound).
# Prints "session pid=<n> muted=<True|False> volume=<0..1> peak max=<0..1>" per session of that process, or
# "NO SESSION" (it hasn't opened any sound yet). Read only: it changes nothing. Used to check [features] mute_audio
# (src/game/sfx/mute_audio.h) in the workspaces.
if (-not ('CoreAudioQuery' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}

[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    int NotImpl1();
    [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
}
[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
}
[Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionManager2 {
    int NotImpl1(); int NotImpl2();
    [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
}
[Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionEnumerator {
    [PreserveSig] int GetCount(out int count);
    [PreserveSig] int GetSession(int index, out IAudioSessionControl2 session);
}
[Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioSessionControl2 {
    int N1(); int N2(); int N3(); int N4(); int N5(); int N6(); int N7(); int N8(); int N9();
    int N10(); int N11();
    [PreserveSig] int GetProcessId(out uint pid);
}
[Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface ISimpleAudioVolume {
    int N1();
    [PreserveSig] int GetMasterVolume(out float level);
    int N3();
    [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool muted);
}
[Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioMeterInformation {
    [PreserveSig] int GetPeakValue(out float peak);
}

public static class CoreAudioQuery {
    public static List<string> Query(uint pid, int samples, int intervalMs) {
        var result = new List<string>();
        var devices = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDevice device;
        if (devices.GetDefaultAudioEndpoint(0, 0, out device) != 0) { result.Add("NO DEVICE"); return result; }
        Guid iid = typeof(IAudioSessionManager2).GUID;
        object o;
        device.Activate(ref iid, 23, IntPtr.Zero, out o);
        IAudioSessionEnumerator sessions;
        ((IAudioSessionManager2)o).GetSessionEnumerator(out sessions);
        int count;
        sessions.GetCount(out count);
        for (int i = 0; i < count; i++) {
            IAudioSessionControl2 s;
            sessions.GetSession(i, out s);
            uint p;
            s.GetProcessId(out p);
            if (p != pid) continue;
            var volume = (ISimpleAudioVolume)s;
            var meter = (IAudioMeterInformation)s;
            bool muted; float level;
            volume.GetMute(out muted);
            volume.GetMasterVolume(out level);
            float max = 0;
            for (int n = 0; n < samples; n++) {
                float peak; meter.GetPeakValue(out peak);
                if (peak > max) max = peak;
                Thread.Sleep(intervalMs);
            }
            result.Add(String.Format("session pid={0} muted={1} volume={2:0.00} peak max={3:0.000}", p, muted, level, max));
        }
        if (result.Count == 0) result.Add("NO SESSION for pid " + pid);
        return result;
    }
}
"@
}
[CoreAudioQuery]::Query([uint32]$ProcessId, $Samples, $IntervalMs)
