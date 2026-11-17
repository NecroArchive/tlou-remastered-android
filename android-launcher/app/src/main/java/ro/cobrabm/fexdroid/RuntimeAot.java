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
        final String compiledResolution;
        final String pendingCodePatches;
        Ready(File library, File engineDirectory, File bootImage,
              String compiledResolution, String pendingCodePatches) {
            this.library=library;
            this.engineDirectory=engineDirectory;
            this.bootImage=bootImage;
            this.compiledResolution=compiledResolution;
            this.pendingCodePatches=pendingCodePatches;
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

    // The ARM64 image contains immutable guest code. Preference differences
    // do not invalidate a verified library: new patches remain pending.
    static String pendingCodePatches(JSONObject compiled, JSONObject requested) {
        java.util.ArrayList<String> changed=new java.util.ArrayList<>();
        if(!compiled.optString("resolution","").equals(requested.optString("resolution","")))
            changed.add("resolution");
        for(String effect:PortSettings.EFFECTS) {
            if(compiled.has(effect) &&
               compiled.optBoolean(effect)!=requested.optBoolean(effect))
                changed.add(effect);
        }
        return String.join(", ",changed);
    }

    /**
     * Cheap pre-launch check: the user's currently selected patches must equal
     * those baked into the immutable ARM64 guest image. Never silently boot a
     * previous AOT variant after the user changed resolution or visual effects.
     * A missing AOT report is handled by the existing full prepare validator.
     */
    static boolean hasCompleteVariantCatalog(JSONObject source) {
        if(!"tlou-111-variants-v3".equals(source.optString("native_patch_profile")))
            return false;
        JSONObject variants=source.optJSONObject("variant_catalog");
        if(variants==null || !"tlou-111-variants-v3".equals(variants.optString("profile"))
                || variants.optString("sha256").length()!=64)
            return false;
        JSONArray groups=variants.optJSONArray("groups");
        if(groups==null || groups.length()!=PortSettings.EFFECTS.length+3)
            return false;
        java.util.HashSet<String> all=new java.util.HashSet<>();
        for(int i=0;i<groups.length();i++)all.add(groups.optString(i));
        if(all.size()!=groups.length())return false;
        for(String r:new String[]{"720","540","360"})
            if(!all.contains("resolution_"+r))return false;
        for(String key:PortSettings.EFFECTS)
            if(!all.contains("effect_"+key))return false;
        return true;
    }

    /** Lightweight status for the launcher label only. The full hash and
     * prepared-game checks in prepare() remain mandatory before launch.
     * A stale .so or an interrupted compiler is never shown as completed. */
    static boolean hasCompletedVariantBuild(Context context) {
        File work=TsoProfile.work(context);
        File so=new File(work,"aot/libtlouaot.so");
        File metaFile=new File(work,"aot/libtlouaot.json");
        File reportFile=new File(work,"aot-report.json");
        File catalog=new File(work,"aot/tlou-111-all-variants.txt");
        if(!so.isFile() || so.length()<4096 || !metaFile.isFile() ||
           !reportFile.isFile() || !catalog.isFile())return false;
        try {
            JSONObject report=new JSONObject(new String(Files.readAllBytes(
                reportFile.toPath()),StandardCharsets.UTF_8));
            JSONObject meta=new JSONObject(new String(Files.readAllBytes(
                metaFile.toPath()),StandardCharsets.UTF_8));
            JSONObject built=report.optJSONObject("variant_build");
            return "aarch64_generated_native_ready".equals(report.optString("status")) &&
                "1.11".equals(report.optString("version")) &&
                hasCompleteVariantCatalog(report) &&
                meta.optInt("abi_version",-1)==2 &&
                TsoProfile.version(TsoProfile.RELAXED).equals(meta.optString("compiler_version")) &&
                built!=null && built.optInt("groups_with_code",-1)==11 &&
                built.optInt("variants_skipped",-1)==0 &&
                built.optInt("variants_planned",0)>=11 &&
                built.optInt("variants_built",0)==built.optInt("variants_planned",-1);
        } catch(Exception incomplete) {
            return false;
        }
    }

    static String pendingSelectedPatches(Context context) throws Exception {
        File report=new File(TsoProfile.work(context),"aot-report.json");
        if(!report.isFile())return "";
        JSONObject source=new JSONObject(new String(Files.readAllBytes(
            report.toPath()),StandardCharsets.UTF_8));
        if(!"1.11".equals(source.optString("version")))return "";
        if(hasCompleteVariantCatalog(source))return "";
        if("aarch64_generated_native_ready".equals(source.optString("status")))
            return "biblioteca ARM64 antiga (precisa de uma única recompilação para ativar todos os patches)";
        JSONObject compiled=source.optJSONObject("native_patch_options");
        if(compiled==null)throw new java.io.IOException(
            "Metadados de patches ARM64 ausentes. Recompile em Ajustes → CPU.");
        JSONObject selected=new JSONObject(new String(Files.readAllBytes(
            PortSettings.patchOptions(context).toPath()),StandardCharsets.UTF_8));
        return pendingCodePatches(compiled,selected);
    }

    static Ready prepare(Context context,File prepared) throws Exception {
        File work=TsoProfile.work(context);
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
        if(!TsoProfile.version(TsoProfile.RELAXED).equals(meta.optString("compiler_version")) ||
                !"aarch64_generated_native_ready".equals(source.optString("status")))
            throw new java.io.IOException("Biblioteca ARM64 não compilada ou incompatível. "
                +"Recompile em Ajustes → CPU.");
        RunLog.write(context,"AOT ARM64: compilador="+meta.optString("compiler_version")+
            " • biblioteca="+so.getAbsolutePath());
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
        if(!reloadAfterCall || nativeTso || !fiberResumes ||
           !"reload_after_calls_for_fibers".equals(source.optString("callee_saved_policy")))
            throw new java.io.IOException(
                "Biblioteca ARM64 com ABI incompatível. Recompile em Ajustes → CPU.");
        if(!TsoProfile.RELAXED.equals(source.optString("tso_profile")) ||
           !"relaxed_ARM64_no_TSO_UNSAFE".equals(source.optString("guest_memory_model")))
            throw new java.io.IOException("Biblioteca ARM64 antiga incompatível. Recompile.");
        JSONObject quality=source.optJSONObject("coverage");
        if(quality==null || quality.optDouble("unsupported_percent",100)>0.25)
            throw new java.io.IOException("Biblioteca ARM64 incompleta; recompile em Ajustes → CPU.");
        String expectedBoot=source.optString("input_boot_sha256");
        if(expectedBoot.length()!=64 || !expectedBoot.equalsIgnoreCase(sha256(boot)))
            throw new java.io.IOException("O jogo preparado mudou após a compilação ARM64");
        // The 1.11 AOT build must contain the already verified FEX animation
        // pointer guard as native ARM64 instructions. Old AOT images omit it,
        // and the unpatched path may dereference a bogus output pointer.
        File runtimeBoot=boot;
        String compiledResolution=null;
        String pendingPatches="";
        if("1.11".equals(source.optString("version"))) {
            JSONArray nativePatches=source.optJSONArray("native_patches");
            boolean hasAnimationGuard=false,hasTimeout=false,hasIdleBackoff=false;
            if(nativePatches!=null)for(int i=0;i<nativePatches.length();i++) {
                String patch=nativePatches.optString(i);
                if("invalid-animation-output-pointer".equals(patch))hasAnimationGuard=true;
                if("frame-timeout".equals(patch))hasTimeout=true;
                if("native-job-idle-backoff".equals(patch))hasIdleBackoff=true;
            }
            // One LLVM -O2 image contains ALL verified 1.11 patches as
            // bb-recomp function variants, not one static selected profile.
            // Variant dispatch requires the native dirty-entry check. Old
            // immutable libraries must be recompiled exactly once.
            boolean fastO2=false,noDirty=false,variantFlag=false;
            if(command!=null)for(int i=0;i<command.length();i++){
                String flag=command.optString(i);
                if("-O2".equals(flag))fastO2=true;
                if("--no-dirty-check".equals(flag))noDirty=true;
                if("--variants".equals(flag))variantFlag=true;
            }
            long abiFlags=meta.optLong("abi_flags",0);
            if(!fastO2 || noDirty || !variantFlag ||
                (abiFlags&6)!=6 || !hasCompleteVariantCatalog(source) ||
                !"tlou-111-llvmO2-native-variants-v1".equals(
                    source.optString("native_codegen_profile")) ||
                source.optBoolean("runtime_code_immutable",true) ||
                !"native-variants;--no-game-hooks;--patches-at-launch".equals(
                    source.optString("runtime_patch_contract")))
                throw new java.io.IOException(
                    "CPU ARM64 antiga: compile uma vez para disponibilizar todos os patches e resoluções.");
            JSONObject allVariants=source.optJSONObject("variant_catalog");
            File catalog=new File(work,"aot/tlou-111-all-variants.txt");
            String expectedCatalogHash=allVariants==null?"":allVariants.optString("sha256");
            String compilerCatalogHash=meta.optString("catalog_hash","");
            if(!catalog.isFile() || expectedCatalogHash.length()!=64 ||
                !expectedCatalogHash.equalsIgnoreCase(sha256(catalog)) ||
                compilerCatalogHash.length()!=16 ||
                "cbf29ce484222325".equalsIgnoreCase(compilerCatalogHash))
                throw new java.io.IOException(
                    "Catálogo de todas as variantes ARM64 ausente ou alterado. Recompile uma vez.");
            JSONObject builtVariants=source.optJSONObject("variant_build");
            if(builtVariants==null ||
                builtVariants.optInt("groups",-1)!=11 ||
                builtVariants.optInt("groups_with_code",-1)!=11 ||
                builtVariants.optInt("variants_planned",0)<11 ||
                builtVariants.optInt("variants_built",0)!=
                    builtVariants.optInt("variants_planned",-1) ||
                builtVariants.optInt("variants_skipped",-1)!=0)
                throw new java.io.IOException(
                    "Algumas variantes ARM64 não foram compiladas. Recompile a CPU.");
            if(!hasAnimationGuard || !hasTimeout || !hasIdleBackoff)
                throw new java.io.IOException(
                    "Faltam correções obrigatórias dos workers/animação no ARM64.");
            JSONObject actual=new JSONObject(new String(Files.readAllBytes(
                PortSettings.patchOptions(context).toPath()),StandardCharsets.UTF_8));
            if(!actual.optBoolean("job_sleep",false))
                throw new java.io.IOException("Política de workers incompatível.");
            compiledResolution=actual.optString("resolution","original");
            if(!java.util.Arrays.asList("original","720","540","360").contains(compiledResolution))
                throw new java.io.IOException("Resolução não disponível no catálogo ARM64.");
            RunLog.write(context,"CPU ARM64: catálogo completo validado ("+
                source.getJSONObject("variant_catalog").optJSONArray("groups").length()+
                " grupos), efeitos e resolução escolhidos para esta sessão • "+
                compiledResolution+" • sem recompilação.");
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
        return new Ready(so,engineDir,runtimeBoot,compiledResolution,pendingPatches);
    }
}
