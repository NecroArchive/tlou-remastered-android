package ro.cobrabm.fexdroid;

import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import java.util.Locale;

/** Single-line presentation and original colors from the Daedalus inFAMOUS HUD. */
final class HudPresentation {
    static final int FPS_COLOR=0xff76ff03, GPU_COLOR=0xffe040fb, CPU_COLOR=0xff00e5ff;
    static SpannableString text(String mode,double fps,double gpu,double cpu,boolean showGpu,boolean showCpu) {
        String first=String.format(Locale.ROOT,"FPS %.0f",fps);
        String gpuText="GPU "+percent(gpu),cpuText="CPU "+percent(cpu);
        String second="";
        if("full".equals(mode))second=(showGpu?gpuText:"")+(showGpu&&showCpu?" · ":"")+(showCpu?cpuText:"");
        if("compact".equals(mode))first+=fps>0?String.format(Locale.ROOT," · %.1f ms",1000.0/fps):" · — ms";
        SpannableString result=new SpannableString(first+(second.isEmpty()?"":" · "+second));
        result.setSpan(new ForegroundColorSpan(FPS_COLOR),0,first.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        if(!second.isEmpty()) {
            if(showGpu)result.setSpan(new ForegroundColorSpan(GPU_COLOR),first.length()+3,first.length()+3+gpuText.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            if(showCpu)result.setSpan(new ForegroundColorSpan(CPU_COLOR),result.length()-cpuText.length(),result.length(),Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
        return result;
    }
    static void style(android.widget.TextView view,android.content.SharedPreferences prefs) {
        float density=view.getResources().getDisplayMetrics().density;
        view.setTypeface(android.graphics.Typeface.MONOSPACE,android.graphics.Typeface.NORMAL);
        view.setTextSize(prefs.getInt("hud_size",12));
        view.setTextColor(FPS_COLOR);
        view.setSingleLine(true);view.setMaxLines(1);view.setEllipsize(android.text.TextUtils.TruncateAt.END);
        int pad=Math.round(6*density);view.setPadding(pad,pad,pad,pad);
        android.graphics.drawable.GradientDrawable bg=new android.graphics.drawable.GradientDrawable();
        bg.setColor((prefs.getInt("hud_opacity",70)*255/100)<<24);bg.setCornerRadius(4*density);view.setBackground(bg);
    }
    private static String percent(double value) {
        return Double.isNaN(value)?"N/A":String.format(Locale.ROOT,"%.0f%%",Math.max(0,Math.min(100,value)));
    }
}
