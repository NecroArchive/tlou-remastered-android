package ro.cobrabm.fexdroid;

import android.os.Handler;
import android.os.Looper;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashSet;
import java.util.Set;

/** Android gamepad events -> tlouport's atomic pad-state file, without /dev/input permissions. */
final class PadBridge {
    private final File state;
    private boolean swapFaceButtons;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Set<String> keys = new LinkedHashSet<>();
    private final Set<String> virtualKeys = new LinkedHashSet<>();
    private int lx=128, ly=128, rx=128, ry=128, l2, r2, hatX, hatY;
    private int virtualLx=128, virtualLy=128, virtualRx=128, virtualRy=128, virtualL2, virtualR2;
    private boolean virtualLeftActive, virtualRightActive;
    private boolean queued;

    PadBridge(File state) { this.state=state; reset(); }

    void setSwapFaceButtons(boolean enabled) { reset(); swapFaceButtons=enabled; }

    String name(KeyEvent e) {
        // Xbox / XInput controllers supply standardized Android KEYCODE_BUTTON_*
        // events. Their Linux scancode numbers are vendor-dependent, so never
        // map arbitrary raw scancodes ahead of Android's interpreted keycode.
        switch (e.getKeyCode()) {
            case KeyEvent.KEYCODE_BUTTON_A: return "cross";
            case KeyEvent.KEYCODE_BUTTON_B: return "circle";
            case KeyEvent.KEYCODE_BUTTON_X: return swapFaceButtons ? "triangle" : "square";
            case KeyEvent.KEYCODE_BUTTON_Y: return swapFaceButtons ? "square" : "triangle";
            case KeyEvent.KEYCODE_BUTTON_L1: return "l1";
            case KeyEvent.KEYCODE_BUTTON_R1: return "r1";
            case KeyEvent.KEYCODE_BUTTON_L2: return "l2";
            case KeyEvent.KEYCODE_BUTTON_R2: return "r2";
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return "l3";
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return "r3";
            case KeyEvent.KEYCODE_BUTTON_START: return "options";
            case KeyEvent.KEYCODE_BUTTON_SELECT: return "touchpad";
            case KeyEvent.KEYCODE_DPAD_UP: return "up";
            case KeyEvent.KEYCODE_DPAD_DOWN: return "down";
            case KeyEvent.KEYCODE_DPAD_LEFT: return "left";
            case KeyEvent.KEYCODE_DPAD_RIGHT: return "right";
        }
        // Old controllers lacking Android button keycodes keep their
        // historically verified Thor/Linux fallback, scoped to gamepad devices.
        if (!e.isFromSource(InputDevice.SOURCE_GAMEPAD) &&
            !e.isFromSource(InputDevice.SOURCE_JOYSTICK)) return null;
        switch (e.getScanCode()) {
            case 304: return "cross"; case 305: return "circle";
            case 307: return swapFaceButtons ? "triangle" : "square";
            case 308: return swapFaceButtons ? "square" : "triangle";
            case 310: return "l1"; case 311: return "r1";
            case 312: return "l2"; case 313: return "r2";
            case 314: return "touchpad"; case 315: return "options";
            case 317: return "l3"; case 318: return "r3";
            default: return null;
        }
    }

    boolean key(KeyEvent e) {
        String name=name(e);
        if (name==null) return false;
        if (e.getAction()==KeyEvent.ACTION_DOWN) keys.add(name);
        else if (e.getAction()==KeyEvent.ACTION_UP) keys.remove(name);
        // Publish button transitions immediately; the runtime samples at 20 ms intervals.
        write();
        return true;
    }

