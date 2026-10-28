// Own one trusted test request and its ordinary process descendants. Out-of-process
// service/COM/task activation is outside this protocol and must not be requested.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace TermForgeVmRequest {
    public sealed class Member {
        public uint Pid;
        public bool IsRoot, ExactJobMembership, Exited;
        public uint Wait, ExitCode;
        public string CreationUtc, Image, QueryError;
        internal IntPtr Handle;
    }
    public sealed class Result {
        public string Schema = "termforge-owned-vm-request-v2", Status = "failed", Error, StartedUtc, EndedUtc;
        public uint RootPid, RootWait, RootExitCode, JobBeforeActive, JobBeforeTotal, JobAfterActive;
        public string RootImage, RootCreationUtc;
        public int ExecutionTimeoutMilliseconds;
        public long ExecutionElapsedMilliseconds;
        public bool CreatedSuspended, RootHandleIdentityVerified, RootImageVerified, RootAssignedBeforeResume, KillOnCloseVerified;
        public bool BreakawayDenied, TimedOut, TerminationRequested, RootReaped, MembersStable, CleanupVerified;
        public List<Member> Members = new List<Member>();
    }
    public static class Supervisor {
        const uint WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258, KILL_ON_CLOSE = 0x2000;
        const uint QUERY_LIMITED = 0x1000, SYNCHRONIZE = 0x100000;
        static readonly IntPtr InvalidHandle = new IntPtr(-1);
        [StructLayout(LayoutKind.Sequential)] struct Security { public int Length; public IntPtr Descriptor; public int Inherit; }
        [StructLayout(LayoutKind.Sequential)] struct Startup {
            public uint Size; public IntPtr Reserved, Desktop, Title;
            public uint X, Y, XSize, YSize, XChars, YChars, Fill, Flags;
            public ushort Show, ReservedSize; public IntPtr ReservedBytes, Input, Output, Error;
        }
        [StructLayout(LayoutKind.Sequential)] struct StartupEx { public Startup Startup; public IntPtr Attributes; }
        [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr Process, Thread; public uint Pid, Tid; }
        [StructLayout(LayoutKind.Sequential)] struct BasicLimits {
            public long ProcessTime, JobTime; public uint Flags; public UIntPtr MinWorkingSet, MaxWorkingSet;
            public uint ActiveLimit; public UIntPtr Affinity; public uint Priority, Scheduling;
        }
        [StructLayout(LayoutKind.Sequential)] struct IoCounters { public ulong A, B, C, D, E, F; }
        [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {
            public BasicLimits Basic; public IoCounters Io;
            public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory;
        }
        [StructLayout(LayoutKind.Sequential)] struct Accounting {
            public long User, Kernel, PeriodUser, PeriodKernel;
            public uint PageFaults, Total, Active, Terminated;
        }
        [StructLayout(LayoutKind.Sequential)] struct FileTime { public uint Low, High; }
        [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job, int kind, ref ExtendedLimits data, uint size);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job, int kind, IntPtr data, uint size, IntPtr returned);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool member);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateJobObject(IntPtr job, uint code);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint GetProcessId(IntPtr process);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr process, out FileTime created, out FileTime exited, out FileTime kernel, out FileTime user);
        [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern bool QueryFullProcessImageNameW(IntPtr process, uint flags, StringBuilder name, ref uint size);
        [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern IntPtr CreateFileW(string path, uint access, uint sharing, ref Security security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool InitializeProcThreadAttributeList(IntPtr attributes, int count, int flags, ref IntPtr size);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool UpdateProcThreadAttribute(IntPtr attributes, uint flags, IntPtr kind, IntPtr value, IntPtr size, IntPtr previous, IntPtr returned);
        [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr attributes);
        [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] static extern bool CreateProcessW(string application, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes, bool inherit, uint flags, IntPtr environment, string directory, ref StartupEx startup, out ProcessInfo info);
        static void Check(bool ok) { if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error()); }
        static void Close(ref IntPtr h) { if (h != IntPtr.Zero && h != InvalidHandle) CloseHandle(h); h = IntPtr.Zero; }
        static string Image(IntPtr handle) { var text = new StringBuilder(32768); uint size = (uint)text.Capacity; Check(QueryFullProcessImageNameW(handle, 0, text, ref size)); return text.ToString(); }
        static string Created(IntPtr handle) { FileTime c,e,k,u; Check(GetProcessTimes(handle,out c,out e,out k,out u)); return DateTime.FromFileTimeUtc((long)(((ulong)c.High << 32) | c.Low)).ToString("o"); }
        static T JobInfo<T>(IntPtr job, int kind) {
            IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(T)));
            try { Check(QueryInformationJobObject(job,kind,p,(uint)Marshal.SizeOf(typeof(T)),IntPtr.Zero)); return (T)Marshal.PtrToStructure(p,typeof(T)); }
            finally { Marshal.FreeHGlobal(p); }
        }
        static uint[] ProcessIds(IntPtr job) {
            int bytes = 8 + IntPtr.Size * 256; IntPtr p = Marshal.AllocHGlobal(bytes);
            try {
                Check(QueryInformationJobObject(job,3,p,(uint)bytes,IntPtr.Zero));
                int assigned=Marshal.ReadInt32(p), listed=Marshal.ReadInt32(p,4);
                if (assigned != listed || listed < 0 || listed > 256) throw new InvalidOperationException("Incomplete owned Job PID list");
                var ids=new uint[listed]; for(int i=0;i<listed;i++) ids[i]=checked((uint)Marshal.ReadIntPtr(p,8+i*IntPtr.Size).ToInt64()); Array.Sort(ids); return ids;
            } finally { Marshal.FreeHGlobal(p); }
        }
        static void ObserveMembers(IntPtr job, Result r) {
            uint[] ids=ProcessIds(job);
            foreach(uint pid in ids) {
                var m=new Member { Pid=pid, IsRoot=pid==r.RootPid }; r.Members.Add(m);
                try {
                    m.Handle=OpenProcess(QUERY_LIMITED|SYNCHRONIZE,false,pid); Check(m.Handle!=IntPtr.Zero);
                    if(GetProcessId(m.Handle)!=pid) throw new InvalidOperationException("PID differs from retained process handle");
                    bool member; Check(IsProcessInJob(m.Handle,job,out member)); m.ExactJobMembership=member;
                    if(!member) throw new InvalidOperationException("PID handle is not in this owned Job");
                    m.Image=Image(m.Handle); m.CreationUtc=Created(m.Handle);
                } catch(Exception error) { m.QueryError=error.Message; }
            }
            uint[] again=ProcessIds(job); r.MembersStable=ids.Length==again.Length;
            for(int i=0;r.MembersStable && i<ids.Length;i++) r.MembersStable=ids[i]==again[i];
        }
        public static Result Run(string executable, string script, string stdout, string stderr, int timeoutMilliseconds) {
            if(!Environment.Is64BitProcess || timeoutMilliseconds<1000 || timeoutMilliseconds>300000) throw new ArgumentException("Requires x64 and a 1..300 second execution deadline");
            executable=Path.GetFullPath(executable); script=Path.GetFullPath(script);
            if(!File.Exists(executable) || !File.Exists(script)) throw new FileNotFoundException("Owned request input missing");
            var r=new Result { ExecutionTimeoutMilliseconds=timeoutMilliseconds };
            IntPtr job=IntPtr.Zero, input=IntPtr.Zero, output=IntPtr.Zero, errorHandle=IntPtr.Zero, attributes=IntPtr.Zero, handles=IntPtr.Zero;
            bool attributesReady=false, assigned=false; ProcessInfo process=new ProcessInfo();
            try {
                job=CreateJobObjectW(IntPtr.Zero,null); Check(job!=IntPtr.Zero);
                var limits=new ExtendedLimits(); limits.Basic.Flags=KILL_ON_CLOSE;
                Check(SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(ExtendedLimits))));
                var actual=JobInfo<ExtendedLimits>(job,9); r.KillOnCloseVerified=(actual.Basic.Flags&KILL_ON_CLOSE)!=0; r.BreakawayDenied=(actual.Basic.Flags&0x1800)==0;
                if(!r.KillOnCloseVerified || !r.BreakawayDenied) throw new InvalidOperationException("Owned Job limits did not round-trip");
                var security=new Security { Length=Marshal.SizeOf(typeof(Security)), Inherit=1 };
                input=CreateFileW("NUL",0x80000000,3,ref security,3,0,IntPtr.Zero); Check(input!=InvalidHandle);
                output=CreateFileW(stdout,0x40000000,3,ref security,2,0,IntPtr.Zero); Check(output!=InvalidHandle);
                errorHandle=CreateFileW(stderr,0x40000000,3,ref security,2,0,IntPtr.Zero); Check(errorHandle!=InvalidHandle);
                IntPtr size=IntPtr.Zero; InitializeProcThreadAttributeList(IntPtr.Zero,1,0,ref size);
                attributes=Marshal.AllocHGlobal(size); Check(InitializeProcThreadAttributeList(attributes,1,0,ref size)); attributesReady=true;
                handles=Marshal.AllocHGlobal(3*IntPtr.Size); Marshal.WriteIntPtr(handles,0,input); Marshal.WriteIntPtr(handles,IntPtr.Size,output); Marshal.WriteIntPtr(handles,2*IntPtr.Size,errorHandle);
                Check(UpdateProcThreadAttribute(attributes,0,new IntPtr(0x20002),handles,new IntPtr(3*IntPtr.Size),IntPtr.Zero,IntPtr.Zero));
                var startup=new StartupEx(); startup.Startup.Size=(uint)Marshal.SizeOf(typeof(StartupEx)); startup.Startup.Flags=0x101; startup.Startup.Show=0;
                startup.Startup.Input=input; startup.Startup.Output=output; startup.Startup.Error=errorHandle; startup.Attributes=attributes;
                var command=new StringBuilder("\""+executable+"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy RemoteSigned -File \""+script+"\"");
                Check(CreateProcessW(executable,command,IntPtr.Zero,IntPtr.Zero,true,0x08080004,IntPtr.Zero,Path.GetDirectoryName(script),ref startup,out process));
                r.CreatedSuspended=true; r.RootPid=process.Pid; r.RootImage=Image(process.Process); r.RootCreationUtc=Created(process.Process);
                r.RootHandleIdentityVerified=GetProcessId(process.Process)==process.Pid;
                r.RootImageVerified=String.Equals(r.RootImage,executable,StringComparison.OrdinalIgnoreCase);
                if(!r.RootImageVerified || !r.RootHandleIdentityVerified) throw new InvalidOperationException("Created root image/handle identity differs");
                Check(AssignProcessToJobObject(job,process.Process)); assigned=true;
                bool rootMember; Check(IsProcessInJob(process.Process,job,out rootMember)); r.RootAssignedBeforeResume=rootMember;
                if(!rootMember) throw new InvalidOperationException("Suspended root was not in its owned Job");
                Close(ref input); Close(ref output); Close(ref errorHandle);
                var execution=Stopwatch.StartNew(); r.StartedUtc=DateTime.UtcNow.ToString("o");
                Check(ResumeThread(process.Thread)!=0xFFFFFFFF); Close(ref process.Thread);
                uint waited=WaitForSingleObject(process.Process,(uint)timeoutMilliseconds);
                r.TimedOut=waited==WAIT_TIMEOUT;
                if(waited!=WAIT_OBJECT_0 && !r.TimedOut) throw new Win32Exception(Marshal.GetLastWin32Error(),"Owned root wait failed");
                Accounting before=JobInfo<Accounting>(job,1);
                // A root may exit just before its console host or ordinary children.
                // Drain only inside the original execution deadline, never beyond it.
                while(!r.TimedOut && before.Active!=0 && execution.ElapsedMilliseconds<timeoutMilliseconds) {
                    Thread.Sleep(10); before=JobInfo<Accounting>(job,1);
                }
                if(before.Active!=0 && execution.ElapsedMilliseconds>=timeoutMilliseconds) r.TimedOut=true;
                r.ExecutionElapsedMilliseconds=execution.ElapsedMilliseconds;
                r.JobBeforeActive=before.Active; r.JobBeforeTotal=before.Total;
                if(r.TimedOut || before.Active!=0) {
                    ObserveMembers(job,r); r.TerminationRequested=true; Check(TerminateJobObject(job,124));
                    r.Status=r.TimedOut?"timed_out":"failed_lingering_children";
                } else { r.MembersStable=true; }
            } catch(Exception error) { r.Error=error.Message; r.Status="failed"; }
            finally {
                // Direct handles originate from this CreateProcess call; no name-based termination.
                if(process.Process!=IntPtr.Zero) {
                    try {
                        if(WaitForSingleObject(process.Process,0)!=WAIT_OBJECT_0 || (assigned && JobInfo<Accounting>(job,1).Active!=0)) {
                            r.TerminationRequested=true;
                            if(assigned) Check(TerminateJobObject(job,124)); else Check(TerminateProcess(process.Process,125));
                        }
                        var cleanup=Stopwatch.StartNew();
                        r.RootWait=WaitForSingleObject(process.Process,5000); r.RootReaped=r.RootWait==WAIT_OBJECT_0;
                        Check(GetExitCodeProcess(process.Process,out r.RootExitCode));
                        while(assigned && JobInfo<Accounting>(job,1).Active!=0 && cleanup.ElapsedMilliseconds<5000) Thread.Sleep(10);
                        r.JobAfterActive=assigned?JobInfo<Accounting>(job,1).Active:0;
                        bool members=true;
                        foreach(Member m in r.Members) {
                            if(m.Handle==IntPtr.Zero) { members=false; continue; }
                            uint remaining=(uint)Math.Max(0,5000-cleanup.ElapsedMilliseconds);
                            m.Wait=WaitForSingleObject(m.Handle,remaining); m.Exited=m.Wait==WAIT_OBJECT_0;
                            if(!GetExitCodeProcess(m.Handle,out m.ExitCode)) { m.QueryError="GetExitCodeProcess failed"; members=false; }
                            members=members && m.ExactJobMembership && m.Exited && m.QueryError==null;
                        }
                        r.CleanupVerified=r.RootReaped && r.JobAfterActive==0 && members && r.MembersStable;
                        if(r.Error==null && !r.TimedOut && !r.TerminationRequested) r.Status=r.RootExitCode==0?"passed":"failed";
                    } catch(Exception error) { r.Error=(r.Error==null?"":r.Error+"; ")+"cleanup: "+error.Message; }
                } else { r.CleanupVerified=true; }
                foreach(Member m in r.Members) Close(ref m.Handle);
                Close(ref process.Thread); Close(ref process.Process);
                Close(ref job); // KILL_ON_JOB_CLOSE is a final backstop, including runner failure.
                Close(ref input); Close(ref output); Close(ref errorHandle);
                if(attributesReady) DeleteProcThreadAttributeList(attributes);
                if(attributes!=IntPtr.Zero) Marshal.FreeHGlobal(attributes);
                if(handles!=IntPtr.Zero) Marshal.FreeHGlobal(handles);
                r.EndedUtc=DateTime.UtcNow.ToString("o");
                if(!r.CleanupVerified) r.Status="cleanup_failed";
            }
            return r;
        }
    }
}
