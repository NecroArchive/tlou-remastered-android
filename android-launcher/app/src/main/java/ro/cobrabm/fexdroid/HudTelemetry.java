package ro.cobrabm.fexdroid;

import android.app.ActivityManager;
import android.content.Context;
import android.system.Os;
import android.system.OsConstants;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.Locale;

/**
 * Low-frequency /proc telemetry for the actual TLOU child process.
 * Values reflect process CPU time and RSS, not fabricated GPU/VRAM figures.
 * Missing or restricted procfs entries are omitted from the HUD.
 */
final class HudTelemetry {
    private final long ticksPerSecond;
    private final File pidFile;
    private final Context context;
    private String cpuSourceState="";
    private int sampledPid=-1;
    private final java.util.concurrent.atomic.AtomicBoolean resetPending=new java.util.concurrent.atomic.AtomicBoolean();
    private long previousTicks=-1L, previousAt;
    private volatile double cpuPercent=Double.NaN;
    private volatile double memoryGiB=Double.NaN, deviceGiB=Double.NaN;
    private volatile double gpuPercent=Double.NaN;
    private String[] gpuPaths;
    private int gpuRediscover;


    HudTelemetry(Context context) {this(context,null);}
    HudTelemetry(Context context,File pidFile) {
        this.pidFile=pidFile;this.context=context.getApplicationContext();
        long hz=100;
        try { hz=Os.sysconf(OsConstants._SC_CLK_TCK); }catch(Throwable ignored){}
        ticksPerSecond=hz>0?hz:100;

    }

    private final java.util.concurrent.ExecutorService sampler=java.util.concurrent.Executors.newSingleThreadExecutor(r->{Thread t=new Thread(r,"tlou-hud-sampler");t.setDaemon(true);return t;});
    private final java.util.concurrent.atomic.AtomicBoolean pending=new java.util.concurrent.atomic.AtomicBoolean();
    private volatile boolean closed;
    private long requestedAt;
    void requestSample(Process process,long now) {
        if(closed || now-requestedAt<1_000_000_000L || !pending.compareAndSet(false,true))return;
        requestedAt=now;
        sampler.execute(()->{try{if(!closed)sample(runtimePid(process),now);}finally{pending.set(false);}});
    }
    void close(){closed=true;sampler.shutdownNow();}
    private int runtimePid=-1;

    /** Android libcore does not expose java.lang.Process.pid() in all SDKs.
     * Prefer public reflection when present, otherwise inspect ONLY this app's
     * each /proc/self/task/TID/children file and validate the child executable name.
     * Missing procfs access simply hides CPU/RAM rather than misreporting them.
     */
    int runtimePid(Process process) {
        if(process==null || !process.isAlive())return -1;
        int published=RuntimeProcess.readPid(pidFile);
        if(published>0){runtimePid=published;return published;}
        if(runtimePid>0 && new File("/proc/"+runtimePid+"/stat").canRead())return runtimePid;
        try {
            java.lang.reflect.Method m=process.getClass().getMethod("pid");
            Object value=m.invoke(process);
            if(value instanceof Number && ((Number)value).intValue()>0) {
                runtimePid=((Number)value).intValue();
                return runtimePid;
            }
        }catch(Exception ignored){}
        File[] tasks=new File("/proc/self/task").listFiles();
        if(tasks==null)return -1;
        for(File task:tasks) {
            try(BufferedReader children=new BufferedReader(new FileReader(new File(task,"children")))) {
                String line=children.readLine();
                if(line==null)continue;
                for(String token:line.trim().split("\\s+")) {
                    if(token.isEmpty())continue;
                    int candidate=Integer.parseInt(token);
                    if(candidate<=0)continue;
                    try(BufferedReader name=new BufferedReader(new FileReader(
                        "/proc/"+candidate+"/cmdline"))) {
                        char[] buf=new char[2048];int count=name.read(buf);
                        if(count>0 && (new String(buf,0,count).contains("tlou-probe") || new String(buf,0,count).contains("gow3-probe"))) {
                            runtimePid=candidate;return runtimePid;
                        }
                    }catch(Exception ignored){}
                }
            }catch(Exception ignored){}
        }
        return -1;
    }