    private static InputDevice.MotionRange range(InputDevice d,int axis) {
        if(d==null)return null;
        InputDevice.MotionRange r=d.getMotionRange(axis,InputDevice.SOURCE_JOYSTICK);
        return r!=null?r:d.getMotionRange(axis);
    }
    private static float axis(MotionEvent e,int axis) {
        return e.getAxisValue(axis);
    }
    private static int trigger(float v,InputDevice.MotionRange r) {
        // XInput fallback triggers can report -1..+1, unlike 0..1 LTRIGGER.
        if(r!=null && r.getMin()<0 && r.getMax()>r.getMin())
            v=(v-r.getMin())/(r.getMax()-r.getMin());
        return Math.max(0,Math.min(255,Math.round(v*255)));
    }
    boolean motion(MotionEvent e) {
        if (!e.isFromSource(InputDevice.SOURCE_JOYSTICK)) return false;
        final InputDevice device=e.getDevice();
        final boolean hasRxRy=range(device,MotionEvent.AXIS_RX)!=null &&
                              range(device,MotionEvent.AXIS_RY)!=null;
        lx=stick(axis(e,MotionEvent.AXIS_X));
        ly=stick(axis(e,MotionEvent.AXIS_Y));
        // Xbox / XInput typically reports right stick as RX/RY and triggers
        // on Z/RZ; classic Android pads use Z/RZ for the right stick.
        rx=stick(axis(e,hasRxRy?MotionEvent.AXIS_RX:MotionEvent.AXIS_Z));
        ry=stick(axis(e,hasRxRy?MotionEvent.AXIS_RY:MotionEvent.AXIS_RZ));
        int leftAxis=range(device,MotionEvent.AXIS_LTRIGGER)!=null
                ?MotionEvent.AXIS_LTRIGGER:range(device,MotionEvent.AXIS_BRAKE)!=null
                ?MotionEvent.AXIS_BRAKE:hasRxRy?MotionEvent.AXIS_Z:-1;
        int rightAxis=range(device,MotionEvent.AXIS_RTRIGGER)!=null
                ?MotionEvent.AXIS_RTRIGGER:range(device,MotionEvent.AXIS_GAS)!=null
                ?MotionEvent.AXIS_GAS:hasRxRy?MotionEvent.AXIS_RZ:-1;
        l2=leftAxis<0?0:trigger(axis(e,leftAxis),range(device,leftAxis));
        r2=rightAxis<0?0:trigger(axis(e,rightAxis),range(device,rightAxis));
        hatX=Math.round(axis(e,MotionEvent.AXIS_HAT_X));
        hatY=Math.round(axis(e,MotionEvent.AXIS_HAT_Y));
        if (!queued) { queued=true; handler.postDelayed(() -> { queued=false; write(); },8); }
        return true;
    }

    private static int stick(float v) { return Math.abs(v)<0.06f ? 128 : Math.max(0,Math.min(255,Math.round((v+1)*127.5f))); }

    void virtual(Set<String> buttons, int leftX, int leftY, int rightX, int rightY,
                 boolean leftActive, boolean rightActive, int leftTrigger, int rightTrigger) {
        virtualKeys.clear(); virtualKeys.addAll(buttons);
        virtualLx=leftX; virtualLy=leftY; virtualRx=rightX; virtualRy=rightY;
        virtualLeftActive=leftActive; virtualRightActive=rightActive;
        virtualL2=leftTrigger; virtualR2=rightTrigger;
        write();
    }

    void reset() {
        keys.clear(); lx=ly=rx=ry=128; l2=r2=hatX=hatY=0;
        virtualKeys.clear(); virtualLx=virtualLy=virtualRx=virtualRy=128;
        virtualL2=virtualR2=0; virtualLeftActive=virtualRightActive=false;
        write();
    }

    private void write() {
        StringBuilder text=new StringBuilder();
        Set<String> all=new LinkedHashSet<>(keys); all.addAll(virtualKeys);
        int leftTrigger=Math.max(l2,virtualL2), rightTrigger=Math.max(r2,virtualR2);
        for (String key:all) {
            if ((key.equals("l2") && leftTrigger>0) || (key.equals("r2") && rightTrigger>0)) continue;
            text.append(key).append(' ');
        }
        if (hatX<0) text.append("left "); if (hatX>0) text.append("right ");
        if (hatY<0) text.append("up "); if (hatY>0) text.append("down ");
        text.append("lx=").append(virtualLeftActive ? virtualLx : lx)
            .append(" ly=").append(virtualLeftActive ? virtualLy : ly)
            .append(" rx=").append(virtualRightActive ? virtualRx : rx)
            .append(" ry=").append(virtualRightActive ? virtualRy : ry)
            .append(" l2=").append(leftTrigger).append(" r2=").append(rightTrigger).append('\n');
        File temp=new File(state.getPath()+".tmp");
        try {
            state.getParentFile().mkdirs();
            try (FileOutputStream out=new FileOutputStream(temp)) {
                out.write(text.toString().getBytes(StandardCharsets.US_ASCII));
            }
            if (!temp.renameTo(state)) throw new java.io.IOException("Cannot publish controller state");
        } catch (java.io.IOException ex) { android.util.Log.e("God of War III", "Controller bridge",ex); }
    }
}
