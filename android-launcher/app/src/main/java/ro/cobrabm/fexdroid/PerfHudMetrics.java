// Adapted from Daedalus GoW3 9b2f68e; shared launcher/in-game HUD.
package ro.cobrabm.fexdroid;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.LinkedHashSet;
import java.util.Locale;

/**
 * 性能 HUD 的纯计算与系统计数读取工具：全部静态方法、零 Android 依赖，
 * 让差分语义、GPU 节点发现与位置校验可以在宿主 JVM 上被 JUnit 直接覆盖
 * （PerfHudOverlay 持有 View 状态，本类只提供无状态计算）。
 *
 * 计数来源策略对齐参考实现 winlator_ludashi_plus 的 WinlatorHUD：
 * - GPU：静态路径 + devfreq/platform 目录扫描发现；kgsl gpubusy 按 busy/total
 *   即时比值解析（内核在每次读取后重置计数，不是跨 tick 累计差分）。
 * - CPU：/proc/stat 差分为主，受限设备（SELinux 拒绝 /proc/stat）回退
 *   cpufreq 频率比值 sum(scaling_cur_freq)/sum(cpuinfo_max_freq)。
 */
final class PerfHudMetrics {
    private PerfHudMetrics() {}

    /**
     * GPU busy 静态候选节点（优先级从高到低）；动态扫描根目录见 {@link #SCAN_ROOTS}。
     * 静态清单在前保证优先级，扫描发现的节点追加在后兜底。
     */
    static final String[] DEFAULT_GPU_PATHS = {
        "/sys/class/kgsl/kgsl-3d0/gpubusy",
        "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage",
        "/sys/class/kgsl/kgsl-3d0/devfreq/gpu_load",
        "/sys/class/misc/mali0/device/utilisation",
        "/sys/class/drm/card0/device/gpu_busy_percent",
        // MTK/Exynos 的常见 GPU 计数节点：部分固件对应用可读（严格 SELinux 设备仍会全部拒绝）
        "/sys/kernel/ged/hal/gpu_utilization",
        "/sys/module/ged/parameters/gpu_loading",
        "/proc/mtk_mali/utilization",
        "/sys/kernel/gpu/gpu_busy",
    };

    /** 动态扫描 GPU 计数节点的目录（对齐 WinlatorHUD detectGpuPathsOnce 的扫描面）。 */
    static final String[] SCAN_ROOTS = {
        "/sys/class/devfreq",
        "/sys/devices/virtual/devfreq",
        "/sys/devices/platform",
    };

    /** 扫描节点目录时，其下这些文件名被视为 GPU 占用计数（对齐 WinlatorHUD GPU_USAGE_FILES）。 */
    static final String[] GPU_USAGE_FILES = {
        "gpu_busy_percentage", "gpu_busy_percent", "gpu_load", "utilisation",
        "utilization", "load", "gpu_busy", "gpuinfo",
    };

    /** 节点路径/目录名含这些 token 才认为是 GPU devfreq 节点（对齐 WinlatorHUD GPU_NODE_TOKENS）。 */
    private static final String[] GPU_NODE_TOKENS = {
        "gpu", "mali", "g3d", "kgsl", "panfrost", "pvr", "powervr", "xclipse", "sgpu",
    };

    /** 占用率解析失败/不可读时约定的无效值（调用方显示 N/A）。 */
    static final int INVALID = -1;
    /** 游戏运行时进程名（comm），用于在 /proc 里发现游戏子进程。 */
    static final String GAME_COMM = "tlou-probe";

    /**
     * GPU 计数节点发现（对齐 WinlatorHUD）：静态清单里能读的直接入选，
     * 再扫描 scanRoots 下名字含 GPU token 的 devfreq/platform 节点，其下
     * 已知占用文件可读的追加进候选。宿主单测用临时目录注入 scanRoots。
     */
    static String[] discoverGpuPaths(String[] staticPaths, String[] scanRoots) {
        LinkedHashSet<String> found = new LinkedHashSet<>();
        if (staticPaths != null) {
            for (String path : staticPaths) {
                try { if (new File(path).canRead()) found.add(path); } catch (Exception ignored) {}
            }
        }
        if (scanRoots != null) {
            for (String root : scanRoots) scanGpuDir(new File(root), found);
        }
        return found.toArray(new String[0]);
    }

    private static void scanGpuDir(File root, LinkedHashSet<String> found) {
        try {
            File[] nodes = root.listFiles(File::isDirectory);
            if (nodes == null) return; // 目录不可读（本机 devfreq 被 SELinux 拒绝）就跳过
            for (File node : nodes) {
                if (!looksLikeGpuNode(node.getName()) && !looksLikeGpuNode(node.getPath())) continue;
                for (String fileName : GPU_USAGE_FILES) {
                    File candidate = new File(node, fileName);
                    try { if (candidate.canRead()) found.add(candidate.getPath()); } catch (Exception ignored) {}
                }
            }
        } catch (Exception ignored) {}
    }

