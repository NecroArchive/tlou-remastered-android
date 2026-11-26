package ro.cobrabm.fexdroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Bitmap;
import android.graphics.RectF;
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
        {247,111},{101,111},{592,105},{701,105},{1035,111},{1188,111},
        {175,414},{175,414},{175,414},{175,414},
        {1166,273},{1192,429},{1080,530},{1019,384},
        {146,532},{284,551},{960,551},{830,532},{640,628}
    };
    private final boolean actionStyle;
    private final PadBridge pad;
    private final SharedPreferences preferences;
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint glyphPaint=new Paint(Paint.ANTI_ALIAS_FLAG|Paint.FILTER_BITMAP_FLAG);
    private final UnchartedActionGlyphs glyphs=new UnchartedActionGlyphs();
    private final SparseArray<PointF> touchDownPositions=new SparseArray<>();
    private final SparseArray<Long> touchDownTimes=new SparseArray<>();
    private long lastLeftTapMs,lastRightTapMs;
    private boolean leftStickClicked,rightStickClicked;
    private final Runnable clearLeftClick=()->{leftStickClicked=false;publish();};
    private final Runnable clearRightClick=()->{rightStickClicked=false;publish();};
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
        preferences=context.getSharedPreferences(actionStyle?"tlou_action_touch_v3":"gow3",Context.MODE_PRIVATE);
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
        return Math.max(0.75f,Math.min(actionStyle?1.30f:1.60f,percent/100f));
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
    private PointF visibleDpadCenter() {
        PointF p=dpadCenter();return new PointF(displayX(p.x),p.y);
    }
    private String hit(float x,float y) {
        PointF dpad=dpadCenter();
        float size=controlSize();
        if(actionStyle) {
            PointF p=selectorCenter();
            float dx=(displayX(x)-p.x)/selectorSize(),dy=(y-p.y)/selectorSize();
            if(Math.abs(dx)<=150 && Math.abs(dy)<=27) {
                if(editing)return "dpad";
                if(dx< -118)return "selector_left";
                if(dx>118)return "selector_right";
                return "selector_"+Math.min(3,Math.max(0,(int)((dx+118)/59)));
            }
        }
        if (!actionStyle && !editing && visibleDistance(x,y,dpad)<112*size && distance(center("up").x,center("up").y,dpad)<115 &&
            distance(center("left").x,center("left").y,dpad)<115 &&
            distance(center("right").x,center("right").y,dpad)<115 &&
            distance(center("down").x,center("down").y,dpad)<115) return "dpad";
        String winner=null; float best=Float.MAX_VALUE;
        for (String name:NAMES) {
            if(actionStyle && (name.equals("up")||name.equals("down")||name.equals("left")||
                name.equals("right")))continue;
            PointF p=center(name);
            float dx=Math.abs(x-p.x)*horizontalScale(),dy=Math.abs(y-p.y);
            boolean inside=actionStyle ? visibleDistance(x,y,p)<(stick(name)?82:
                    name.equals("touchpad")||name.equals("options")||name.equals("shake")||name.equals("l3")||name.equals("r3")?40:
                    name.equals("l1")||name.equals("l2")||name.equals("r1")||name.equals("r2")?54:46)*size : rectangle(name) ? dx<(name.equals("touchpad") ? 72 : 62)*size && dy<38*size
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
            if(control.startsWith("selector_")) {
                String dir=selectorDirection(control);
                if(dir!=null)pressed.add(dir);
            } else if (control.equals("dpad")) {
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
    void release() {
        controls.clear();positions.clear();touchDownPositions.clear();touchDownTimes.clear();
        lastLeftTapMs=lastRightTapMs=0;leftStickClicked=rightStickClicked=false;
        removeCallbacks(clearLeftClick);removeCallbacks(clearRightClick);publish();
    }
    private void clickStickIfDoubleTapped(int id,long when) {
        if(!actionStyle)return;
        String target=controls.get(id);
        if(!"leftstick".equals(target)&&!"rightstick".equals(target))return;
        PointF start=touchDownPositions.get(id),last=positions.get(id);
        Long begin=touchDownTimes.get(id);
        if(start==null||last==null||begin==null||when-begin>230 ||
           visibleDistance(last.x,last.y,start)>18*controlSize())return;
        if("leftstick".equals(target)){
            if(lastLeftTapMs>0&&when-lastLeftTapMs<=365){
                leftStickClicked=true;lastLeftTapMs=0;
                removeCallbacks(clearLeftClick);postDelayed(clearLeftClick,135);
            }else lastLeftTapMs=when;
        }else{
            if(lastRightTapMs>0&&when-lastRightTapMs<=365){
                rightStickClicked=true;lastRightTapMs=0;
                removeCallbacks(clearRightClick);postDelayed(clearRightClick,135);
            }else lastRightTapMs=when;
        }
    }

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
                if (index>=0) {
                    float tx=Math.max(actionStyle?80:36-barX,Math.min(actionStyle?1200:1244+barX,layoutX(e,index)));
                    float ty=Math.max(36-barY,Math.min(684+barY,layoutY(e,index)));
                    if(actionStyle && "dpad".equals(dragged)) {
                        PointF p=dpadCenter();float dx=tx-p.x,dy=ty-p.y;
                        for(String dir:DPAD_DIRECTIONS)center(dir).offset(dx,dy);
                    } else center(dragged).set(tx,ty);
                }
                invalidate();
            } else if ((action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP)
                       && id==editPointer && dragged!=null) {
                SharedPreferences.Editor save=preferences.edit();
                for(String name:("dpad".equals(dragged)?DPAD_DIRECTIONS:new String[]{dragged})) {
                    PointF p=center(name);save.putFloat("overlay_x_"+name,p.x).putFloat("overlay_y_"+name,p.y);
                }
                save.apply();
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
            touchDownPositions.put(id,new PointF(x,y));touchDownTimes.put(id,e.getEventTime());
        } else if (action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) {
            // Explicit L3/R3 targets; analog taps do not synthesize extra clicks.
            controls.remove(id); positions.remove(id);touchDownPositions.remove(id);touchDownTimes.remove(id);
        }
        if (action==MotionEvent.ACTION_MOVE) {
            for (int i=0;i<e.getPointerCount();i++) {
                PointF p=positions.get(e.getPointerId(i));
                if (p!=null) p.set(layoutX(e,i),layoutY(e,i));
            }
        }
        publish(); return true;
    }

    private int themedColor=0xffd6e89f;
    private void refreshTheme() {
        String value=PortSettings.prefs(getContext()).getString("control_color","olive");
        switch(value) {
            case "amber": themedColor=0xffe0c783; break;
            case "white": themedColor=0xffe4e9dc; break;
            case "red": themedColor=0xffd28b7e; break;
            case "blue": themedColor=0xff92b5cf; break;
            default: themedColor=0xffd6e89f; // The Last of Us weathered olive
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
    private void drawActionGlyph(Canvas c,String name,float x,float y,float size) {
        c.save(); c.translate(x,y); c.scale(size,size);
        paint.setColor(ACTION_GLYPH);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(.13f);
        paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);
        android.graphics.Path path=new android.graphics.Path();
        switch(name) {
            case "l1": // sprinting figure
                paint.setStyle(Paint.Style.FILL);c.drawCircle(.22f,-.64f,.17f,paint);
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(.20f);
                c.drawLine(.12f,-.36f,-.18f,.12f,paint);
                c.drawLine(.06f,-.22f,.58f,-.09f,paint);c.drawLine(.58f,-.09f,.66f,-.42f,paint);
                c.drawLine(-.05f,-.18f,-.50f,-.39f,paint);
                c.drawLine(-.18f,.12f,.25f,.39f,paint);c.drawLine(.25f,.39f,.58f,.71f,paint);
                c.drawLine(-.18f,.12f,-.50f,.65f,paint);
                break;
            case "l2": // aim sight
                c.drawCircle(0,0,.36f,paint);
                c.drawLine(-.75f,0,-.41f,0,paint);c.drawLine(.41f,0,.75f,0,paint);
                c.drawLine(0,-.75f,0,-.41f,paint);c.drawLine(0,.41f,0,.75f,paint);
                c.drawCircle(0,0,.055f,paint);
                break;
            case "r1": // listening mode: ear and sound waves
                c.drawArc(-.51f,-.68f,.33f,.48f,150f,290f,false,paint);
                c.drawArc(-.30f,-.38f,.12f,.13f,165f,265f,false,paint);
                c.drawLine(-.16f,.47f,-.09f,.70f,paint);
                c.drawArc(.08f,-.51f,.70f,.53f,-65f,130f,false,paint);
                break;
            case "shake": // flashlight with motion marks
                paint.setStyle(Paint.Style.FILL);c.drawRoundRect(-.21f,-.08f,.21f,.67f,.07f,.07f,paint);
                c.drawRoundRect(-.39f,-.46f,.39f,-.03f,.06f,.06f,paint);
                paint.setStyle(Paint.Style.STROKE);c.drawLine(0,-.63f,0,-.88f,paint);
                c.drawLine(-.46f,-.60f,-.65f,-.78f,paint);c.drawLine(.46f,-.60f,.65f,-.78f,paint);
                break;
            case "r2": // pistol silhouette, rather than an ambiguous sunburst
                paint.setStyle(Paint.Style.FILL);
                path.moveTo(-.74f,-.23f);path.lineTo(.63f,-.23f);
                path.lineTo(.70f,.05f);path.lineTo(.12f,.05f);
                path.lineTo(-.10f,.65f);path.lineTo(-.49f,.65f);
                path.lineTo(-.36f,.06f);path.lineTo(-.74f,.06f);
                path.close();c.drawPath(path,paint);
                paint.setColor(0xff304650);paint.setStyle(Paint.Style.STROKE);
                c.drawLine(-.41f,-.08f,.33f,-.08f,paint);
                break;
            case "triangle": // open palm reaching for an object
                paint.setStyle(Paint.Style.FILL);
                path.moveTo(-.48f,-.16f);path.lineTo(-.47f,-.57f);
                path.quadTo(-.42f,-.75f,-.28f,-.62f);
                path.lineTo(-.19f,-.32f);path.lineTo(-.17f,-.75f);
                path.quadTo(-.13f,-.89f,.02f,-.80f);
                path.lineTo(.08f,-.38f);
                path.quadTo(.38f,-.70f,.49f,-.51f);
                path.quadTo(.57f,-.39f,.45f,-.20f);
                path.lineTo(.20f,.15f);path.lineTo(.20f,.70f);
                path.lineTo(-.31f,.70f);path.close();
                c.drawPath(path,paint);
                break;
            case "square": // solid clenched fist with visible knuckles
                paint.setStyle(Paint.Style.FILL);
                c.drawRoundRect(-.66f,-.32f,.61f,.31f,.16f,.16f,paint);
                c.drawRoundRect(-.41f,.13f,.40f,.68f,.13f,.13f,paint);
                paint.setStyle(Paint.Style.STROKE);paint.setColor(0xff304650);
                for(int i=-1;i<=1;i++)c.drawLine(i*.29f,-.24f,i*.29f,.10f,paint);
                break;
            case "circle": // crouching figure behind cover
                paint.setStyle(Paint.Style.FILL);
                c.drawCircle(-.33f,-.39f,.18f,paint);
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(.21f);
                c.drawLine(-.24f,-.15f,-.10f,.20f,paint);
                c.drawLine(-.11f,.20f,-.51f,.50f,paint);
                c.drawLine(-.07f,.21f,.30f,.55f,paint);
                c.drawLine(-.23f,-.07f,.19f,.04f,paint);
                paint.setStrokeWidth(.12f);
                c.drawLine(.40f,-.66f,.40f,.71f,paint);
                c.drawLine(.40f,.12f,.78f,.12f,paint);
                break;
            case "cross": // jumping/climbing figure
                paint.setStyle(Paint.Style.FILL);
                c.drawCircle(-.07f,-.56f,.16f,paint);
                paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(.21f);
                c.drawLine(-.04f,-.28f,.14f,.13f,paint);
                c.drawLine(.02f,-.16f,-.47f,-.27f,paint);
                c.drawLine(.02f,-.16f,.48f,-.54f,paint);
                c.drawLine(.14f,.13f,-.42f,.60f,paint);
                c.drawLine(.14f,.13f,.60f,.47f,paint);
                break;
            default: break;
        }
        c.restore();paint.setStyle(Paint.Style.FILL);
        paint.setStrokeCap(Paint.Cap.BUTT);paint.setStrokeJoin(Paint.Join.MITER);
    }

    // Uncharted: desaturated slate surfaces and weathered metallic edges.
    // Neutral steel/sandstone surfaces; pressed controls have a subtle warm highlight.
    private static final int ACTION_EDGE=0xffd6e89f;
    private static final int ACTION_EDGE_ACTIVE=0xfff2ffc1;
    private static final int ACTION_GLYPH=0xffdce8ad;
    private static final int ACTION_GLYPH_ACTIVE=0xfff2ffc1;
    private int actionFill(boolean down) {
        float a=Math.min(.95f,.50f+(down?.19f:0f));
        return (Math.round(a*255f)<<24)|(down?0x004d6021:0x00142108);
    }
    private int classicFill(boolean down) {
        float a=Math.min(.95f,.50f+(down?.25f:0f));
        return (Math.round(a*255f)<<24)|(down?0x00e8dfc9:0x0030394c);
    }
    // Large, immediately recognisable pictograms for the two controls that
    // were unreadable in the real RedMagic screenshot. Both use the same gold
    // palette as GoW III, and are actually rendered in Android Canvas.
    private void drawFirePistol(Canvas c,float x,float y,float size) {
        c.save();c.translate(x,y);c.scale(size,size);
        android.graphics.Path p=new android.graphics.Path();
        paint.setStyle(Paint.Style.FILL);paint.setColor(ACTION_GLYPH);
        // A horizontal pistol slide/barrel pointed right, with a proper grip.
        p.moveTo(-.78f,-.28f);p.lineTo(.53f,-.28f);
        p.lineTo(.68f,-.14f);p.lineTo(.66f,.04f);
        p.lineTo(.15f,.09f);p.lineTo(-.06f,.68f);
        p.lineTo(-.46f,.68f);p.lineTo(-.41f,.07f);
        p.lineTo(-.78f,.05f);p.close();c.drawPath(p,paint);
        // Slide and trigger cutout give the small silhouette its gun profile.
        paint.setColor(0xff513820);paint.setStrokeWidth(.07f);
        c.drawRect(-.64f,-.13f,.30f,-.05f,paint);
        c.drawOval(-.30f,.02f,.03f,.27f,paint);
        paint.setStyle(Paint.Style.STROKE);paint.setColor(ACTION_GLYPH);
        paint.setStrokeWidth(.09f);
        c.drawLine(-.43f,.17f,-.37f,.52f,paint);
        // Small muzzle flash is explicitly attached to the barrel's tip.
        paint.setStyle(Paint.Style.FILL);paint.setColor(ACTION_GLYPH_ACTIVE);
        p.reset();p.moveTo(.71f,-.26f);
        p.lineTo(1.07f,-.37f);p.lineTo(.92f,-.07f);
        p.lineTo(1.19f,.04f);p.lineTo(.88f,.12f);
        p.lineTo(.94f,.38f);p.lineTo(.68f,.14f);p.close();
        c.drawPath(p,paint);
        c.restore();paint.setStyle(Paint.Style.FILL);
    }
    private void drawInteractHand(Canvas c,float x,float y,float size) {
        c.save();c.translate(x,y);c.scale(size,size);
        paint.setStyle(Paint.Style.FILL);paint.setColor(ACTION_GLYPH);
        // Four separated fingers, a thumb, and a broad open palm; no thin
        // line-art that disappears at a 46-unit button radius.
        c.drawRoundRect(-.41f,-.63f,-.19f,.13f,.10f,.10f,paint);
        c.drawRoundRect(-.15f,-.86f,.08f,.13f,.11f,.11f,paint);
        c.drawRoundRect(.11f,-.73f,.34f,.15f,.10f,.10f,paint);
        c.drawRoundRect(.37f,-.52f,.59f,.28f,.10f,.10f,paint);
        android.graphics.Path palm=new android.graphics.Path();
        palm.moveTo(-.47f,-.04f);
        palm.cubicTo(-.56f,-.38f,-.80f,-.31f,-.77f,-.08f);
        palm.lineTo(-.48f,.43f);
        palm.cubicTo(-.37f,.70f,-.18f,.80f,.13f,.78f);
        palm.cubicTo(.48f,.76f,.53f,.45f,.49f,.04f);
        palm.lineTo(-.47f,-.04f);palm.close();
        c.drawPath(palm,paint);
        // Fingers remain individually readable against the gold silhouette.
        paint.setColor(0xff344955);paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(.045f);
        c.drawLine(-.18f,-.35f,-.18f,.04f,paint);
        c.drawLine(.09f,-.42f,.09f,.04f,paint);
        c.drawLine(.36f,-.30f,.36f,.11f,paint);
        c.restore();paint.setStyle(Paint.Style.FILL);
    }
    private void actionSurface(Canvas c,float x,float y,float r,boolean down) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(actionFill(down));c.drawCircle(x,y,r,paint);
        // Edge lighting is confined to the circular button, not the gameplay.
        paint.setShader(new android.graphics.RadialGradient(x,y,r,
            new int[]{0x00000000,0x162a3c0b,0x887b983b},new float[]{0f,.70f,1f},
            android.graphics.Shader.TileMode.CLAMP));
        c.drawCircle(x,y,r,paint);paint.setShader(null);
        paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(1.8f*controlSize());
        paint.setColor(down?ACTION_EDGE_ACTIVE:themeColor());c.drawCircle(x,y,r,paint);
    }
    private void drawMask(Canvas c,String name,float x,float y,float r) {
        Bitmap icon=glyphs.get(name);
        if(icon==null){drawActionGlyph(c,name,x,y,r);return;}
        glyphPaint.setColorFilter(new android.graphics.PorterDuffColorFilter(themeColor(),android.graphics.PorterDuff.Mode.SRC_IN));
        c.drawBitmap(icon,null,new RectF(x-r,y-r,x+r,y+r),glyphPaint);
    }
    private void actionButton(Canvas c,String name,float radius,String label) {
        PointF p=visibleCenter(name);float s=controlSize(),r=radius*s;
        actionSurface(c,p.x,p.y,r,pressed.contains(name)||("touchpad".equals(name)&&pressed.contains("touchpad_right")));
        paint.setColor(themeColor());paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(3*s);
        if("touchpad".equals(name)) {
            c.drawRoundRect(p.x-r*.49f,p.y-r*.22f,p.x+r*.26f,p.y+r*.36f,5,5,paint);
            c.drawRoundRect(p.x-r*.26f,p.y-r*.40f,p.x+r*.50f,p.y+r*.18f,5,5,paint);
        }else if("options".equals(name)){
            paint.setStrokeWidth(5*s);c.drawLine(p.x-r*.16f,p.y-r*.40f,p.x-r*.16f,p.y+r*.40f,paint);
            c.drawLine(p.x+r*.16f,p.y-r*.40f,p.x+r*.16f,p.y+r*.40f,paint);
        }else drawMask(c,name,p.x,p.y,r*.80f);
        if(!label.isEmpty()){
            paint.setStyle(Paint.Style.FILL);paint.setColor(themeColor());paint.setTextAlign(Paint.Align.CENTER);
            paint.setTypeface(Typeface.DEFAULT_BOLD);paint.setTextSize(23*s);
            c.drawText(label,p.x,p.y+r+20*s,paint);
        }
    }
    // Selector is a visual replacement for the four native directional keys.
    // Left/right arrows preserve menu navigation and long-gun/handgun access.
    private static final String[] DPAD_DIRECTIONS={"up","right","down","left"};
    private static final String[] SELECTOR_ICONS={"pistol","brick","bottle","medkit"};
    static String selectorDirection(String key) {
        switch(key) {
            case "selector_left":return "left";
            case "selector_right":case "selector_0":return "right";
            case "selector_1":case "selector_2":return "down";
            case "selector_3":return "up";
            default:return null;
        }
    }
    private float selectorSize(){return Math.min(controlSize(),1.15f);}
    private PointF selectorCenter(){
        PointF p=visibleDpadCenter();float half=150*selectorSize()+5;
        float edge=(getWidth()/Math.max(scale(),.001f)-1280)/2f;
        p.x=Math.max(-edge+half,Math.min(1280+edge-half,p.x));return p;
    }
    private void actionDpad(Canvas c){
        PointF p=selectorCenter();float s=selectorSize();
        c.save();c.translate(p.x,p.y);c.scale(s,s);
        paint.setStyle(Paint.Style.FILL);paint.setColor(0x99131e07);
        c.drawRoundRect(-150,-27,150,27,27,27,paint);
        paint.setStyle(Paint.Style.STROKE);paint.setColor(themeColor());paint.setStrokeWidth(1.6f);
        c.drawRoundRect(-150,-27,150,27,27,27,paint);
        for(int i=0;i<4;i++){
            float x=-118+i*59+29.5f;boolean down=false;
            for(int k=0;k<controls.size();k++)if(("selector_"+i).equals(controls.valueAt(k)))down=true;
            paint.setStyle(Paint.Style.FILL);paint.setColor(down?0xaa708c24:0x6015210a);
            c.drawRoundRect(x-28,-24,x+28,24,9,9,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setColor(themeColor());paint.setStrokeWidth(down?2.5f:1.1f);
            c.drawRoundRect(x-28,-24,x+28,24,9,9,paint);
            drawMask(c,SELECTOR_ICONS[i],x,0,19);
        }
        paint.setStyle(Paint.Style.STROKE);paint.setColor(themeColor());paint.setStrokeWidth(3);
        c.drawLine(-129,-7,-136,0,paint);c.drawLine(-136,0,-129,7,paint);
        c.drawLine(129,-7,136,0,paint);c.drawLine(136,0,129,7,paint);
        c.restore();
    }
    private void actionStick(Canvas c,String name) {
        PointF p=visibleCenter(name);
        float r=80*controlSize();
        actionSurface(c,p.x,p.y,r,false);
        float kx=p.x,ky=p.y;
        for(int i=0;i<controls.size();i++) {
            if(name.equals(controls.valueAt(i))) {
                PointF q=positions.get(controls.keyAt(i));
                if(q!=null) {
                    float dx=displayX(q.x)-p.x;
                    kx=p.x+Math.max(-r*.58f,Math.min(r*.58f,dx));
                    ky=p.y+Math.max(-r*.58f,Math.min(r*.58f,q.y-p.y));
                }
                break;
            }
        }
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xa0789243);
        c.drawCircle(kx,ky,31*controlSize(),paint);
    }
    @Override protected void onDraw(Canvas c) {
        super.onDraw(c);refreshTheme();c.save();c.translate(offsetX(),offsetY());c.scale(scale(),scale());
        if(actionStyle) {
            actionButton(c,"l1",52,"L1");actionButton(c,"l2",52,"L2");
            actionButton(c,"r1",52,"R1");actionButton(c,"r2",52,"R2");
            actionButton(c,"touchpad",34,"");actionButton(c,"options",34,"");
            actionDpad(c);
            actionButton(c,"triangle",46,"△");actionButton(c,"square",46,"□");
            actionButton(c,"circle",46,"○");actionButton(c,"cross",46,"×");
            actionStick(c,"leftstick");actionStick(c,"rightstick");
            actionButton(c,"l3",40,"L3");actionButton(c,"r3",40,"R3");
            actionButton(c,"shake",37,"");
        } else {
            rectangle(c,"l2",108,"L2");rectangle(c,"l1",108,"L1");
            rectangle(c,"r1",108,"R1");rectangle(c,"r2",108,"R2");
            rectangle(c,"touchpad",126,"TOUCH");rectangle(c,"options",108,"OPTIONS");
            circle(c,"up",39,"▲",0xffc6ad76);circle(c,"left",39,"◀",0xffc6ad76);
            circle(c,"right",39,"▶",0xffc6ad76);circle(c,"down",39,"▼",0xffc6ad76);
            circle(c,"triangle",39,"△",0xff83cdaa);circle(c,"circle",39,"○",0xffe38288);
            circle(c,"cross",39,"×",0xff88aaff);circle(c,"square",39,"□",0xffd6a0ce);
            drawStick(c,"leftstick");drawStick(c,"rightstick");
            circle(c,"l3",29,"L3",0xffa0aec0);circle(c,"r3",29,"R3",0xffa0aec0);
            rectangle(c,"shake",118,"AGITAR");
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
