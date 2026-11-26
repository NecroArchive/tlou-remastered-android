package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PointF;
import android.graphics.Typeface;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

/** Translucent PlayStation-style controls with a persistent, draggable layout. */
final class TouchControllerOverlay extends View {
    private static final String[] NAMES={
        "l2","l1","touchpad","options","r1","r2",
        "up","left","right","down","triangle","circle","cross","square",
        "leftstick","l3","r3","rightstick","shake"
    };
    private static final float[][] DEFAULT={
        {87,105},{213,105},{640,105},{773,105},{1067,105},{1193,105},
        {160,395},{90,465},{230,465},{160,535},
        {1120,405},{1190,475},{1120,545},{1050,475},
        {320,612},{420,614},{860,614},{960,612},{640,636}
    };
    // Mobile action geometry derived from the approved Uncharted layout.
    // Classic positions remain in their existing preference namespace.
    private static final float[][] ACTION_DEFAULT={
        {275,105},{115,105},{570,105},{710,105},{1005,105},{1160,105},
        {145,235},{85,295},{205,295},{145,355},
        {1120,280},{1190,435},{1050,565},{980,398},
        {145,570},{318,590},{730,590},{850,570},{640,655}
    };
    private final boolean actionStyle;
    private final PadBridge pad;
    private final SharedPreferences preferences;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Map<String,PointF> centers=new LinkedHashMap<>();
    private final SparseArray<String> controls=new SparseArray<>();
    private final SparseArray<PointF> positions=new SparseArray<>();
    private final Set<String> pressed=new LinkedHashSet<>();
    private boolean mouseDown,editing;
    private final boolean mouseEnabled;
    private int editPointer=-1;
    private String dragged;
    private Runnable onEditDone;

    TouchControllerOverlay(Context context,PadBridge pad,boolean mouseEnabled) {
        super(context); this.pad=pad; this.mouseEnabled=mouseEnabled;
        actionStyle=!"classic".equals(PortSettings.prefs(context).getString("control_style","action"));
        preferences=context.getSharedPreferences(actionStyle?"tlou_action_touch":"gow3",Context.MODE_PRIVATE);
        float[][] defaults=actionStyle?ACTION_DEFAULT:DEFAULT;
        for (int i=0;i<NAMES.length;i++) {
            String name=NAMES[i];
            centers.put(name,new PointF(preferences.getFloat("overlay_x_"+name,defaults[i][0]),
                                        preferences.getFloat("overlay_y_"+name,defaults[i][1])));
        }
        setContentDescription("On-screen PlayStation controller");
    }
    private PointF center(String name) { return centers.get(name); }
    // Scale the visuals and touch targets without changing saved button positions.
    private float controlSize() {
        int percent=PortSettings.prefs(getContext()).getInt("control_size",actionStyle?100:120);
        return Math.max(0.75f,Math.min(1.60f,percent/100f));
    }
    // The 1280x720 layout drawn at one scale in the centred 16:9 area, as the game (MainActivity):
    // round on any screen shape. Controls may be dragged into the bars around it.
    private float scale() { return Math.max(1e-3f,Math.min(getWidth()/1280f,getHeight()/720f)); }
    private float offsetX() { return (getWidth()-1280f*scale())/2f; }
    private float offsetY() { return (getHeight()-720f*scale())/2f; }
    private float horizontalScale() { return actionStyle?Math.max(1f,getWidth()/(1280f*scale())):1f; }
    private float displayX(float x) { return 640f+(x-640f)*horizontalScale(); }
    private float layoutX(MotionEvent e,int index) {
        return 640f+((e.getX(index)-offsetX())/scale()-640f)/horizontalScale();
    }
    private PointF visibleCenter(String name) {
        PointF p=center(name); return new PointF(displayX(p.x),p.y);
    }
    private float visibleDistance(float x,float y,PointF p) {
        return (float)Math.hypot((x-p.x)*horizontalScale(),y-p.y);
    }
    private float layoutY(MotionEvent e,int index) { return (e.getY(index)-offsetY())/scale(); }
    private static float distance(float x,float y,PointF p) { return (float)Math.hypot(x-p.x,y-p.y); }
    private static boolean stick(String name) { return name.equals("leftstick") || name.equals("rightstick"); }
    private static boolean rectangle(String name) {
        return name.equals("l1") || name.equals("l2") || name.equals("r1") ||
               name.equals("r2") || name.equals("touchpad") || name.equals("options") ||
               name.equals("shake");
    }
    private PointF dpadCenter() {
        PointF u=center("up"),l=center("left"),r=center("right"),d=center("down");
        return new PointF((u.x+l.x+r.x+d.x)/4f,(u.y+l.y+r.y+d.y)/4f);
    }
    private String hit(float x,float y) {
        PointF dpad=dpadCenter();
        float size=controlSize();
        if (!editing && visibleDistance(x,y,dpad)<112*size && distance(center("up").x,center("up").y,dpad)<115 &&
            distance(center("left").x,center("left").y,dpad)<115 &&
            distance(center("right").x,center("right").y,dpad)<115 &&
            distance(center("down").x,center("down").y,dpad)<115) return "dpad";
        String winner=null; float best=Float.MAX_VALUE;
        for (String name:NAMES) {
            PointF p=center(name);
            float dx=Math.abs(x-p.x)*horizontalScale(),dy=Math.abs(y-p.y);
            boolean inside=rectangle(name) ? dx<(name.equals("touchpad") ? 72 : 62)*size && dy<38*size
                    : visibleDistance(x,y,p)<(stick(name) ? (actionStyle?82:70) : name.equals("l3") || name.equals("r3") ? 39 : 48)*size;
            if (inside && visibleDistance(x,y,p)<best) { best=visibleDistance(x,y,p); winner=name; }
        }
        return winner;
    }
    private static int axis(float v) { return Math.max(0,Math.min(255,Math.round((v+1f)*127.5f))); }

