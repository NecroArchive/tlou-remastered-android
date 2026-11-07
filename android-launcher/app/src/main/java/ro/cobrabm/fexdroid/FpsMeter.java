package ro.cobrabm.fexdroid;

/**
 * Monotonic presentation FPS, with min/max tracked from valid half-second
 * windows. This is NOT GPU timing; it never invents CPU or GPU utilization.
 */
final class FpsMeter {
    private long frames,time;
    private boolean initialized;
    private double fps,minimum=Double.NaN,maximum=Double.NaN;
    private long presented;

    double sample(long totalFrames,long nowNanos) {
        if(!initialized || totalFrames<frames || nowNanos<=time) {
            initialized=true;frames=totalFrames;time=nowNanos;fps=0;
            minimum=Double.NaN;maximum=Double.NaN;presented=totalFrames;
            return 0;
        }
        long elapsed=nowNanos-time;
        if(elapsed>=500_000_000L) {
            fps=(totalFrames-frames)*1_000_000_000.0/elapsed;
            frames=totalFrames;time=nowNanos;presented=totalFrames;
            // A 500 ms period with no displayed frames is a real zero-FPS stall;
            // it must appear in MIN instead of being silently filtered out.
            minimum=Double.isNaN(minimum)?fps:Math.min(minimum,fps);
            maximum=Double.isNaN(maximum)?fps:Math.max(maximum,fps);
        }
        return fps;
    }
    double minimum(){return Double.isNaN(minimum)?0:minimum;}
    double maximum(){return Double.isNaN(maximum)?0:maximum;}
    long presented(){return presented;}
    void reset(){initialized=false;fps=0;minimum=Double.NaN;maximum=Double.NaN;presented=0;}
}
