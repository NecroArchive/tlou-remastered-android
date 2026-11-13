package ro.cobrabm.fexdroid;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

public final class TlouCpuThreadsTest {
    private static void ok(boolean condition,String label){
        if(!condition)throw new AssertionError(label);
    }
    private static void write(Path p,String text)throws Exception {
        Files.write(p,text.getBytes(StandardCharsets.US_ASCII));
    }
    private static String fakeStat(int tid,String name,int lastCpu){
        StringBuilder text=new StringBuilder(tid+" ("+name+") S");
        // Kernel stat field 39 (processor) = 36 after the state field 3.
        for(int i=1;i<=36;i++)text.append(" ").append(i==36?lastCpu:0);
        return text.append("\n").toString();
    }
    public static void main(String[] args)throws Exception {
        Path tmp=Files.createTempDirectory("tlou-workers-");
        Path task=Files.createDirectory(tmp.resolve("14358"));
        write(task.resolve("stat"),fakeStat(14358,"NdJobWorkerThre",3));
        write(task.resolve("comm"),"NdJobWorkerThre\n");
        write(task.resolve("schedstat"),"1000000000 100000000 500\n");
        TlouCpuThreads.Sample old=TlouCpuThreads.read(task.toFile());
        ok(old.tid==14358 && old.core==3 && old.runtimeNs==1000000000L,
           "kernel field39 parse");
        write(task.resolve("stat"),fakeStat(14358,"NdJobWorkerThre",1));
        write(task.resolve("schedstat"),"5500000000 200000000 1000\n");
        TlouCpuThreads.Sample curr=TlouCpuThreads.read(task.toFile());
        Map<Integer,TlouCpuThreads.Sample> before=new HashMap<>();
        Map<Integer,TlouCpuThreads.Sample> after=new HashMap<>();
        before.put(old.tid,old);after.put(curr.tid,curr);
        String report=TlouCpuThreads.report(before,after,5000000000L,"ARM64 AOT");
        ok(report.contains("workers=1 worker-total=90.0%"),"worker CPU percentage");
        ok(report.contains("NdJobWorkerThre:14358=90.0%@CPU1"),"core+tid");
        ok(report.contains("rq=2.0%"),"runnable-but-not-running metric");
        TlouCpuThreads.Sample other=new TlouCpuThreads.Sample(
                14367,"GPUcommand",7,3000000000L,0);
        before.put(other.tid,new TlouCpuThreads.Sample(14367,"GPUcommand",7,1000000000L,0));
        after.put(other.tid,other);
        String withRenderer=TlouCpuThreads.report(before,after,5000000000L,"FEX");
        ok(withRenderer.contains("total=130.0%"),"all-thread CPU aggregate");
        ok(withRenderer.contains("worker-cores=0.90"),"worker thread accounting");
        ok(withRenderer.contains("GPUcommand:14367=40.0%@CPU7"),"non-worker measurement");
        ok("".equals(TlouCpuThreads.report(before,after,0,"FEX")),"reject zero wall time");
        System.out.println("TLOU CPU worker sampling tests passed");
    }
}
