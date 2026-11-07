package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Full-screen, TLOU-themed game-over/error surface; never prints runtime logs. */
final class GameFailureScreen {
    private static final int BG=0xff0b100e,TEXT=0xffeff0e7,MUTED=0xffbac4b1,ACCENT=0xffc1cf94;
    private GameFailureScreen(){}
    private static int dp(Activity a,int value){
        return Math.round(value*a.getResources().getDisplayMetrics().density);
    }
    private static TextView line(Activity a,String text,int sp,int color,boolean bold){
        TextView out=new TextView(a);
        out.setText(text);out.setTextColor(color);out.setTextSize(sp);
        out.setTypeface(Typeface.create("sans-serif-condensed",
            bold?Typeface.BOLD:Typeface.NORMAL));
        out.setGravity(Gravity.CENTER_HORIZONTAL);
        return out;
    }
    static void show(Activity activity,GameFailureMessage reason,Runnable back){
        if(activity.isFinishing()||activity.isDestroyed())return;
        Dialog dialog=new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setCancelable(false);
        FrameLayout frame=new FrameLayout(activity);
        frame.setBackgroundColor(BG);
        ImageView image=new ImageView(activity);
        image.setImageResource(R.drawable.tlou_hero);
        image.setScaleType(ImageView.ScaleType.CENTER_CROP);
        image.setAlpha(0.16f);
        frame.addView(image,new FrameLayout.LayoutParams(-1,-1));
        ScrollView scroll=new ScrollView(activity);
        scroll.setFillViewport(true);
        frame.addView(scroll,new FrameLayout.LayoutParams(-1,-1));
        LinearLayout outer=new LinearLayout(activity);
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(activity,20),dp(activity,20),dp(activity,20),dp(activity,20));
        scroll.addView(outer,new ScrollView.LayoutParams(-1,-1));
        LinearLayout panel=new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER_HORIZONTAL);
        panel.setPadding(dp(activity,28),dp(activity,24),dp(activity,28),dp(activity,26));
        GradientDrawable shape=new GradientDrawable();
        shape.setColor(0xf21b241e);
        shape.setCornerRadius(dp(activity,10));
        shape.setStroke(dp(activity,1),0xff536a4c);
        panel.setBackground(shape);
        outer.addView(panel,new LinearLayout.LayoutParams(
            Math.min(dp(activity,700),activity.getResources().getDisplayMetrics().widthPixels-dp(activity,40)),-2));
        TextView brand=line(activity,"THE LAST OF US  ·  REMASTERED",15,ACCENT,true);
        brand.setLetterSpacing(.12f);
        panel.addView(brand);
        TextView heading=line(activity,reason.title,23,TEXT,true);
        LinearLayout.LayoutParams hp=new LinearLayout.LayoutParams(-1,-2);
        hp.topMargin=dp(activity,17);panel.addView(heading,hp);
        TextView explanation=line(activity,reason.description,15,TEXT,false);
        LinearLayout.LayoutParams ep=new LinearLayout.LayoutParams(-1,-2);
        ep.topMargin=dp(activity,14);panel.addView(explanation,ep);
        TextView hint=line(activity,reason.action,14,MUTED,false);
        LinearLayout.LayoutParams ap=new LinearLayout.LayoutParams(-1,-2);
        ap.topMargin=dp(activity,10);panel.addView(hint,ap);
        TextView code=line(activity,reason.code+"  ·  Diagnóstico salvo no launcher",12,ACCENT,true);
        LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(-1,-2);
        cp.topMargin=dp(activity,20);panel.addView(code,cp);
        Button button=new Button(activity);
        button.setText("VOLTAR AO LAUNCHER");
        button.setAllCaps(false);button.setTextColor(BG);
        GradientDrawable buttonShape=new GradientDrawable();
        buttonShape.setColor(ACCENT);buttonShape.setCornerRadius(dp(activity,6));
        button.setBackground(buttonShape);
        LinearLayout.LayoutParams bp=new LinearLayout.LayoutParams(-1,dp(activity,48));
        bp.topMargin=dp(activity,23);
        panel.addView(button,bp);
        button.setOnClickListener(v->{dialog.dismiss();back.run();});
        dialog.setContentView(frame);
        Window window=dialog.getWindow();
        if(window!=null){
            window.setBackgroundDrawable(new ColorDrawable(BG));
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.MATCH_PARENT);
        }
        dialog.show();
        window=dialog.getWindow();
        if(window!=null)window.setLayout(-1,-1);
    }
}
