package ro.cobrabm.fexdroid;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Bitmap;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Window;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.text.DateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.IntConsumer;

/** TLOU save cards: actual chapter, description, slot, date and game-provided thumbnail. */
final class SaveChoicePanel extends Dialog {
    private static final int BG=0xff101613,CARD=0xff1b2520,TEXT=0xffe7e9df,MUTED=0xffa6b0a4,OLIVE=0xffc2cf96;
    private final Activity activity;
    private final GameChoiceProtocol.Request request;
    private final IntConsumer selection;
    private final AtomicBoolean answered=new AtomicBoolean();
    private final LinearLayout[] cards;
    private final ScrollView scroll;
    private int selected;
    private final java.util.function.Function<KeyEvent,String> buttonName;
    SaveChoicePanel(Activity activity,GameChoiceProtocol.Request request,Bitmap[] thumbnails,IntConsumer selection){
        this(activity,request,thumbnails,selection,null);
    }
    SaveChoicePanel(Activity activity,GameChoiceProtocol.Request request,Bitmap[] thumbnails,IntConsumer selection,java.util.function.Function<KeyEvent,String> buttonName){
        super(activity);this.buttonName=buttonName;this.activity=activity;this.request=request;this.selection=selection;selected=request.focus;
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout root=column();root.setPadding(dp(12),dp(8),dp(12),dp(8));root.setBackgroundColor(BG);
        root.addView(text("THE LAST OF US REMASTERED",11,OLIVE));
        LinearLayout header=new LinearLayout(activity);header.setGravity(Gravity.CENTER_VERTICAL);
        TextView heading=text(title(),20,TEXT);heading.setTypeface(Typeface.create("sans-serif-condensed",Typeface.BOLD));
        header.addView(heading,new LinearLayout.LayoutParams(0,-2,1));
        Button close=button("×",false,()->answer(-2));header.addView(close,new LinearLayout.LayoutParams(dp(38),dp(38)));root.addView(header);
        int existing=0;for(var entry:request.entries)if(!entry.isNew)existing++;
        TextView count=text(tr("Salvamentos")+" · "+existing,12,MUTED);count.setPadding(0,dp(2),0,dp(4));root.addView(count);
        scroll=new ScrollView(activity);LinearLayout list=column();scroll.addView(list);
        cards=new LinearLayout[request.entries.length];
        for(int i=0;i<cards.length;i++){
            final int index=i;GameChoiceProtocol.Entry entry=request.entries[i];
            LinearLayout card=new LinearLayout(activity);card.setGravity(Gravity.CENTER_VERTICAL);card.setPadding(dp(8),dp(6),dp(8),dp(6));cards[i]=card;
            if(thumbnails!=null && i<thumbnails.length && thumbnails[i]!=null){
                ImageView image=new ImageView(activity);image.setScaleType(ImageView.ScaleType.CENTER_CROP);image.setImageBitmap(thumbnails[i]);
                card.addView(image,new LinearLayout.LayoutParams(dp(112),dp(62)));
            }else{
                TextView missing=text(entry.isNew?tr("Novo save"):tr("Sem miniatura"),11,MUTED);missing.setGravity(Gravity.CENTER);missing.setBackgroundColor(0xff101813);
                card.addView(missing,new LinearLayout.LayoutParams(dp(112),dp(62)));
            }
            LinearLayout words=column();LinearLayout.LayoutParams wordsLp=new LinearLayout.LayoutParams(0,-1,1);wordsLp.leftMargin=dp(10);card.addView(words,wordsLp);
            TextView name=text(entry.isNew?tr("Novo save"):entry.heading(),14,TEXT);
            name.setTypeface(null,Typeface.BOLD);name.setSingleLine(true);
            name.setEllipsize(android.text.TextUtils.TruncateAt.END);words.addView(name);
            String summary=!entry.detail.isEmpty()?entry.detail:
                    !entry.title.isEmpty() && !entry.title.equals(entry.heading())?entry.title:entry.label;
            if(!entry.isNew && !summary.isEmpty()){
                TextView detail=text(summary,11,MUTED);detail.setMaxLines(2);
                detail.setEllipsize(android.text.TextUtils.TruncateAt.END);words.addView(detail);
            }
            String metadata=(entry.directory.isEmpty()?"":tr("Slot")+" · "+entry.directory);
            if(!entry.isNew && entry.timestamp>0)metadata+=(metadata.isEmpty()?"":"  •  ")+date(entry.timestamp);
            if(!metadata.isEmpty()){
                TextView info=text(metadata,10,OLIVE);info.setSingleLine(true);
                info.setEllipsize(android.text.TextUtils.TruncateAt.END);words.addView(info);
            }
            card.setOnClickListener(v->select(index));card.setContentDescription(entry.heading());
            card.setOnLongClickListener(v->{showSaveDetails(entry);return true;});
            LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(-1,dp(84));lp.bottomMargin=dp(5);list.addView(card,lp);
        }
        root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout footer=new LinearLayout(activity);footer.setGravity(Gravity.RIGHT);footer.setPadding(0,dp(5),0,0);
        footer.addView(button(tr("Cancelar"),false,()->answer(-2)),new LinearLayout.LayoutParams(dp(120),dp(42)));
        LinearLayout.LayoutParams actionLp=new LinearLayout.LayoutParams(dp(140),dp(42));actionLp.leftMargin=dp(10);
        footer.addView(button(action(),true,()->answer(selected)),actionLp);root.addView(footer);
        root.setFocusableInTouchMode(true);root.requestFocus();
        setContentView(root);setOnCancelListener(d->answer(-2));select(selected);
        setOnKeyListener((d,key,event)->{
            if(event.getAction()!=KeyEvent.ACTION_DOWN)return false;
            String mapped=buttonName==null?null:buttonName.apply(event);
            if(key==KeyEvent.KEYCODE_DPAD_UP || key==KeyEvent.KEYCODE_DPAD_DOWN){select((selected+(key==KeyEvent.KEYCODE_DPAD_UP?-1:1)+cards.length)%cards.length);return true;}
            if("cross".equals(mapped) || (buttonName==null && key==KeyEvent.KEYCODE_BUTTON_A) || key==KeyEvent.KEYCODE_DPAD_CENTER || key==KeyEvent.KEYCODE_ENTER){answer(selected);return true;}
            if("circle".equals(mapped) || (buttonName==null && key==KeyEvent.KEYCODE_BUTTON_B) || key==KeyEvent.KEYCODE_ESCAPE){answer(-2);return true;}return false;
        });
        final int[] hat={0};root.setOnGenericMotionListener((v,event)->{
            int y=Math.round(event.getAxisValue(MotionEvent.AXIS_HAT_Y));if(y!=0 && y!=hat[0])select((selected+y+cards.length)%cards.length);hat[0]=y;return y!=0;
        });
    }
    @Override public void show(){super.show();Window w=getWindow();if(w!=null){w.setBackgroundDrawableResource(android.R.color.transparent);w.setGravity(Gravity.CENTER);w.setLayout(Math.min(dp(800),(int)(activity.getResources().getDisplayMetrics().widthPixels*.94)),(int)(activity.getResources().getDisplayMetrics().heightPixels*.90));}}
    void updateThumbnails(Bitmap[] pictures){
        for(int i=0;i<cards.length && i<pictures.length;i++)if(pictures[i]!=null){
            ImageView image=new ImageView(activity);image.setScaleType(ImageView.ScaleType.CENTER_CROP);image.setImageBitmap(pictures[i]);
            cards[i].removeViewAt(0);cards[i].addView(image,0,new LinearLayout.LayoutParams(dp(112),dp(62)));
        }
    }
    private void showSaveDetails(GameChoiceProtocol.Entry entry){
        StringBuilder details=new StringBuilder();
        if(!entry.title.isEmpty())details.append(entry.title).append('\n');
        if(!entry.subtitle.isEmpty() && !entry.subtitle.equals(entry.title))details.append(entry.subtitle).append('\n');
        if(!entry.detail.isEmpty())details.append(entry.detail).append('\n');
        if(!entry.directory.isEmpty())details.append(tr("Slot")).append(": ").append(entry.directory).append('\n');
        if(!entry.isNew && entry.timestamp>0)details.append(date(entry.timestamp));
        new android.app.AlertDialog.Builder(activity)
                .setTitle(entry.heading()).setMessage(details.toString().trim())
                .setPositiveButton(android.R.string.ok,null).show();
    }
    private int dp(int n){return Math.round(n*activity.getResources().getDisplayMetrics().density);}
    private String tr(String value){return TlouLanguage.t(activity,value);}
    private LinearLayout column(){LinearLayout l=new LinearLayout(activity);l.setOrientation(1);return l;}
    private TextView text(String value,int size,int color){TextView t=new TextView(activity);t.setText(value);t.setTextSize(size);t.setTextColor(color);return t;}
    private Button button(String value,boolean primary,Runnable action){Button b=new Button(activity);b.setFocusable(false);b.setAllCaps(false);b.setText(value);b.setTextColor(primary?BG:TEXT);b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(primary?OLIVE:0xff303c33));b.setOnClickListener(v->action.run());return b;}
    private String title(){switch(request.title){case "Load game":return tr("Carregar jogo");case "Save game":return tr("Salvar jogo");case "Delete save":return tr("Excluir save");default:return request.title;}}
    private String action(){switch(request.title){case "Load game":return tr("Carregar");case "Save game":return tr("Salvar");case "Delete save":return tr("Excluir");default:return tr("Selecionar");}}
    private String date(long seconds){
        if(seconds<=0 || seconds>Long.MAX_VALUE/1000)return tr("Data indisponível");
        Locale locale=Locale.forLanguageTag(TlouLanguage.CODES[TlouLanguage.index(activity)]);
        return DateFormat.getDateTimeInstance(DateFormat.MEDIUM,DateFormat.SHORT,locale).format(new Date(seconds*1000));
    }
    private void select(int index){
        selected=index;
        for(int i=0;i<cards.length;i++){GradientDrawable bg=new GradientDrawable();bg.setCornerRadius(dp(10));bg.setColor(i==selected?0xff2b3926:CARD);bg.setStroke(dp(i==selected?2:1),i==selected?OLIVE:0xff354331);cards[i].setBackground(bg);}
        cards[index].post(()->cards[index].requestRectangleOnScreen(new android.graphics.Rect(0,0,cards[index].getWidth(),cards[index].getHeight()),false));
    }
    private void answer(int index){if(answered.compareAndSet(false,true)){selection.accept(index);dismiss();}}
}
