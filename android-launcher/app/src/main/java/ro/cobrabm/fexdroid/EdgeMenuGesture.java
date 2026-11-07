package ro.cobrabm.fexdroid;

import android.view.MotionEvent;

/** Observe normal input until a single-finger horizontal swipe starts at the left edge. */
final class EdgeMenuGesture {
    static final int PASS=0,OPEN=1,CONSUME=2;
    private final float edge,travel,vertical;
    private boolean candidate,opened;
    private float downX,downY;
    EdgeMenuGesture(float density){edge=24*density;travel=48*density;vertical=32*density;}
    int onEvent(MotionEvent e) {
        int action=e.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN) {
            downX=e.getX();downY=e.getY();opened=false;
            candidate=e.getPointerCount()==1 && downX>=0 && downX<=edge;
            return PASS;
        }
        if(opened) {
            if(action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL)opened=false;
            return CONSUME;
        }
        if(e.getPointerCount()!=1 || action==MotionEvent.ACTION_POINTER_DOWN ||
            action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_CANCEL) {candidate=false;return PASS;}
        if(!candidate || action!=MotionEvent.ACTION_MOVE)return PASS;
        float dx=e.getX()-downX,dy=Math.abs(e.getY()-downY);
        if(dx< -edge || dy>vertical){candidate=false;return PASS;}
        if(dx>=travel && dx>dy*1.5f){candidate=false;opened=true;return OPEN;}
        return PASS;
    }
}
