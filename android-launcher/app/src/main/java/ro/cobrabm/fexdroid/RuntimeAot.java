package ro.cobrabm.fexdroid;

import android.content.Context;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;

/**
 * Validates locally generated ARM64 native code before the runtime is launched.
 *
 * The normal FEX binary and the user's game files are never changed.
 * The native code generated from the player's own PS4 game stays private to
 * this application; the standalone AOT CPU engine is a separate libgow3cpu.so
 * loaded by the child process through LD_LIBRARY_PATH.
 */
final class RuntimeAot {
    private RuntimeAot() {}

    static final class Ready {
        final File library;
        final File engineDirectory;
        Ready(File library, File engineDirectory) {
            this.library=library;
            this.engineDirectory=engineDirectory;
        }
    }

    private static String sha256(File file) throws Exception {
        MessageDigest md=MessageDigest.getInstance("SHA-256");
        byte[] bytes=new byte[1024*1024];
        try(InputStream in=new FileInputStream(file)) {
            int length;
            while((length=in.read(bytes))!=-1) md.update(bytes,0,length);
        }
        StringBuilder hash=new StringBuilder(64);
        for(byte b:md.digest())hash.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
        return hash.toString();
    }

    private static void stageAsset(Context context,String path,File target,long minimum) throws Exception {
        File folder=target.getParentFile();
        if(folder==null || (!folder.isDirectory()&&!folder.mkdirs()))
            throw new java.io.IOException("Não foi possível criar diretório ARM64");
        File temp=new File(folder,target.getName()+".new");
        try {
            try(InputStream in=context.getAssets().open(path);
                FileOutputStream out=new FileOutputStream(temp)) {
                byte[] buffer=new byte[65536]; int n;
                while((n=in.read(buffer))!=-1)out.write(buffer,0,n);
                out.getFD().sync();
            }
            if(temp.length()<minimum || !temp.setReadable(true,true))
                throw new java.io.IOException("Biblioteca de CPU AOT incompleta");
            if(target.exists()&&!target.delete())throw new java.io.IOException("Não foi possível atualizar CPU AOT");
            if(!temp.renameTo(target))throw new java.io.IOException("Não foi possível instalar CPU AOT");
        } finally {temp.delete();}
    }

    static Ready prepare(Context context,File prepared) throws Exception {
        File work=new File(context.getFilesDir(),"tlou-aot-work");
        File so=new File(work,"aot/libtlouaot.so");
        File json=new File(work,"aot/libtlouaot.json");
        File report=new File(work,"aot-report.json");
        File boot=new File(prepared,"boot-linked.bin");
        for(File f:new File[]{so,json,report,boot}) {
            if(!f.isFile())throw new java.io.IOException("AOT não compilado para este jogo: "+f.getName());
        }
        JSONObject meta=new JSONObject(new String(Files.readAllBytes(json.toPath()),StandardCharsets.UTF_8));
        JSONObject source=new JSONObject(new String(Files.readAllBytes(report.toPath()),StandardCharsets.UTF_8));
        if(meta.optInt("abi_version",-1)!=2)
            throw new java.io.IOException("ABI da biblioteca AOT incompatível");
        if(!"aarch64_generated_runtime_not_integrated".equals(source.optString("status")))
            throw new java.io.IOException("Biblioteca AOT não validada pelo compilador");
        String expectedBoot=source.optString("input_boot_sha256");
        if(expectedBoot.length()!=64 || !expectedBoot.equalsIgnoreCase(sha256(boot)))
            throw new java.io.IOException("O jogo preparado mudou após a compilação ARM64");
        if(so.length()<4096 || meta.optLong("so_size",-1)!=so.length() ||
                !meta.optString("so_sha256").equalsIgnoreCase(sha256(so)))
            throw new java.io.IOException("Biblioteca ARM64 alterada ou corrompida");
        File engineDir=new File(context.getFilesDir(),"tlou/runtime-aot");
        File engine=new File(engineDir,"libgow3cpu.so");
        // Unpack a separate CPU engine. The FEX library in nativeLibraryDir
        // is left byte-identical, and normal gameplay continues to use it.
        stageAsset(context,"runtime-aot/libgow3cpu.so",engine,4096);
        // bb-recomp's output depends on SONAME libbbcpu.so. The tiny shim
        // depends on libgow3cpu.so, ensuring bbr_* symbols resolve from
        // the same AOT engine already used by the game executable.
        stageAsset(context,"runtime-aot/libbbcpu.so",
                   new File(engineDir,"libbbcpu.so"),512);
        return new Ready(so,engineDir);
    }
}