    private void publish() {
        pressed.clear();
        float size=controlSize();
        int lx=128,ly=128,rx=128,ry=128,l2=0,r2=0;
        boolean left=false,right=false,mouse=false;
        for (int i=0;i<controls.size();i++) {
            String control=controls.valueAt(i);
            PointF p=positions.get(controls.keyAt(i));
            if (p==null) continue;
            if (control.equals("dpad")) {
                PointF c=dpadCenter();
                float dx=(p.x-c.x)*horizontalScale(),dy=p.y-c.y;
                if(actionStyle) {
                    // TLOU weapon/item selection is cardinal, not a diagonal combo.
                    if(Math.max(Math.abs(dx),Math.abs(dy))>12*size)
                        pressed.add(Math.abs(dx)>Math.abs(dy)?(dx<0?"left":"right"):(dy<0?"up":"down"));
                } else {
                    if(dx<-22*size)pressed.add("left");if(dx>22*size)pressed.add("right");
                    if(dy<-22*size)pressed.add("up");if(dy>22*size)pressed.add("down");
                }
            } else if (control.equals("leftstick")) {
                left=true; PointF c=center(control);
                float dx=(p.x-c.x)*horizontalScale(),dy=p.y-c.y;
                float range=Math.max(56f*size,(float)Math.hypot(dx,dy));
                if(Math.hypot(dx,dy)>4*size){lx=axis(dx/range);ly=axis(dy/range);}
            } else if (control.equals("rightstick")) {
                right=true; PointF c=center(control);
                float dx=(p.x-c.x)*horizontalScale(),dy=p.y-c.y;
                float range=Math.max(56f*size,(float)Math.hypot(dx,dy));
                if(Math.hypot(dx,dy)>4*size){rx=axis(dx/range);ry=axis(dy/range);}
            } else if (control.equals("l2")) { l2=255; pressed.add(control); }
            else if (control.equals("r2")) { r2=255; pressed.add(control); }
            else if (control.equals("touchpad")) pressed.add("touchpad_right"); /* 右侧点击:打开游戏暂停菜单,进而可进存档列表 */
            else if (control.equals("mouse")) {
                // Android Bionic has no X11/XInput mouse backend.
                mouse=true;
            } else pressed.add(control);
        }
        if (mouse!=mouseDown) { mouseDown=mouse; }
        pad.virtual(pressed,lx,ly,rx,ry,left,right,l2,r2);
        invalidate();
    }
    void release() { controls.clear(); positions.clear(); publish(); }
    void edit(Runnable done) { release(); editing=true; onEditDone=done; invalidate(); }
    void finishEdit() {
        editPointer=-1; dragged=null; editing=false; invalidate();
        if (onEditDone!=null) onEditDone.run();
        onEditDone=null;
    }
    void resetLayout() {
        SharedPreferences.Editor editor=preferences.edit();
        float[][] defaults=actionStyle?ACTION_DEFAULT:DEFAULT;
        for (int i=0;i<NAMES.length;i++) {
            String name=NAMES[i]; centers.get(name).set(defaults[i][0],defaults[i][1]);
            editor.remove("overlay_x_"+name).remove("overlay_y_"+name);
        }
        editor.apply(); release(); invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int action=e.getActionMasked(),at=e.getActionIndex(),id=e.getPointerId(at);
        if (editing) {
            if (action==MotionEvent.ACTION_CANCEL) { editPointer=-1; dragged=null; return true; }
            if (action==MotionEvent.ACTION_DOWN) {
                float x=layoutX(e,at),y=layoutY(e,at);
                if (displayX(x)>548 && displayX(x)<732 && y<72) { finishEdit(); return true; }
                dragged=hit(x,y); editPointer=dragged==null ? -1 : id;
            } else if (action==MotionEvent.ACTION_MOVE && editPointer>=0 && dragged!=null) {
                int index=e.findPointerIndex(editPointer);
                float barX=offsetX()/scale(),barY=offsetY()/scale();
                if (index>=0) center(dragged).set(
                    Math.max(actionStyle?80:36-barX,Math.min(actionStyle?1200:1244+barX,layoutX(e,index))),
                    Math.max(36-barY,Math.min(684+barY,layoutY(e,index))));
                invalidate();
            } else if ((action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP)
                       && id==editPointer && dragged!=null) {
                PointF p=center(dragged);
                preferences.edit().putFloat("overlay_x_"+dragged,p.x)
                    .putFloat("overlay_y_"+dragged,p.y).apply();
                editPointer=-1; dragged=null;
            }
            return true;
        }
        if (action==MotionEvent.ACTION_CANCEL) { release(); return true; }
        if (action==MotionEvent.ACTION_DOWN || action==MotionEvent.ACTION_POINTER_DOWN) {
            float x=layoutX(e,at),y=layoutY(e,at);
            String name=hit(x,y);
            if (name==null && !mouseEnabled) return true;
            controls.put(id,name==null ? "mouse" : name);
            positions.put(id,new PointF(x,y));
        } else if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) {
            controls.remove(id); positions.remove(id);
        }
        if (action==MotionEvent.ACTION_MOVE) {
            for (int i=0;i<e.getPointerCount();i++) {
                PointF p=positions.get(e.getPointerId(i));
                if (p!=null) p.set(layoutX(e,i),layoutY(e,i));
            }
        }
        publish(); return true;
    }

    private int themedColor=0xffb3c88f;
    private void refreshTheme() {
        String value=PortSettings.prefs(getContext()).getString("control_color","olive");
        switch(value) {
            case "amber": themedColor=0xffe0c783; break;
            case "white": themedColor=0xffe4e9dc; break;
            case "red": themedColor=0xffd28b7e; break;
            case "blue": themedColor=0xff92b5cf; break;
            default: themedColor=0xffb3c88f; // The Last of Us weathered olive
        }
    }
    private int themeColor(){return themedColor;}
    private int overlayFill(boolean selected,boolean down) {
        int rgb=themeColor()&0x00ffffff;
        return ((selected?0xdd:down?0x99:0x55)<<24)|rgb;
    }

    private void circle(Canvas c,String name,float r,String label,int accent) {
        PointF p=visibleCenter(name);
        float size=controlSize();
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(overlayFill(name.equals(dragged),pressed.contains(name)));
        c.drawCircle(p.x,p.y,r*size,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(themeColor());
        c.drawCircle(p.x,p.y,r*size,paint);
        paint.setStyle(Paint.Style.FILL); paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setColor(themeColor()); paint.setTextSize((label.length()>2 ? 19 : 33)*size); paint.setTextAlign(Paint.Align.CENTER);
        c.drawText(label,p.x,p.y-(paint.ascent()+paint.descent())/2,paint);
    }
    private void rectangle(Canvas c,String name,float width,String label) {
        PointF p=visibleCenter(name);
        float size=controlSize();
        float halfWidth=width*size/2f,halfHeight=25f*size,corner=14f*size;
        // The touchpad button presses "touchpad_right" (right-side click, opens the pause menu),
        // not "touchpad", so highlight it from that signal too.
        boolean lit=pressed.contains(name) ||
            (name.equals("touchpad") && pressed.contains("touchpad_right"));
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(overlayFill(name.equals(dragged),lit));
        c.drawRoundRect(p.x-halfWidth,p.y-halfHeight,p.x+halfWidth,p.y+halfHeight,corner,corner,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setColor(themeColor()); paint.setStrokeWidth(2);
        c.drawRoundRect(p.x-halfWidth,p.y-halfHeight,p.x+halfWidth,p.y+halfHeight,corner,corner,paint);
        paint.setStyle(Paint.Style.FILL); paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setColor(themeColor()); paint.setTextSize(19*size); paint.setTextAlign(Paint.Align.CENTER);
        c.drawText(label,p.x,p.y-(paint.ascent()+paint.descent())/2,paint);
    }
    private void drawStick(Canvas c,String name) {
        PointF center=visibleCenter(name);
        float size=controlSize();
        paint.setStyle(Paint.Style.FILL); paint.setColor(overlayFill(name.equals(dragged),false));
        c.drawCircle(center.x,center.y,57*size,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setColor(themeColor()); paint.setStrokeWidth(2);
        c.drawCircle(center.x,center.y,57*size,paint);
        float knobX=center.x,knobY=center.y;
        for (int i=0;i<controls.size();i++) {
            if (name.equals(controls.valueAt(i))) {
                PointF p=positions.get(controls.keyAt(i));
                if (p!=null) {
                    knobX=center.x+Math.max(-27*size,Math.min(27*size,displayX(p.x)-center.x));
                    knobY=center.y+Math.max(-27*size,Math.min(27*size,p.y-center.y));
                }
                break;
            }
        }
        paint.setStyle(Paint.Style.FILL); paint.setColor(overlayFill(false,true));
        c.drawCircle(knobX,knobY,28*size,paint);
        paint.setColor(themeColor()); paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setTextAlign(Paint.Align.CENTER); paint.setTextSize(22*size);
        c.drawText(name.equals("leftstick") ? "L" : "R",knobX,
                   knobY-(paint.ascent()+paint.descent())/2,paint);
    }
    private void caption(Canvas c,String name,String pt,String en) {
        PointF p=visibleCenter(name);
        paint.setStyle(Paint.Style.FILL);paint.setColor(themeColor());
        paint.setTypeface(Typeface.DEFAULT_BOLD);paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(13*controlSize());
        String translated=TlouLanguage.t(getContext(),pt);
        if(TlouLanguage.index(getContext())!=0 && translated.equals(pt))translated=en;
        c.drawText(translated,p.x,p.y+(rectangle(name)?39:54)*controlSize(),paint);
    }
    @Override protected void onDraw(Canvas c) {
        super.onDraw(c); refreshTheme(); c.save(); c.translate(offsetX(),offsetY()); c.scale(scale(),scale());
        rectangle(c,"l2",108,"L2"); rectangle(c,"l1",108,"L1");
        rectangle(c,"r1",108,"R1"); rectangle(c,"r2",108,"R2");
        rectangle(c,"touchpad",126,"TOUCH"); rectangle(c,"options",108,"OPTIONS");
        circle(c,"up",39,"▲",0xffc6ad76); circle(c,"left",39,"◀",0xffc6ad76);
        circle(c,"right",39,"▶",0xffc6ad76); circle(c,"down",39,"▼",0xffc6ad76);
        circle(c,"triangle",39,"△",0xff83cdaa); circle(c,"circle",39,"○",0xffe38288);
        circle(c,"cross",39,"×",0xff88aaff); circle(c,"square",39,"□",0xffd6a0ce);
        drawStick(c,"leftstick"); drawStick(c,"rightstick");
        circle(c,"l3",29,"L3",0xffa0aec0); circle(c,"r3",29,"R3",0xffa0aec0);
        if (actionStyle) rectangle(c,"shake",118,"↻");
        else rectangle(c,"shake",118,"AGITAR");
        if(actionStyle) {
            // Native Canvas symbols and game-specific action captions. The
            // underlying PlayStation buttons are unchanged, including alternate
            // shoulder mappings selected in the original game.
            caption(c,"l1","CORRER","SPRINT"); caption(c,"l2","MIRAR","AIM");
            caption(c,"r1","ESCUTA","LISTEN"); caption(c,"r2","ATIRAR","FIRE");
            caption(c,"triangle","INTERAGIR","INTERACT"); caption(c,"square","CORPO A CORPO","MELEE");
            caption(c,"circle","AGACHAR","CROUCH"); caption(c,"cross","SUBIR","CLIMB");
            caption(c,"r3","LANTERNA","FLASHLIGHT");
            caption(c,"shake","AGITAR LANTERNA","SHAKE LIGHT");
        }
        if (editing) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(0xcc15171d);
            c.drawRoundRect(548,9,732,62,13,13,paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(themeColor());
            c.drawRoundRect(548,9,732,62,13,13,paint);
            paint.setStyle(Paint.Style.FILL); paint.setTextSize(20);
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setColor(themeColor());
            c.drawText("CONCLUIR",640,36-(paint.ascent()+paint.descent())/2,paint);
        }
        c.restore();
    }
}
