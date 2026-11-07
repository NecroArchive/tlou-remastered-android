package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

/** Contact/project cards follow Daedalus GoW3 AboutScreen/ContactCard/GitHubCard. */
final class AboutPanel {
    private final Activity activity;
    private static final int TEXT=0xffe7e9df,MUTED=0xffa6b0a4,ACCENT=0xffc2cf96;
    private AboutPanel(Activity activity){this.activity=activity;}
    static LinearLayout create(Activity a,Runnable logs,Runnable export) {
        return new AboutPanel(a).build(logs,export);
    }
    private String tr(String s){return TlouLanguage.t(activity,s);}
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    private TextView text(String value,int size,int color){
        TextView t=new TextView(activity);t.setText(tr(value));t.setTextSize(size);t.setTextColor(color);return t;
    }
    private LinearLayout column(){LinearLayout l=new LinearLayout(activity);l.setOrientation(1);return l;}
    private LinearLayout card(LinearLayout parent,String title){
        LinearLayout l=column();l.setPadding(dp(16),dp(16),dp(16),dp(16));
        GradientDrawable bg=new GradientDrawable();bg.setColor(0xff1b2520);bg.setCornerRadius(dp(14));bg.setStroke(dp(1),0x666d8051);l.setBackground(bg);
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(12);parent.addView(l,lp);
        TextView tag=text(title,11,ACCENT);tag.setTypeface(null,Typeface.BOLD);tag.setLetterSpacing(.08f);l.addView(tag);
        return l;
    }
    private void row(LinearLayout parent,String label,String value,Runnable action){
        LinearLayout row=new LinearLayout(activity);row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout words=column();TextView heading=text(label,11,MUTED);words.addView(heading);
        TextView description=text(value,13,TEXT);description.setTypeface(null,Typeface.BOLD);description.setPadding(0,dp(3),0,0);words.addView(description);
        row.addView(words,new LinearLayout.LayoutParams(0,-2,1));
        if(action!=null){Button b=new Button(activity);b.setText(tr("Abrir"));b.setAllCaps(false);b.setTextSize(11);b.setTextColor(ACCENT);b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xff303c33));b.setOnClickListener(v->action.run());
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(dp(76),dp(42));lp.leftMargin=dp(12);row.addView(b,lp);}
        LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,-2);lp.topMargin=dp(14);parent.addView(row,lp);
    }
    private Runnable url(String value){return ()->{try{activity.startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse(value)));}catch(ActivityNotFoundException e){Toast.makeText(activity,value,Toast.LENGTH_LONG).show();}};}
    private LinearLayout build(Runnable logs,Runnable export){
        LinearLayout root=column();
        TextView title=text("THE LAST OF US REMASTERED",22,TEXT);title.setTypeface(Typeface.create("sans-serif-condensed",Typeface.BOLD));root.addView(title);
        String version="0.1";try{version=activity.getPackageManager().getPackageInfo(activity.getPackageName(),0).versionName;}catch(Exception ignored){}
        TextView v=text(tr("Versão")+" "+version,13,ACCENT);v.setPadding(0,dp(6),0,dp(8));root.addView(v);
        root.addView(text("Port para Android com runtime Bionic.",13,MUTED));
        LinearLayout contact=card(root,"DESENVOLVIMENTO");
        row(contact,"Fork e modificações","erickmendes22 · Daedalus Emulator",null);
        row(contact,"YouTube","@daedalusemulator",url("https://www.youtube.com/@daedalusemulator"));
        LinearLayout projects=card(root,"PROJETOS");
        row(projects,"Projeto Android","erickmendes22/tlou-remastered-android",url("https://github.com/erickmendes22/tlou-remastered-android"));
        row(projects,"Projeto de referência para PC","ravencaio/tlou-remastered-pc",url("https://github.com/ravencaio/tlou-remastered-pc"));
        row(projects,"Bugs e sugestões","GitHub Issues",url("https://github.com/erickmendes22/tlou-remastered-android/issues"));
        LinearLayout credits=card(root,"CRÉDITOS");
        row(credits,"Tradução de CPU","FEX-Emu",url("https://github.com/FEX-Emu/FEX"));
        row(credits,"Drivers Vulkan","libadrenotools · Turnip / Mesa",url("https://github.com/bylaws/libadrenotools"));
        LinearLayout data=card(root,"REGISTROS");
        row(data,"Ver logs","TLOU Android",logs);
        if(export!=null)row(data,"Exportar logs ZIP","TLOU-Android-Logs.zip",export);
        return root;
    }
}