    private static boolean looksLikeGpuNode(String path) {
        String lower = path.toLowerCase(Locale.US);
        for (String token : GPU_NODE_TOKENS) if (lower.contains(token)) return true;
        return false;
    }

    /**
     * 帧率差分：呈现帧计数器在 1 秒窗口内的增量。负差分（计数器重置、游戏重启）
     * 与首帧未知（last<0）都按 0 处理；计数器是单调累计值，正常情况下 only 增。
     */
    static int diffFrames(long last, long now) {
        if (last < 0 || now < 0) return 0;
        long d = now - last;
        return d > 0 ? (int)Math.min(d, Integer.MAX_VALUE) : 0;
    }

    /**
     * 帧生成生效时的帧率段（change add-native-frame-generation，spec「HUD 区分源/上屏」）：
     * multiplier>=2 且上屏率与源率不同（插帧确实在产出）时「源→上屏 LSFG Nx」
     * （如 59→118 LSFG 2x）；否则退回单一帧率——warm-up 期（上屏==源）与关闭/未生效
     * 的显示语义与无帧生成能力时一致。纯字符串函数，宿主 JUnit 直接覆盖。
     */
    static String fpsSegment(int sourceFps, int presentedFps, int multiplier) {
        if (multiplier < 2 || presentedFps <= 0 || presentedFps == sourceFps)
            return String.valueOf(sourceFps);
        return sourceFps + "→" + presentedFps + " LSFG " + multiplier + "x";
    }

    /**
     * 解析 `/proc/stat` 首行（"cpu  user nice system idle iowait irq softirq steal …"）
     * 为 {total, idle}，idle = idle + iowait。字段缺失或没有任何数字时返回 null。
     */
    static long[] cpuTotals(String firstLine) {
        if (firstLine == null) return null;
        String[] f = firstLine.trim().split("\\s+");
        // f[0] 是 "cpu" 标签；至少要有 user/nice/system/idle 四个字段才有意义。
        if (f.length < 5 || !f[0].startsWith("cpu")) return null;
        long total = 0, idle;
        try {
            for (int i = 1; i < f.length; i++) total += Long.parseLong(f[i]);
            idle = Long.parseLong(f[4]) + (f.length > 5 ? Long.parseLong(f[5]) : 0L);
        } catch (NumberFormatException ex) { return null; }
        return new long[]{total, idle};
    }

    /**
     * 两次 CPU 采样的占用百分比：100×(Δtotal−Δidle)/Δtotal。Δtotal 无效（时钟回拨、
     * 采样过密）时返回 {@link #INVALID}，调用方当次显示 N/A。
     */
    static int cpuUsage(long[] prev, long[] cur) {
        if (prev == null || cur == null) return INVALID;
        long dt = cur[0] - prev[0], di = cur[1] - prev[1];
        if (dt <= 0) return INVALID;
        long busy = dt - di;
        if (busy < 0) busy = 0;
        return (int)Math.min(100, Math.round(busy * 100.0 / dt));
    }

    /**
     * cpufreq 负载估算（Winlator 的 CPU 回退方案）：逐核 sum(scaling_cur_freq)/
     * sum(cpuinfo_max_freq)。只统计 cur>0 且 max>0 的核（热插拔离线核读不到为 0，
     * 自动跳过）；没有可用核时返回 {@link #INVALID}。
     */
    static int cpuFreqLoad(long[] curFreqs, long[] maxFreqs) {
        if (curFreqs == null || maxFreqs == null || curFreqs.length != maxFreqs.length) return INVALID;
        long cur = 0, max = 0;
        for (int i = 0; i < curFreqs.length; i++) {
            if (curFreqs[i] > 0 && maxFreqs[i] > 0) { cur += curFreqs[i]; max += maxFreqs[i]; }
        }
        return max > 0 ? (int)Math.max(0, Math.min(100, cur * 100 / max)) : INVALID;
    }

    /**
     * 解析进程 `/proc/<pid>/stat` 为 {ppid, utime+stime}（时钟节拍）。comm 字段
     * 可能含空格/括号，因此取最后一个 ')' 之后的字段：state(1) ppid(2) … utime(12) stime(13)。
     */
    static long[] procStatFields(String statLine) {
        if (statLine == null) return null;
        int close = statLine.lastIndexOf(')');
        if (close < 0) return null;
        String[] f = statLine.substring(close + 1).trim().split("\\s+");
        if (f.length < 13) return null;
        try { return new long[]{Long.parseLong(f[1]), Long.parseLong(f[11]) + Long.parseLong(f[12])}; }
        catch (NumberFormatException ex) { return null; }
    }

