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
 * The user's game files are never modified by the on-device AOT compiler.
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
        String version=source.optString("version");
        String profile="1.00".equals(version)?"tlou-100-variants-v1":
            "1.11".equals(version)?"tlou-111-variants-v3":"";
        if(profile.isEmpty() || !profile.equals(source.optString("native_patch_profile")))
            return false;
        JSONObject variants=source.optJSONObject("variant_catalog");
        if(variants==null || !profile.equals(variants.optString("profile")) ||
           variants.optString("sha256").length()!=64)return false;
        JSONArray groups=variants.optJSONArray("groups");
        int count="1.00".equals(version)?13:PortSettings.EFFECTS.length+3;
        if(groups==null || groups.length()!=count)return false;
        java.util.HashSet<String> all=new java.util.HashSet<>();
        for(int i=0;i<groups.length();i++)all.add(groups.optString(i));
        if(all.size()!=groups.length())return false;
        for(String resolution:new String[]{"720","540","360"})
            if(!all.contains("resolution_"+resolution))return false;
        String[] effects="1.00".equals(version)?
            new String[]{"ssao","motion_blur","depth_of_field",
                         "bloom","sunlight_shadows","subsurface"}:
            PortSettings.EFFECTS;
        for(String effect:effects)
            if(!all.contains("effect_"+effect))return false;
        if("1.00".equals(version))
            for(String base:new String[]{"base_null_component_transforms",
                 "base_static_ray_hit_submesh_bounds","base_frame_timeout",
                 "base_job_sleep"})
                if(!all.contains(base))return false;
        return true;
    }

    /**
     * The old 1.00 AOT had all 13 patch groups but no callable ARM64 entry
     * at +0x910580 (a jump into the function beginning at +0x910510).
     * Reject it before launch, so no repeated hard-stop on code 22.
     * Does not change the existing 1.11 compiler acceptance rules.
     */
    static boolean hasVerified100MidCall(JSONObject report) {
        if(report==null || !"1.00".equals(report.optString("version")))return false;
        if(!"tlou-100-batched-call-hotspots-v2".equals(
                report.optString("verified_100_entry_policy")))return false;
        if(!"tlou-100-batched-call-hotspots-v1".equals(
                report.optString("granular_policy")))return false;
        JSONArray entries=report.optJSONArray("verified_100_midcall_entries");
        if(entries==null || entries.length()!=1 ||
           !"+0x910580".equals(entries.optString(0)))return false;
        JSONArray command=report.optJSONArray("translation_command");
        if(command==null)return false;
        boolean extra=false,granular=false,checked=false;
        for(int i=0;i<command.length();i++) {
            String flag=command.optString(i);
            if("--extra-entries".equals(flag))extra=true;
            if("--granular-funcs".equals(flag))granular=true;
            if("--check-entries".equals(flag) && i+1<command.length()) {
                String[] offsets=command.optString(i+1).split(",");
                for(String offset:offsets)if("910580".equals(offset))checked=true;
            }
        }
        return extra && granular && checked &&
            report.optInt("extra_entry_count",0)>=1 &&
            report.optInt("granular_risk_hint_count",0)>=151552 &&
            report.optBoolean("entry_check_completed",false) &&
            (report.optBoolean("resume_enabled",false) ||
             report.optBoolean("full_rebuild_100",false));
    }

    /**
     * An existing 01.00 native library from before the entry-batch fix must
     * receive a delta rebuild. This is intentionally separate from fresh
     * installs: do not launch obsolete code, but automatically invoke the
     * already available on-device compiler on Play. Never touches 01.11.
     */
    static boolean needs100EntryRepair(Context context, File prepared) {
        try {
            if(!"100".equals(TsoProfile.gameCode(GameInstallState.marker(prepared))))
                return false;
            File work=TsoProfile.workForPrepared(context,prepared);
            File report=new File(work,"aot-report.json");
            File so=new File(work,"aot/libtlouaot.so");
            if(!report.isFile() || !so.isFile() || so.length()<4096)return false;
            JSONObject source=new JSONObject(new String(Files.readAllBytes(
                report.toPath()),StandardCharsets.UTF_8));
            // Only treat previously COMPILED 01.00 images as repairable.
            // No library -> ordinary initial compilation prompt.
            return "1.00".equals(source.optString("version")) &&
                "aarch64_generated_native_ready".equals(source.optString("status")) &&
                hasCompleteVariantCatalog(source) &&
                !hasVerified100MidCall(source);
        } catch(Exception unreadable) { return false; }
    }

    /** Lightweight status for the launcher label only. The full hash and
     * prepared-game checks in prepare() remain mandatory before launch.
     * A stale .so or an interrupted compiler is never shown as completed. */
    static boolean hasCompletedVariantBuild(Context context) {
        File work;
        try {
            work=TsoProfile.workForPrepared(
                context,new File(context.getFilesDir(),"tlou-prepared"));
        } catch(Exception noPreparedGame) { return false; }
        File so=new File(work,"aot/libtlouaot.so");
        File metaFile=new File(work,"aot/libtlouaot.json");
        File reportFile=new File(work,"aot-report.json");
        if(!so.isFile() || so.length()<4096 || !metaFile.isFile() ||
           !reportFile.isFile())return false;
        try {
            JSONObject report=new JSONObject(new String(Files.readAllBytes(
                reportFile.toPath()),StandardCharsets.UTF_8));
            String profile=report.optString("version");
            String selectedGame=TsoProfile.gameCode(GameInstallState.marker(
                new File(context.getFilesDir(),"tlou-prepared")));
            if(!("100".equals(selectedGame)&&"1.00".equals(profile) ||
                 "111".equals(selectedGame)&&"1.11".equals(profile)))return false;
            File catalog=new File(work,"1.00".equals(profile)?
                "aot/tlou-100-all-variants.txt":"aot/tlou-111-all-variants.txt");
            if(!catalog.isFile())return false;
            JSONObject meta=new JSONObject(new String(Files.readAllBytes(
                metaFile.toPath()),StandardCharsets.UTF_8));
            JSONObject built=report.optJSONObject("variant_build");
            int groups="1.00".equals(profile)?13:11;
            return "aarch64_generated_native_ready".equals(report.optString("status")) &&
                ("1.00".equals(profile)||"1.11".equals(profile)) &&
                hasCompleteVariantCatalog(report) &&
                (!"1.00".equals(profile) || hasVerified100MidCall(report)) &&
                meta.optInt("abi_version",-1)==2 &&
                (meta.optLong("abi_flags",0)&6)==6 &&
                TsoProfile.version(TsoProfile.RELAXED).equals(meta.optString("compiler_version")) &&
                built!=null && built.optInt("groups_with_code",-1)==groups &&
                built.optInt("variants_skipped",-1)==0 &&
                built.optInt("variants_planned",0)>=groups &&
                built.optInt("variants_built",0)==built.optInt("variants_planned",-1);
        } catch(Exception incomplete) {
            return false;
        }
    }

    static String pendingSelectedPatches(Context context,File prepared) throws Exception {
        File report=new File(TsoProfile.workForPrepared(context,prepared),"aot-report.json");
        if(!report.isFile())return "";
        JSONObject source=new JSONObject(new String(Files.readAllBytes(
            report.toPath()),StandardCharsets.UTF_8));
        String version=source.optString("version");
        if(!"1.00".equals(version)&&!"1.11".equals(version))return "";
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
        File work=TsoProfile.workForPrepared(context,prepared);
        File so=new File(work,"aot/libtlouaot.so");
        File json=new File(work,"aot/libtlouaot.json");
        File report=new File(work,"aot-report.json");
        File boot=new File(prepared,"boot-linked.bin");
        for(File f:new File[]{so,json,report,boot}) {
            if(!f.isFile())throw new java.io.IOException("AOT não compilado para este jogo: "+f.getName());
        }
        JSONObject meta=new JSONObject(new String(Files.readAllBytes(json.toPath()),StandardCharsets.UTF_8));
        JSONObject source=new JSONObject(new String(Files.readAllBytes(report.toPath()),StandardCharsets.UTF_8));
        String selectedGame=TsoProfile.gameCode(GameInstallState.marker(prepared));
        String compiledVersion=source.optString("version","");
        if(!("100".equals(selectedGame)&&"1.00".equals(compiledVersion) ||
             "111".equals(selectedGame)&&"1.11".equals(compiledVersion)))
            throw new java.io.IOException(
                "Esta CPU ARM64 foi compilada para outra versão do jogo ("+
                compiledVersion+"). Compile a versão selecionada, sem apagar os dados.");
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
        if("1.00".equals(source.optString("version"))) {
            // The 01.00 game writes 4 mandatory stability patches and the
            // selected resolution/effects into guest code on every launch.
            // A baseline-only AOT library always stops at +0x946180.
            // Require every option to exist as a verified native variant.
            boolean usesVariants=false,noDirty=false,resumable=false;
            if(command!=null)for(int i=0;i<command.length();i++){
                String flag=command.optString(i);
                if("--variants".equals(flag))usesVariants=true;
                if("--no-dirty-check".equals(flag))noDirty=true;
                if("--resume".equals(flag))resumable=true;
            }
            if(!usesVariants||noDirty||
               !(resumable||source.optBoolean("full_rebuild_100",false)) ||
               !hasVerified100MidCall(source) ||
               (meta.optLong("abi_flags",0)&6)!=6 ||
               !hasCompleteVariantCatalog(source) ||
               !"tlou-100-llvmO1-native-variants-v1".equals(
                   source.optString("native_codegen_profile")) ||
               source.optBoolean("runtime_code_immutable",true) ||
               !"native-variants;--no-game-hooks;--patches-at-launch".equals(
                   source.optString("runtime_patch_contract")))
                throw new java.io.IOException(
                    "CPU ARM64 1.00 antiga: faltam variantes ou a entrada +0x910580. "+
                    "Recompile a CPU uma vez em Ajustes → CPU.");
            File catalog=new File(work,"aot/tlou-100-all-variants.txt");
            JSONObject variants=source.getJSONObject("variant_catalog");
            String expectedHash=variants.optString("sha256");
            String compilerHash=meta.optString("catalog_hash");
            if(!catalog.isFile() || expectedHash.length()!=64 ||
               !expectedHash.equalsIgnoreCase(sha256(catalog)) ||
               compilerHash.length()!=16 ||
               "cbf29ce484222325".equalsIgnoreCase(compilerHash))
                throw new java.io.IOException("Catálogo ARM64 1.00 ausente ou inválido.");
            JSONObject built=source.optJSONObject("variant_build");
            if(built==null || built.optInt("groups",-1)!=13 ||
               built.optInt("groups_with_code",-1)!=13 ||
               built.optInt("variants_planned",0)<13 ||
               built.optInt("variants_built",0)!=built.optInt("variants_planned",-1) ||
               built.optInt("variants_skipped",-1)!=0)
                throw new java.io.IOException("Variantes ARM64 1.00 incompletas; recompile.");
            JSONObject selected=new JSONObject(new String(Files.readAllBytes(
                PortSettings.patchOptions(context).toPath()),StandardCharsets.UTF_8));
            compiledResolution=selected.optString("resolution","original");
            if(!java.util.Arrays.asList("original","720","540","360").contains(compiledResolution))
                throw new java.io.IOException("Resolução ARM64 não disponível.");
            RunLog.write(context,"CPU ARM64 1.00: "+built.optInt("groups")+
                " grupos de patches nativos validados; resolução "+
                compiledResolution+"; sem recompilação ao trocar opções.");
        }
        if(so.length()<4096 || meta.optLong("so_size",-1)!=so.length() ||
                !meta.optString("so_sha256").equalsIgnoreCase(sha256(so)))
            throw new java.io.IOException("Biblioteca ARM64 alterada ou corrompida");
        File engineDir=new File(context.getFilesDir(),"tlou/runtime-aot");
        File engine=new File(engineDir,"libgow3cpu.so");
        // Unpack the native AOT engine used by the executable and the
        // newly compiled game library. No FEX CPU fallback is permitted.
        stageAsset(context,"runtime-aot/libgow3cpu.so",engine,4096);
        // bb-recomp's output depends on SONAME libbbcpu.so. The tiny shim
        // depends on libgow3cpu.so, ensuring bbr_* symbols resolve from
        // the same AOT engine already used by the game executable.
        stageAsset(context,"runtime-aot/libbbcpu.so",
                   new File(engineDir,"libbbcpu.so"),512);
        return new Ready(so,engineDir,runtimeBoot,compiledResolution,pendingPatches);
    }
}
