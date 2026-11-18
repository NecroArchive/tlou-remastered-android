package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/** Release-wide dialog palette, including a readable scrollable log inspector. */
final class TlouDialogs {
    private static final int BACKGROUND=0xff101613;
    private static final int PANEL=0xff1b2520;
    private static final int BUTTON=0xff303c33;
    private static final int INK=0xffe7e9df;
    private static final int MUTED=0xffabb8a4;
    private static final int OLIVE=0xffc2cf96;
    private TlouDialogs() {}
    static String version(Activity activity) {
        try {
            return activity.getPackageManager().getPackageInfo(activity.getPackageName(),0).versionName;
        } catch(Exception ignored) {
            return "0.1.1";
        }
    }
    static int dp(Context c,int value) {
        return Math.round(value*c.getResources().getDisplayMetrics().density);
    }
    static GradientDrawable background(Context c,int color,int radius) {
        GradientDrawable bg=new GradientDrawable();
        bg.setColor(color);
        bg.setCornerRadius(dp(c,radius));
        bg.setStroke(dp(c,1),0xff52634b);
        return bg;
    }
    static void styleButton(Button button,boolean primary) {
        Context context=button.getContext();
        button.setAllCaps(false);
        button.setTextSize(13);
        button.setTextColor(primary?BACKGROUND:INK);
        button.setBackgroundTintList(null);
        button.setBackground(background(context,primary?OLIVE:BUTTON,10));
        button.setMinHeight(dp(context,44));
        button.setPadding(dp(context,10),0,dp(context,10),0);
    }
    static AlertDialog show(Activity activity,AlertDialog.Builder builder) {
        AlertDialog dialog=builder.create();
        dialog.show();
        decorate(dialog);
        return dialog;
    }
    static void decorate(AlertDialog dialog) {
        Window window=dialog.getWindow();
        if(window!=null) {
            window.setBackgroundDrawable(background(dialog.getContext(),PANEL,18));
            window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            WindowManager.LayoutParams attributes=window.getAttributes();
            attributes.dimAmount=.66f;
            window.setAttributes(attributes);
            int width=dialog.getContext().getResources().getDisplayMetrics().widthPixels;
            window.setLayout(Math.min(dp(dialog.getContext(),500),width-dp(dialog.getContext(),32)),-2);
        }
        Button positive=dialog.getButton(AlertDialog.BUTTON_POSITIVE);
        Button neutral=dialog.getButton(AlertDialog.BUTTON_NEUTRAL);
        Button negative=dialog.getButton(AlertDialog.BUTTON_NEGATIVE);
        if(positive!=null)styleButton(positive,true);
        if(neutral!=null)styleButton(neutral,false);
        if(negative!=null)styleButton(negative,false);
    }
    private static TextView text(Activity activity,String label,int size,int color) {
        TextView view=new TextView(activity);
        view.setText(TlouLanguage.t(activity,label));
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }
    private static Button logButton(Activity activity,String label,boolean primary,Runnable callback) {
        Button button=new Button(activity);
        button.setText(TlouLanguage.t(activity,label));
        styleButton(button,primary);
        button.setOnClickListener(v->callback.run());
        return button;
    }
    static Dialog showLogs(Activity activity,String logs,Runnable export) {
        Dialog dialog=new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout panel=new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(activity,18),dp(activity,18),dp(activity,18),dp(activity,16));
        panel.setBackground(background(activity,PANEL,20));

        TextView title=text(activity,"TLOU — logs",20,INK);
        title.setTypeface(Typeface.create("sans-serif-condensed",Typeface.BOLD));
        panel.addView(title);
        TextView subtitle=text(activity,"DAEDALUS / ANDROID  •  v"+version(activity),11,OLIVE);
        subtitle.setLetterSpacing(.08f);
        subtitle.setPadding(0,dp(activity,6),0,dp(activity,12));
        panel.addView(subtitle);

        ScrollView scroll=new ScrollView(activity);
        scroll.setFillViewport(false);
        scroll.setVerticalScrollBarEnabled(true);
        scroll.setBackground(background(activity,BACKGROUND,12));
        TextView logText=text(activity,logs,12,INK);
        logText.setFontFeatureSettings("tnum");
        logText.setTypeface(Typeface.MONOSPACE);
        logText.setTextIsSelectable(true);
        logText.setLineSpacing(dp(activity,2),1f);
        logText.setPadding(dp(activity,14),dp(activity,14),dp(activity,14),dp(activity,14));
        scroll.addView(logText);
        LinearLayout.LayoutParams scrollParams=new LinearLayout.LayoutParams(-1,0,1);
        scrollParams.bottomMargin=dp(activity,14);
        panel.addView(scroll,scrollParams);

        LinearLayout actions=new LinearLayout(activity);
        actions.setGravity(Gravity.CENTER_VERTICAL);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        Button copy=logButton(activity,"Copiar",false,()->{
            ClipboardManager clipboard=(ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
            if(clipboard!=null)clipboard.setPrimaryClip(ClipData.newPlainText("TLOU logs",logs));
            Toast.makeText(activity,TlouLanguage.t(activity,"Logs copiados"),Toast.LENGTH_SHORT).show();
        });
        LinearLayout.LayoutParams buttonParams=new LinearLayout.LayoutParams(0,dp(activity,46),1);
        actions.addView(copy,buttonParams);
        if(export!=null) {
            Button exportBtn=logButton(activity,"Exportar",false,()->{dialog.dismiss();export.run();});
            LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,dp(activity,46),1);
            p.leftMargin=dp(activity,8);actions.addView(exportBtn,p);
        }
        Button close=logButton(activity,"Fechar",true,dialog::dismiss);
        LinearLayout.LayoutParams closeParams=new LinearLayout.LayoutParams(0,dp(activity,46),1);
        closeParams.leftMargin=dp(activity,8);actions.addView(close,closeParams);
        panel.addView(actions);
        dialog.setContentView(panel);
        dialog.show();
        Window window=dialog.getWindow();
        if(window!=null) {
            window.setBackgroundDrawableResource(android.R.color.transparent);
            window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            WindowManager.LayoutParams params=window.getAttributes();
            params.dimAmount=.7f;
            window.setAttributes(params);
            int width=activity.getResources().getDisplayMetrics().widthPixels;
            int height=activity.getResources().getDisplayMetrics().heightPixels;
            window.setLayout(Math.min(dp(activity,760),width-dp(activity,32)),
                Math.min(dp(activity,750),height-dp(activity,48)));
        }
        return dialog;
    }
}
