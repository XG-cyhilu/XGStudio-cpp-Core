$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CredRead4 {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct CREDENTIAL {
        public uint Flags; public uint Type; public string TargetName; public string Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint CredentialBlobSize; public IntPtr CredentialBlob; public uint Persist;
        public uint AttributeCount; public IntPtr Attributes; public string TargetAlias; public string UserName;
    }
    [DllImport("advapi32.dll", SetLastError=true, CharSet=CharSet.Unicode, EntryPoint="CredReadW")]
    public static extern bool CredRead(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll", SetLastError=true)] public static extern void CredFree(IntPtr cred);
    public static string Get(string target) {
        IntPtr p;
        if (!CredRead(target, 1, 0, out p)) throw new Exception("miss");
        CREDENTIAL c = (CREDENTIAL)Marshal.PtrToStructure(p, typeof(CREDENTIAL));
        byte[] b = new byte[c.CredentialBlobSize];
        Marshal.Copy(c.CredentialBlob, b, 0, (int)c.CredentialBlobSize);
        CredFree(p);
        return System.Text.Encoding.UTF8.GetString(b);
    }
}
'@
$g = 'D:\XGstudio\.tools\PortableGit\cmd\git.exe'
$r = 'D:\XGstudio\cpploclib'
$tok = [CredRead4]::Get('gh:github.com:XG-cyhilu')
& $g -C $r add -A
& $g -C $r -c user.name='XG-cyhilu' -c user.email='3923421431@qq.com' commit -q -m '重命名为 XGStudio-cpp-Core，更新 homepage 与 README 标题'
$b64 = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("x-access-token:$tok"))
& $g -C $r -c http.sslVerify=false -c "http.extraHeader=Authorization: Basic $b64" -c credential.helper= push origin main 2>&1 | Select-Object -Last 4
& $g -C $r log --oneline -2
