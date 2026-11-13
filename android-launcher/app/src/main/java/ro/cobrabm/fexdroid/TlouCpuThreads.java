package ro.cobrabm.fexdroid;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.Consumer;

/**
 * Non-invasive process-wide CPU accounting for both FEX and ARM64 AOT.
 * /proc/PID/task/TID/schedstat is read at 5-second intervals; there is no
 * per-draw/worker instrumentation and no change to guest scheduling/affinity.
 * A worker using 90% CPU is shown as 90% of ONE core, not of the entire SoC.
 */
final class TlouCpuThreads implements AutoCloseable {
    static final class Sample {
        final int tid, core;
        final String name;
        final long runtimeNs, delayNs;
        Sample(int tid,String name,int core,long runtimeNs,long delayNs) {
            this.tid=tid;this.name=name;this.core=core;
            this.runtimeNs=runtimeNs;this.delayNs=delayNs;
        }
    }
    private final long pid;
    private final String backend;
    private final Consumer<String> log;
    private final Thread worker;
    private volatile boolean running=true;

    TlouCpuThreads(long pid,String backend,Consumer<String> log) {
        this.pid=pid;this.backend=backend;this.log=log;
        worker=new Thread(this::monitor,"tlou-cpu-threads");
        worker.setDaemon(true);
        worker.setPriority(Thread.MIN_PRIORITY);
    }
    void start(){ worker.start(); }

    static Sample read(File tidDir) throws IOException {
        final int tid=Integer.parseInt(tidDir.getName());
        final String stat=readFile(new File(tidDir,"stat"));
        final int end=stat.lastIndexOf(')');
        if(end<0 || end+2>=stat.length())throw new IOException("task stat inválido");
        final int first=stat.indexOf('(');
        final String fallback=first>=0 ? stat.substring(first+1,end) : "task";
        final String[] parts=stat.substring(end+2).trim().split("\\s+");
        // /proc stat field 39 (last execution CPU), after 'comm)' field 2,
        // is token 36 because token 0 is field 3 ('state').
        final int core=parts.length>36 ? parseInt(parts[36],-1) : -1;
        String name=fallback;
        try {name=readFile(new File(tidDir,"comm")).trim();}
        catch(IOException ignored) { /* /stat contains the fallback name */ }
        final String[] sched=readFile(new File(tidDir,"schedstat")).trim().split("\\s+");
        if(sched.length<2)throw new IOException("task schedstat inválido");
        return new Sample(tid,name,core,Long.parseLong(sched[0]),Long.parseLong(sched[1]));
    }
    private static String readFile(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()),StandardCharsets.US_ASCII);
    }
    private static int parseInt(String str,int fallback) {
        try {return Integer.parseInt(str);}catch(NumberFormatException e){return fallback;}
    }
    private Map<Integer,Sample> snapshot(File tasks) throws IOException {
        final File[] entries=tasks.listFiles(File::isDirectory);
        if(entries==null)throw new IOException("Acesso indisponível a "+tasks);
        final Map<Integer,Sample> result=new HashMap<>();
        for(File entry:entries) {
            try {
                final Sample s=read(entry);
                result.put(s.tid,s);
            }catch(IOException|NumberFormatException ignored) {
                // A short-lived compiler/DCLoader thread may exit during the read.
            }
        }
        if(result.isEmpty())throw new IOException("Sem schedstat por thread; acesso /proc restrito");
        return result;
    }
    private static final class Usage {
        Sample current; double busy,wait;
        Usage(Sample current,double busy,double wait) {
            this.current=current;this.busy=busy;this.wait=wait;
        }
    }
    static String report(Map<Integer,Sample> before, Map<Integer,Sample> after,
                         long elapsedNs,String backend) {
        if(elapsedNs<=0)return "";
        final List<Usage> all=new ArrayList<>();
        for(Sample now:after.values()) {
            Sample old=before.get(now.tid);
            if(old==null||now.runtimeNs<old.runtimeNs||now.delayNs<old.delayNs)continue;
            // A task can be reaped/reused. A different name means no valid delta.
            if(!old.name.equals(now.name))continue;
            double used=Math.min(100.0,100.0*(now.runtimeNs-old.runtimeNs)/elapsedNs);
            double waiting=100.0*(now.delayNs-old.delayNs)/elapsedNs;
            all.add(new Usage(now,used,waiting));
        }
        all.sort(Comparator.comparingDouble((Usage u)->u.busy).reversed());
        double process=0,workers=0;
        int workerCount=0;
        final List<String> jobs=new ArrayList<>(),top=new ArrayList<>();
        for(Usage u:all) {
            process+=u.busy;
            String item=String.format(Locale.US,"%s:%d=%.1f%%@CPU%d rq=%.1f%%",
                    u.current.name,u.current.tid,u.busy,u.current.core,u.wait);
            if(u.current.name.startsWith("NdJobWorker")) {
                workers+=u.busy;++workerCount;jobs.add(item);
            } else if(top.size()<8)top.add(item);
        }
        Collections.sort(jobs); // stable ID/order despite scheduler migration
        return String.format(Locale.US,
                "TLOU CPU all-threads (%s, %.2fs): total=%.1f%% of one-core "
                +"(%.2f cores) matched=%d workers=%d worker-total=%.1f%% "
                +"worker-cores=%.2f | job=[%s] | other-top=[%s] "
                +"(CPU = execution time, rq = waiting runnable, CPU# = last observed core)",
                backend,elapsedNs/1e9,process,process/100.0,all.size(),
                workerCount,workers,workers/100.0,String.join(", ",jobs),
                String.join(", ",top));
    }
    private void monitor() {
        File tasks=new File("/proc/"+pid+"/task");
        try {
            Map<Integer,Sample> previous=snapshot(tasks);
            long before=System.nanoTime();
            while(running) {
                Thread.sleep(5000);
                if(!running)return;
                Map<Integer,Sample> next=snapshot(tasks);
                long now=System.nanoTime();
                String output=report(previous,next,now-before,backend);
                if(!output.isEmpty())log.accept(output);
                before=now;previous=next;
            }
        } catch(InterruptedException ignored) {
            Thread.currentThread().interrupt();
        } catch(IOException|RuntimeException error) {
            log.accept("TLOU CPU all-threads ("+backend+"): unavailable: "+error);
        }
    }
    @Override public void close() {running=false;worker.interrupt();}
}
