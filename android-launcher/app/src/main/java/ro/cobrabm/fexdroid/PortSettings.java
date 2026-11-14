package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONObject;
import java.io.File;
import java.nio.file.Files;
import java.nio.charset.StandardCharsets;
import java.util.Map;

/** Log 32 launch profile. Experimental A/B switches are NOT user preferences. */
final class PortSettings {
    static final String[] EFFECTS={"ssao","motion_blur","depth_of_field","bloom","sunlight_shadows","subsurface","omni_light_shadows","spot_light_shadows"};
    static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("tlou",Context.MODE_PRIVATE);
    }

    // Existing player-selected game, driver, patch and resolution preferences survive upgrades.
    // Old experiment preferences cannot silently reactivate discarded modes.
    static void retireTestPreferences(Context context) {
        SharedPreferences.Editor edit=prefs(context).edit();
        for(String key:new String[]{"memory_walk_once","ring_binding_cursor","draw_counter_trim",
            "compute_perm_memo","host_copy_batch","srt_hot_readbacks","fetch_no_block",
            "land_on_guest","static_min_512","stack_mapped_readback","inline_single_copy",
            "split_cs64","mapped_fault_readbacks","mapped_readbacks","gpu_profile",
            "prep_pool","cpu_trims","prefetch_pipelines","parallel_pipelines",
            "prep_workers","frames_ahead","descriptor_bulk","readback","sotc_precise_narrow",
            "hud_gpu","hud_cpu",
            "compute_wait_fast","relaxed_skinning_fp"}) edit.remove(key);
        String limit=prefs(context).getString("fps_limit","60");
        if(!"30".equals(limit) && !"60".equals(limit))edit.putString("fps_limit","60");
        // Preserve an explicit old OFF preference while making Auto the default
        // for users who had the original always-visible default enabled.
        if(prefs(context).contains("touch_visible") &&
           !prefs(context).contains("touch_mode"))
            edit.putString("touch_mode",prefs(context).getBoolean("touch_visible",true)?"auto":"off");
        edit.remove("touch_visible");
        // The proxy/compatibility renderer was retired. Ignore old saved choices.
        edit.remove("render_path");
        edit.apply();
    }

    static File patchOptions(Context context) throws Exception {
        SharedPreferences p=prefs(context);
        JSONObject options=new JSONObject();
        // GPU-heavy proxy scaling kept the game native at 1080p and resolved
        // intermediate targets repeatedly. Direct 01.00 executable scaling
        // reduces ALL scene/post allocations; the existing Vulkan UI composer
        // expands the completed LDR background before drawing the HUD at 1080p.
        // "proxy" is the earlier fallback with separately allocated Vulkan
        // targets. The user has tested the direct-performance method at 720p
        // and original 1080p, NOT the proxy fallback in this build.
        // Never silently change the saved resolution.
        final String resolution=p.getString("resolution","original");
        options.put("resolution",resolution);
        // F5 regression isolation (2026-11-08): return SM7475/23049PCD8G
        // to the same sleeping-worker patch already used on other devices.
        // The previously observed HALT after 806 s remains unproven to be
        // caused by this patch; validate both FPS and >806 s stability.
        options.put("job_sleep",true);
        for(String key:EFFECTS) options.put(key,p.getBoolean(key,false));
        File file=new File(context.getFilesDir(),"patch-options.json");
        Files.write(file.toPath(),options.toString().getBytes(StandardCharsets.UTF_8));
        return file;
    }

    static void environment(Context context,Map<String,String> env) {
        SharedPreferences p=prefs(context);
        String resolution=p.getString("resolution","original");
        // The proven unified path is mandatory: no legacy proxy/composite mode.
        // Keep the original movie decoder, scale scene/HUD/aim as one domain.
        final String scaledPreset=resolution.equals("720")?"1":
            resolution.equals("540")?"3":resolution.equals("360")?"4":null;
        env.put("TLOU_EXPECT_SCENE_SIZE","1920x1080");
        env.put("GOW3_UPSCALER","off");
        env.put("GOW3_OUTPUT_RES","1920x1080");
        env.remove("GOW3_RENDER_RES");
        env.remove("GOW3_UPSCALE_PRESET");
        env.remove("TLOU_SPATIAL_SCENE");
        env.remove("TLOU_HOST_SCENE_MODE");
        if(scaledPreset!=null) {
            final String actualSize=resolution.equals("720")?"1280x720":
                resolution.equals("540")?"960x540":"640x360";
            env.put("TLOU_UNIFIED_GUEST_UI","1");
            env.put("TLOU_EXPECT_SCENE_SIZE",actualSize);
            env.put("TLOU_EXPECT_HOST_SCENE",actualSize);
            env.put("GOW3_MOVIE_SIZE",actualSize);
        } else {
            env.remove("TLOU_UNIFIED_GUEST_UI");
            env.remove("GOW3_MOVIE_SIZE");
            env.remove("TLOU_EXPECT_HOST_SCENE");
        }

        // Exact flags observed in launch.log of TLOU-Android-Logs (32).zip.
        // Its preceding launch-previous.log used different v0.31 trial flags:
        // the more recent actual run had the three below OFF.
        env.put("TLOU_MEMORY_WALK_ONCE","1");
        env.put("TLOU_RING_BINDING_CURSOR","1");
        // Disable per-packet ARM64 clock barriers and shared stats atomics in normal gameplay.
        // CPU thread-load diagnostics use pthread clocks; full pipe stats remain opt-in.
        env.put("TLOU_DRAW_COUNTER_TRIM","1");
        // TLOU ZIP 30/31: exact memo saw 0 hits across >200k compute lookups/5s.
        // Retain the old stable default: avoid descriptor comparisons and memo bookkeeping.
        env.put("GOW3_TLOU_COMPUTE_MEMO","0");
        env.put("GOW3_TLOU_HOST_COPY_BATCH","0");
        env.put("TLOU_SRT_HOT_READBACKS","1");
        env.put("TLOU_SRT_DEP_BATCH","0");
        env.put("GOW3_FETCH_NO_BLOCK","1");
        env.put("GOW3_LAND_ON_GUEST","1");
        env.put("GOW3_STATIC_MIN_BYTES","512");
        env.put("TLOU_STACK_MAPPED_READBACK","1");
        env.put("TLOU_INLINE_SINGLE_COPY","1");
        env.put("TLOU_SPLIT_CS64","1");
        env.put("TLOU_MAPPED_FAULT_READBACKS","1");
        env.put("TLOU_MAPPED_READBACKS","1");
        // 2026-11-13 CPU-bound gameplay: 850k post-draw calls and ~750k shader
        // scans every 5 s. Profiling these hot paths in a production session
        // distorts the saturated GPU-command thread even with sparse samples.
        // Keep the native scanner/NEON implementation unchanged and make its
        // diagnostics strictly opt-in via existing AOT detailed diagnostics.
        final boolean detailedAotDiagnostics =
                p.getBoolean("aot_detailed_diagnostics",false);
        // Native phase sampler defaults ON when env is absent; force 0 here.
        env.put("TLOU_CPU_PHASE_SAMPLES",detailedAotDiagnostics?"1":"0");
        // The shader scanner profile checks env presence, not merely "1".
        if(detailedAotDiagnostics) env.put("TLOU_SHADER_SCAN_PROFILE","1");
        else env.remove("TLOU_SHADER_SCAN_PROFILE");
        env.put("TLOU_SHADER_SCAN_SCALAR",
                p.getBoolean("shader_scan_scalar",false)?"1":"0");
        // Leave profiling truly disabled. Some native switches check presence,
        // so setting "0" instead of removing is incorrect.
        for(String name:new String[]{"GOW3_GPU_PROFILE","GOW3_FRAME_STATS","GOW3_PERF_DIAG"})
            env.remove(name);

        env.put("GOW3_READBACKS","1"); // Precise=2 caused artifacts; never resurrect.
        env.put("GOW3_EOP_READBACK","1"); // Selective readbacks, not Precise/Readback 2.
        // Readback 1 stays on at all levels. Every exact physics EOP result
        // already calls DownloadRangesAsync(..., protect=true, yield_to_cpu=true),
        // which read-protects its own pages without a global switch.
        // Protecting ALL other pending readbacks caused many unrelated page
        // faults and a sharp performance regression on Adreno 830.
        // Only shader-independent Full geometry synchronization is retained:
        // both user-provided 720p and native 1080p logs used Full, without the
        // previously suspected performance regression. No more Fast/Balanced
        // profiles which can return character deformation.
        env.put("TLOU_AUTO_SMALL_CS_READBACK","2");
        // Full geometry coverage remains enabled. Only the explicit EOP/physics
        // results are page-protected via DownloadRangesAsync(..., protect=true).
        // Global async protection also faults unrelated image and soft readbacks,
        // unlike the other Android ports, and needlessly serializes guest threads.
        // Important: do NOT disable GOW3_EOP_READBACK or downgrade Full coverage.
        env.remove("GOW3_ASYNC_READBACK");
        // Coverage audit was completed in earlier device logs. Do not run
        // its atomic identity tracking on the hot compute-binding path during
        // normal gameplay; an explicit native diagnostic can still enable it.
        env.remove("TLOU_PHYSICS_COVERAGE");
        env.put("GOW3_ASYNC_SHADERS","1");
        env.put("GOW3_FRAMES_AHEAD","0");
        env.put("TLOU_DESCRIPTOR_BULK","0");
        env.put("GOW3_PREP_POOL","1");
        env.put("GOW3_CPU_TRIMS","1");
        env.put("GOW3_PREFETCH_PIPELINES","1");
        env.put("GOW3_PARALLEL_PIPELINES","1");
        // TLOU: previously tested draw-preparation workers increased CPU
        // contention without raising FPS. Logs 19/20 still showed ~39 FPS
        // despite 99.9% hits with two workers. Restore the proven zero-worker
        // policy; do not keep the speculative normal-priority scanner or
        // per-5-second hit instrumentation running in gameplay.
        env.put("GOW3_PREP_WORKERS","0");
        // Real NDK ADPF hint session belongs to the separate Bionic game PID,
        // not the Java launcher. OFF by default for reproducible A/B tests.
        // Optional, always OFF by default. Changes thread affinity, never the CPU frequency ceiling.
        // Do not enable automatically: command, draw and recorder threads could contend on two prime cores.
        env.put("TLOU_FAST_RENDER_CORES",p.getBoolean("fast_render_cores",false)?"1":"0");
        // Optional AOT-only A/B: GPU command and draw recorder get separate
        // PRIME cores if (and only if) the allowed CPU topology has exactly
        // two PRIME cores. The six PS4 job workers retain the launch mask.
        // FEX always ignores this preference to preserve the benchmark base.
        final boolean nativeAot="aot".equals(p.getString("cpu_backend","fex"));
        env.put("TLOU_AOT_SPLIT_PRIME",nativeAot &&
                p.getBoolean("aot_gpu_prime_split_v2",false) ? "1" : "0");
        env.put("GOW3_ADPF", p.getBoolean("adpf",false) ? "1" : "0");
        env.remove("TLOU_PREP_NORMAL_PRIORITY");
        env.remove("TLOU_PREP_HIT_STATS");
        env.put("GOW3_DEFERRED_READBACK","0");
        env.put("GOW3_STALE_READBACK","0");

        // FPS unlock for TLOU: the default PS4 vblank at 60 Hz caps flips
        // regardless of guest work. Fast 480 Hz guest vblank plus a bounded
        // 120 FPS present limit enables >60 FPS without an unbounded loop.
        // VideoOutDriver retains its 60 Hz movie-specific pacing.
        final boolean unlocked=p.getBoolean("fps_unlock",false);
        env.put("GOW3_VBLANK_HZ",unlocked?"0":"60");
        // Merely requesting 480 Hz guest vblank is insufficient: when VSync
        // remains ON, both VideoOut's adaptive pacing and Android's FIFO
        // compositor revert to the physical refresh (60 Hz in log 6).
        // Disable VSync only for this unlocked launch. The user's saved VSync
        // choice remains untouched for regular capped launches.
        env.put("GOW3_VSYNC",unlocked?"0":p.getBoolean("vsync",true)?"1":"0");
        env.put("GOW3_FPS_LIMIT",fpsLimit(context));
    }

    static String experimentReport(Context context,Map<String,String> env) {
        StringBuilder result=new StringBuilder("TLOU launch settings: resolution=")
            .append(prefs(context).getString("resolution","original"));
        result.append("; renderer=Vulkan; CPU=").append("aot".equals(prefs(context).getString("cpu_backend","fex"))?"native AOT ARM64 (experimental)":"FEX ARM64");
        result.append("; shader-scan-diagnostic=").append(env.containsKey("TLOU_SHADER_SCAN_PROFILE")?"on":"off");
        result.append("; shader-scan-method=")
              .append(prefs(context).getBoolean("shader_scan_scalar",false)?"original":"NEON");
        result.append("; game-raster=").append(env.getOrDefault("TLOU_EXPECT_SCENE_SIZE","1920x1080"));
        result.append("; game-ui=")
              .append(env.containsKey("TLOU_UNIFIED_GUEST_UI")
                      ?env.getOrDefault("TLOU_EXPECT_SCENE_SIZE","1920x1080")
                      :"1920x1080");
        result.append("; video-decoding=original; movie-plane-layout=")
              .append(env.getOrDefault("GOW3_MOVIE_SIZE","native"));
        result.append("; host-scene=")
              .append(env.getOrDefault("TLOU_EXPECT_HOST_SCENE","native"));
        result.append("; scene-composite=")
              .append(env.containsKey("TLOU_UNIFIED_GUEST_UI")?"unified-direct-no-ui-blit":
                      env.containsKey("TLOU_SPATIAL_SCENE")?"proxy-spatial-compat":"native");
        result.append("; temporal-fsr=off");
        result.append("; frames-in-flight=").append(env.getOrDefault("GOW3_FRAMES_IN_FLIGHT","0"));
        result.append("; geometry-sync=full");
        result.append("; async-global-protect=").append(env.containsKey("GOW3_ASYNC_READBACK")?"on":"off");
        result.append("; geometry-sync policy=2 full only");
        result.append("; fps-unlock=").append(prefs(context).getBoolean("fps_unlock",false)?"120":"off");
        result.append("; effective-vsync=").append(env.getOrDefault("GOW3_VSYNC","?"));
        result.append("; virtual-vblank-hz=").append(env.getOrDefault("GOW3_VBLANK_HZ","?"));
        result.append("; adpf-native=").append(env.getOrDefault("GOW3_ADPF","0"));
        result.append("; fast-render-cores=").append(env.getOrDefault("TLOU_FAST_RENDER_CORES","0"));
        result.append("; aot-split-prime=").append(env.getOrDefault("TLOU_AOT_SPLIT_PRIME","0"));
        result.append("; compute-exact-memo=").append(env.getOrDefault("GOW3_TLOU_COMPUTE_MEMO","0"));
        result.append("; guest-job-worker-wake=patched-sleep");
        return result.toString();
    }

    static String fpsLimit(Context context) {
        if(prefs(context).getBoolean("fps_unlock",false))return "120";
        return "30".equals(prefs(context).getString("fps_limit","60"))?"30":"60";
    }
    static String summary(Context context) {
        SharedPreferences p=prefs(context);
        String r=p.getString("resolution","original"), limit=fpsLimit(context);
        String res=r.equals("original")?"Original":r+"p";
        return res+"  ·  "+(limit+" FPS")+"  ·  "+
            (p.getBoolean("fps_unlock",false)?"VSync OFF (desbloqueio)":
             p.getBoolean("vsync",true)?"VSync ON":"VSync OFF");
    }
}
