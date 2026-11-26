package ro.cobrabm.fexdroid;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.Date;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Durable launch breadcrumbs exist before JNI, AHB or the FEX child is started. */
final class RunLog {
    static File launch(Context c) { return new File(c.getFilesDir(),"launch.log"); }
    static synchronized void begin(Context c) {
        File current=launch(c), previous=new File(c.getFilesDir(),"launch-previous.log");
        if(current.exists()) { previous.delete(); current.renameTo(previous); }
        write(c,"JOGAR • "+new Date()+" • Android "+Build.VERSION.RELEASE+" / API "+Build.VERSION.SDK_INT+" • "+Build.MANUFACTURER+" "+Build.MODEL);
    }
    static synchronized void write(Context c,String message) {
        try(FileOutputStream out=new FileOutputStream(launch(c),true)) {
            out.write((new Date()+" ["+Thread.currentThread().getName()+"] "+message+"\n").getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        } catch(IOException e) { android.util.Log.e("TLOU","Cannot write launch log",e); }
    }
    static void failure(Context c,String label,Throwable error) {
        StringWriter trace=new StringWriter(); error.printStackTrace(new PrintWriter(trace));
        write(c,label+"\n"+trace);
    }
    static File runtime(Context c) { return new File(c.getFilesDir(),"tlou/runtime.log"); }
    static String tail(File file,int bytes) {
        if(!file.isFile())return "Ainda não criado.";
        try(RandomAccessFile in=new RandomAccessFile(file,"r")) {
            in.seek(Math.max(0,in.length()-bytes)); byte[] data=new byte[(int)(in.length()-in.getFilePointer())];in.readFully(data);return new String(data,StandardCharsets.UTF_8);
        }catch(IOException e){return e.toString();}
    }
    static String summary(Context c) {
        return "INICIALIZAÇÃO\n"+tail(launch(c),10000)+"\n\nRUNTIME\n"+tail(runtime(c),10000)+
            "\n\nINSTALAÇÃO / PREPARAÇÃO\n"+tail(new File(c.getFilesDir(),"installation.log"),5000);
    }
    private static final String PREVIOUS_SHADER_DIR="previous-shader-captures";
    private static List<String> shaderCaptureNames() {
        List<String> paths=new ArrayList<>();
        paths.add("qualcomm-compute-failed.txt");
        for(int i=0;i<8;i++)paths.add("qualcomm-compute-failed-"+i+".spv");
        // Match native GraphicsPipeline's bounded capture budget (12).
        for(int i=0;i<12;i++) {
            paths.add("qualcomm-graphics-"+i+".txt");
            for(int stage=0;stage<6;stage++)paths.add("qualcomm-graphics-"+i+"-"+stage+".spv");
        }
        return paths;
    }
    static void rotateShaderCaptures(Context c) {
        File current=new File(c.getFilesDir(),"tlou");
        File previous=new File(current,PREVIOUS_SHADER_DIR);
        if(!previous.isDirectory()&&!previous.mkdirs()) {
            write(c,"Não foi possível criar a pasta das capturas anteriores.");
            return;
        }
        for(String name:shaderCaptureNames()) {
            File old=new File(previous,name),now=new File(current,name);
            try {
                java.nio.file.Files.deleteIfExists(old.toPath());
                if(now.isFile())java.nio.file.Files.move(now.toPath(),old.toPath());
            }catch(IOException e){failure(c,"Rotação da captura "+name,e);}
        }
    }
    static void export(Context c,OutputStream output) throws IOException {
        try(ZipOutputStream zip=new ZipOutputStream(output)) {
            List<String> paths=new ArrayList<>(Arrays.asList(
                "launch.log","launch-previous.log","tlou/runtime.log",
                "tlou/runtime-previous.log","installation.log",
                "tlou-prepared/device-ready.json",
                "tlou-aot-work/aot-report.json","tlou-aot-work/aot/compile.txt",
                "tlou-aot-work/aot/libtlouaot.json",
                "tlou/aot-misses.log","tlou/perf-cs-22e6e19e.bin",
                "tlou/perf-cs-61d67259.bin"));
            // Only the single shipping ARM64 cache is relevant to reports.
            // Never export the multi-GB proprietary recompiled unit shards.
            for(String filename:new String[]{
                "aot-report.json","aot/compile.txt","aot/libtlouaot.json"})
                paths.add("tlou-aot-work-off/"+filename);
            // A Qualcomm VK_ERROR_UNKNOWN can occur after the Vulkan instance
            // starts but before gameplay. Include only the bounded failing
            // shader binaries, not the private shader cache or whole game.
            for(String name:shaderCaptureNames()) {
                paths.add("tlou/"+name);
                paths.add("tlou/"+PREVIOUS_SHADER_DIR+"/"+name);
            }
            for(String path:paths) {
                File file=new File(c.getFilesDir(),path);if(!file.isFile())continue;
                zip.putNextEntry(new ZipEntry(path));
                try(RandomAccessFile in=new RandomAccessFile(file,"r")) {
                    // Long gameplay reports keep four times the former
                    // runtime log tail. Binary captures must remain intact.
                    long limit=path.startsWith("tlou/runtime")?16L*1024*1024:4L*1024*1024;
                    if(!path.endsWith(".spv"))in.seek(Math.max(0,in.length()-limit));
                    byte[] bytes=new byte[65536];int n;while((n=in.read(bytes))!=-1)zip.write(bytes,0,n);
                }
                zip.closeEntry();
            }
            zip.putNextEntry(new ZipEntry("android-process-exits.txt"));
            try {
                ActivityManager manager=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE);
                if(manager!=null)for(ApplicationExitInfo exit:manager.getHistoricalProcessExitReasons(c.getPackageName(),0,5)) {
                    String line=new Date(exit.getTimestamp())+" process="+exit.getProcessName()+" reason="+exit.getReason()+" status="+exit.getStatus()+" description="+exit.getDescription()+"\n";
                    zip.write(line.getBytes(StandardCharsets.UTF_8));
                }
            } catch(RuntimeException e) { zip.write(e.toString().getBytes(StandardCharsets.UTF_8)); }
            zip.closeEntry();
        }
    }
}
