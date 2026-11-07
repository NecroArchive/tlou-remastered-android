package ro.cobrabm.fexdroid;

import android.content.Context;
import java.util.Locale;

/**
 * Non-root Adreno Turbo, matching Bachata S4+ and libadrenotools:
 * /dev/kgsl-3d0 -> IOCTL_KGSL_SETPROPERTY(KGSL_PROP_PWRCTRL).
 * Restores normal scaling on session end. Thermal limits still apply.
 */
final class GpuTurboSession {
    private boolean active;

    synchronized boolean start(Context context) {
        if(active || !PortSettings.prefs(context).getBoolean("gpu_turbo",false))return active;
        String gpu;
        try { gpu=BuiltinDrivers.renderer().toLowerCase(Locale.ROOT); }
        catch(Throwable error) { RunLog.write(context,"Adreno Turbo: GPU não detectada"); return false; }
        if(!gpu.contains("adreno")) {
            RunLog.write(context,"Adreno Turbo ignorado para GPU não-Adreno: "+gpu);
            return false;
        }
        try {
            active=Gow3Present.adrenoTurbo(true);
            RunLog.write(context,active?"Adreno Turbo ativado sem root (KGSL PWRCTRL)":
                "Adreno Turbo indisponível: ioctl KGSL recusado; jogo continua normalmente");
        } catch(Throwable error) {
            active=false;RunLog.write(context,"Adreno Turbo não ativado: "+error);
        }
        return active;
    }
    synchronized void stop(Context context) {
        if(!active)return;
        boolean restored=Gow3Present.adrenoTurbo(false);
        RunLog.write(context,restored?"Adreno Turbo desativado; escala dinâmica normal restaurada":
            "Aviso: não foi possível restaurar o estado KGSL; verifique o dispositivo");
        active=false;
    }
    synchronized boolean isActive() {return active;}
}
