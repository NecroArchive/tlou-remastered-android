package ro.cobrabm.fexdroid;

import android.content.Context;
import org.json.JSONObject;
import org.json.JSONArray;
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
        final File bootImage;
        Ready(File library, File engineDirectory, File bootImage) {
            this.library=library;
            this.engineDirectory=engineDirectory;
            this.bootImage=bootImage;
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
        if(!"4-tlou-tso-one-fence".equals(meta.optString("compiler_version")) ||
                !"aarch64_generated_native_ready".equals(source.optString("status")))
            throw new java.io.IOException("Biblioteca AOT antiga ou sem suporte a TSO. Recompile em Ajustes → CPU.");
        // Guest PS4 fibers can replace RBX/RBP/R12-R15 in HostCall. Old native
        // libraries trusted SysV callee-saved registers and are not safe here.
        JSONArray command=source.optJSONArray("translation_command");
        boolean reloadAfterCall=false, nativeTso=false, fiberResumes=false;
        if(command!=null)for(int i=0;i<command.length();i++) {
            String flag=command.optString(i);
            if("--no-callee-saved".equals(flag))reloadAfterCall=true;
            if("--tso".equals(flag))nativeTso=true;
            if("--resume-entries".equals(flag))fiberResumes=true;
        }
        if(!reloadAfterCall || !nativeTso || !fiberResumes ||
           !"reload_after_calls_for_fibers".equals(source.optString("callee_saved_policy")))
            throw new java.io.IOException(
                "Biblioteca AOT anterior incompatível com fibers do TLOU. Recompile em Ajustes → CPU.");
        JSONObject quality=source.optJSONObject("coverage");
        if(quality==null || quality.optDouble("unsupported_percent",100)>0.25)
            throw new java.io.IOException("Biblioteca AOT incompleta; recompile com o compilador TSO corrigido");
        String expectedBoot=source.optString("input_boot_sha256");
        if(expectedBoot.length()!=64 || !expectedBoot.equalsIgnoreCase(sha256(boot)))
            throw new java.io.IOException("O jogo preparado mudou após a compilação ARM64");
        // The 1.11 AOT build must contain the already verified FEX animation
        // pointer guard as native ARM64 instructions. Old AOT images omit it,
        // and the unpatched path may dereference a bogus output pointer.
        File runtimeBoot=boot;
        if("1.11".equals(source.optString("version"))) {
            JSONArray nativePatches=source.optJSONArray("native_patches");
            boolean hasAnimationGuard=false,hasTimeout=false,hasIdleBackoff=false;
            if(nativePatches!=null)for(int i=0;i<nativePatches.length();i++) {
                String patch=nativePatches.optString(i);
                if("invalid-animation-output-pointer".equals(patch))hasAnimationGuard=true;
                if("frame-timeout".equals(patch))hasTimeout=true;
                if("native-job-idle-backoff".equals(patch))hasIdleBackoff=true;
            }
            JSONObject expected=source.optJSONObject("native_patch_options");
            // Byte-verified 01.11 AOT compilation now uses LLVM -O2 and an
            // immutable executable contract. Static patches are already
            // present in the boot used to compile each unit, so --patches and
            // runtime executable hooks MUST remain disabled.
            boolean fastO2=false,noDirty=false;
            if(command!=null)for(int i=0;i<command.length();i++) {
                if("-O2".equals(command.optString(i)))fastO2=true;
                if("--no-dirty-check".equals(command.optString(i)))noDirty=true;
            }
            if(!fastO2 || !noDirty ||
                    !"tlou-111-static-llvmO2-immutable-v1".equals(
                        source.optString("native_codegen_profile")) ||
                    !source.optBoolean("runtime_code_immutable",false) ||
                    !"static-patches;--no-game-hooks;--patches-disabled".equals(
                        source.optString("runtime_patch_contract")))
                throw new java.io.IOException(
                    "CPU ARM64 1.11 antiga (LLVM -O1). Recompile para ativar -O2 e o perfil estático otimizado.");
            RunLog.write(context,"TLOU AOT 1.11 codegen: LLVM -O2, immutable code, dirty-unit entry checks removed, TSO/fibers retained.");
            if(!"tlou-111-static-v2".equals(source.optString("native_patch_profile")) ||
                    !hasAnimationGuard || !hasTimeout || expected==null ||
                    (expected.optBoolean("job_sleep",true) && !hasIdleBackoff))
                throw new java.io.IOException(
                    "AOT 1.11 sem correção de espera ativa dos seis workers. Recompile em Ajustes → CPU.");
            JSONObject actual=new JSONObject(new String(Files.readAllBytes(
                    PortSettings.patchOptions(context).toPath()),StandardCharsets.UTF_8));
            if(expected==null || !expected.optString("resolution","?").equals(
                    actual.optString("resolution","!")) ||
                    expected.optBoolean("job_sleep",false)!=actual.optBoolean("job_sleep",false))
                throw new java.io.IOException(
                    "Configuração AOT mudou desde a compilação (resolução/workers). Recompile a CPU ARM64.");
            for(String effect:PortSettings.EFFECTS) {
                if(!expected.has(effect) ||
                        expected.optBoolean(effect)!=actual.optBoolean(effect))
                    throw new java.io.IOException(
                        "Efeitos AOT alterados após a compilação: "+effect+". Recompile a CPU ARM64.");
            }
            RunLog.write(context,"TLOU AOT patches ARM64 compilados: "+nativePatches+
                    " • resolução="+expected.optString("resolution")+" • FEX original preservado.");
            runtimeBoot=new File(work,"native-boot-linked.bin");
            String expectedNative=source.optString("native_boot_sha256");
            if(!runtimeBoot.isFile() || expectedNative.length()!=64 ||
               !expectedNative.equalsIgnoreCase(sha256(runtimeBoot)) ||
               expectedNative.equalsIgnoreCase(expectedBoot))
                throw new java.io.IOException("Imagem AOT 1.11 incompatível; recompile a CPU ARM64.");
        }
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
        return new Ready(so,engineDir,runtimeBoot);
    }
}