    /** 进程 stat 的 (comm)（如 "iss-probe"）；格式不符返回 null。 */
    static String procComm(String statLine) {
        if (statLine == null) return null;
        int open = statLine.indexOf('('), close = statLine.lastIndexOf(')');
        if (open < 0 || close <= open) return null;
        return statLine.substring(open + 1, close);
    }

    /**
     * 游戏进程 CPU 占用（占全部核心的百分比）：Δ(utime+stime)/(USER_HZ·Δwall·cores)。
     * USER_HZ 在 Android/arm64 内核恒为 100。首帧、回拨或无效输入返回 {@link #INVALID}。
     */
    static int processCpuUsage(long prevTicks, long curTicks, long prevNanos, long curNanos, int cores) {
        if (prevTicks < 0 || curTicks < prevTicks || prevNanos <= 0 || curNanos <= prevNanos || cores <= 0) {
            return INVALID;
        }
        double wallS = (curNanos - prevNanos) / 1e9;
        double pct = (curTicks - prevTicks) / (wallS * cores);
        return (int)Math.max(0, Math.min(100, Math.round(pct)));
    }

    /** 读一个文本计数文件的首行；路径不存在、权限拒绝或任何 I/O 问题返回 null（降级 N/A）。 */
    static String readFirstLine(String path) {
        if (path == null) return null;
        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            String line = r.readLine();
            return line == null ? null : line.trim();
        } catch (Exception ex) {
            return null; // SELinux 拒绝、节点不存在等都走这里，不允许抛出中断采样循环
        }
    }

    /**
     * 解析即时百分比文本（如 "42 %"、"37"），夹到 0..100；不含数字返回 {@link #INVALID}。
     * 注意：kgsl `gpubusy` 不能走这里（两个数字，语义不同），见 {@link #gpuBusyRatio}。
     */
    static int parsePercent(String text) {
        if (text == null) return INVALID;
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("\\d+").matcher(text);
        if (!m.find()) return INVALID;
        long v;
        try { v = Long.parseLong(m.group()); } catch (NumberFormatException ex) { return INVALID; }
        return (int)Math.max(0, Math.min(100, v));
    }

    /**
     * kgsl `gpubusy` 的 "busy total"：读取即重置的窗口计数，busy/total 即上一个
     * 采样窗口的占用比（对齐 WinlatorHUD；不做跨 tick 差分）。total<=0 返回
     * {@link #INVALID}（GPU 完全空闲时 total 也可能是 0）。
     */
    static int gpuBusyRatio(String text) {
        if (text == null) return INVALID;
        String[] f = text.trim().split("\\s+");
        if (f.length < 2) return INVALID;
        long busy, total;
        try { busy = Long.parseLong(f[0]); total = Long.parseLong(f[1]); }
        catch (NumberFormatException ex) { return INVALID; }
        if (total <= 0) return INVALID;
        if (busy < 0) busy = 0;
        return (int)Math.max(0, Math.min(100, busy * 100 / total));
    }

    /** MTK `mtk_mali/utilization` 的 ACTIVE= token（行内可能还有 CLK 等其他字段）。 */
    static int mtkActive(String text) {
        if (text == null) return INVALID;
        int idx = text.indexOf("ACTIVE=");
        if (idx < 0) return INVALID;
        return parsePercent(text.substring(idx + 7));
    }

    /**
     * 持久化位置校验（design D4 / spec「异常位置数据不致丢失 HUD」）：
     * 缺失（null）、NaN/Infinity 或完全出屏 → 回退默认位置（右上角）；
     * 其余按可视范围夹紧，保证 HUD 永远可见、可拖回。返回 {x, y}。
     */
    static float[] resolvePosition(Float storedX, Float storedY,
                                   float screenW, float screenH,
                                   float hudW, float hudH, float margin) {
        float defX = Math.max(margin, screenW - hudW - margin);
        float defY = margin;
        boolean bad = storedX == null || storedY == null
                || storedX.isNaN() || storedY.isNaN()
                || storedX.isInfinite() || storedY.isInfinite()
                // 完全出屏（右/下越界或左/上越出）按损坏数据处理
                || storedX >= screenW || storedY >= screenH
                || storedX + hudW <= 0 || storedY + hudH <= 0;
        float x = bad ? defX : storedX, y = bad ? defY : storedY;
        float maxX = Math.max(0, screenW - hudW), maxY = Math.max(0, screenH - hudH);
        return new float[]{Math.max(0, Math.min(x, maxX)), Math.max(0, Math.min(y, maxY))};
    }
}