    private static long processTicks(int pid) throws Exception {
        try(BufferedReader reader=new BufferedReader(new FileReader("/proc/"+pid+"/stat"))) {
            String line=reader.readLine();
            if(line==null)throw new IllegalStateException("Sem estatísticas de CPU");
            int end=line.lastIndexOf(')');
            if(end<0||end+2>=line.length())throw new IllegalArgumentException("Formato de /proc/stat");
            String[] fields=line.substring(end+2).trim().split("\\s+");
            return Long.parseLong(fields[11])+Long.parseLong(fields[12]); // utime+stime
        }
    }

    void sample(int pid,long nowNanos) {
        if(resetPending.getAndSet(false) || sampledPid!=pid){previousTicks=-1;previousAt=0;sampledPid=pid;}
        sampleGpu();
        if(pid>0)sampleHotThreads(pid,nowNanos);
        if(pid<=0){cpuPercent=Double.NaN;memoryGiB=Double.NaN;return;}
        try {
            long ticks=processTicks(pid);
            if(previousTicks>=0 && nowNanos>previousAt && ticks>=previousTicks)
                cpuPercent=(ticks-previousTicks)*100.0*1_000_000_000.0/
                    (ticksPerSecond*(nowNanos-previousAt)); // percent of one CPU core
            previousTicks=ticks;previousAt=nowNanos;
            cpuSource("active:"+pid,"HUD CPU: processo "+pid+", contadores do kernel, "+ticksPerSecond+" ticks/s");
        }catch(Exception e){cpuPercent=Double.NaN;previousTicks=-1;previousAt=0;cpuSource("unavailable:"+pid,"HUD CPU indisponível: "+e);}
        try(BufferedReader reader=new BufferedReader(new FileReader("/proc/"+pid+"/status"))) {
            String line;
            while((line=reader.readLine())!=null)if(line.startsWith("VmRSS:")) {
                String digits=line.substring(6).replaceAll("[^0-9]","");
                if(!digits.isEmpty())memoryGiB=Long.parseLong(digits)/(1024.0*1024.0);
                break;
            }
        }catch(Exception e){memoryGiB=Double.NaN;}
    }

    // The game's FEX threads are not covered by the native "CPU critical"
    // report (which only measures command, draw and Vulkan recorder threads).
    // Sample the child PID's own /proc task counters off the UI thread; unlike
    // a per-process HUD percentage this identifies the actual saturated TID.
    private static final class HotThread {
        long lastTicks, windowTicks;
        double peakPercent;
        int lastCore, observations;
        String name;
    }
    private final java.util.HashMap<Integer,HotThread> hotThreads=new java.util.HashMap<>();
    private int hotPid=-1;
    private long hotAt, hotReportAt;
    private int hotSamples;

    private static String readOneLine(String path) {
        try(BufferedReader r=new BufferedReader(new FileReader(path))) {
            return r.readLine();
        }catch(Exception ignored){return null;}
    }
    private static String coreClock(int core) {
        final String base="/sys/devices/system/cpu/cpu"+core+"/cpufreq/";
        String cur=readOneLine(base+"scaling_cur_freq");
        String max=readOneLine(base+"scaling_max_freq");
        String physical=readOneLine(base+"cpuinfo_max_freq");
        if(cur==null && max==null)return "n/a";
        try {
            String a=cur==null?"?":String.format(Locale.US,"%.2f",Long.parseLong(cur.trim())/1e6);
            String b=max==null?"?":String.format(Locale.US,"%.2f",Long.parseLong(max.trim())/1e6);
            String c=physical==null?"?":String.format(Locale.US,"%.2f",Long.parseLong(physical.trim())/1e6);
            return a+"/"+b+"/"+c+"GHz(current/policy/hardware)";
        }catch(Exception ignored){return "n/a";}
    }
    private void sampleHotThreads(int pid,long now) {
        if(pid!=hotPid || hotAt==0 || now<=hotAt) {
            hotThreads.clear();hotPid=pid;hotAt=now;hotReportAt=now;hotSamples=0;
            return;
        }
        long elapsed=now-hotAt;
        File[] threads=new File("/proc/"+pid+"/task").listFiles();
        if(threads==null)return;
        for(File thread:threads) {
            final int tid;
            try{tid=Integer.parseInt(thread.getName());}
            catch(NumberFormatException ignored){continue;}
            String stat=readOneLine(new File(thread,"stat").getPath());
            if(stat==null)continue;
            int nameEnd=stat.lastIndexOf(')');
            int nameStart=stat.indexOf('(');
            if(nameStart<0 || nameEnd<=nameStart || nameEnd+2>=stat.length())continue;
            String[] fields=stat.substring(nameEnd+2).trim().split("\\s+");
            // /proc/PID/task/TID/stat field 3 is fields[0];
            // utime=14, stime=15, processor=39, indexed from field 3.
            if(fields.length<37)continue;
            try {
                long ticks=Long.parseLong(fields[11])+Long.parseLong(fields[12]);
                int core=Integer.parseInt(fields[36]);
                HotThread info=hotThreads.get(tid);
                if(info==null) {
                    info=new HotThread();info.lastTicks=ticks;
                    hotThreads.put(tid,info);
                } else if(ticks>=info.lastTicks) {
                    long delta=ticks-info.lastTicks;
                    info.windowTicks+=delta;
                    double percent=delta*100.0*1e9/(ticksPerSecond*(double)elapsed);
                    info.peakPercent=Math.max(info.peakPercent,percent);
                    info.lastTicks=ticks;
                } else {
                    // TID reuse / counter reset must not create a bogus peak.
                    info.windowTicks=0;info.peakPercent=0;info.lastTicks=ticks;
                }
                info.lastCore=core;
                info.name=stat.substring(nameStart+1,nameEnd);
                ++info.observations;
            }catch(NumberFormatException ignored){}
        }
        hotAt=now;
        ++hotSamples;
        if(now-hotReportAt<5_000_000_000L)return;
        final double windowNs=now-hotReportAt;
        java.util.ArrayList<java.util.Map.Entry<Integer,HotThread>> ranked=
            new java.util.ArrayList<>(hotThreads.entrySet());
        ranked.sort((a,b)->Long.compare(b.getValue().windowTicks,a.getValue().windowTicks));
        StringBuilder line=new StringBuilder("TLOU hot threads (5 s): pid=").append(pid)
            .append(" cpu6=").append(coreClock(6))
            .append(" cpu7=").append(coreClock(7))
            .append(" sampled=").append(hotSamples);
        int shown=0;
        for(java.util.Map.Entry<Integer,HotThread> item:ranked) {
            HotThread info=item.getValue();
            if(info.windowTicks==0 || shown++==6)break;
            double average=info.windowTicks*100.0*1e9/(ticksPerSecond*windowNs);
            line.append(String.format(Locale.US,
                " | tid=%d name=%s avg=%.1f%% peak1s=%.1f%% last-core=%d",
                item.getKey(),info.name,average,info.peakPercent,info.lastCore));
        }
        RunLog.write(context,line.toString());
        // Keep lastTicks for correct deltas across windows; discard stale
        // threads after reporting, so extinct short-lived tasks cannot grow
        // this map indefinitely.
        java.util.HashSet<Integer> alive=new java.util.HashSet<>();
        for(File thread:threads) {
            try{alive.add(Integer.parseInt(thread.getName()));}
            catch(NumberFormatException ignored){}
        }
        hotThreads.keySet().retainAll(alive);
        for(HotThread t:hotThreads.values()){t.windowTicks=0;t.peakPercent=0;t.observations=0;}
        hotReportAt=now;hotSamples=0;
    }

    private void cpuSource(String state,String message){
        if(!state.equals(cpuSourceState)){cpuSourceState=state;RunLog.write(context,message);}
    }
    private void sampleGpu() {
        if(gpuPaths==null || (gpuPaths.length==0 && ++gpuRediscover>=5)) {
            gpuRediscover=0;
            gpuPaths=PerfHudMetrics.discoverGpuPaths(PerfHudMetrics.DEFAULT_GPU_PATHS,PerfHudMetrics.SCAN_ROOTS);
        }
        gpuPercent=Double.NaN;
        for(String path:gpuPaths) {
            String line=PerfHudMetrics.readFirstLine(path);
            int n=path.endsWith("/gpubusy")?PerfHudMetrics.gpuBusyRatio(line):
                path.endsWith("/mtk_mali/utilization")?PerfHudMetrics.mtkActive(line):PerfHudMetrics.parsePercent(line);
            if(n>=0){gpuPercent=n;return;}
        }
        if(++gpuRediscover>=5){gpuPaths=null;gpuRediscover=0;}
    }
    double gpuPercent(){return gpuPercent;}
    double cpuPercent(){return Double.isNaN(cpuPercent)?Double.NaN:
        Math.min(100,cpuPercent/Math.max(1,Runtime.getRuntime().availableProcessors()));}
    double ramGiB(){return memoryGiB;}
    double totalRamGiB(){return deviceGiB;}

    void reset(){resetPending.set(true);cpuPercent=Double.NaN;}
}
