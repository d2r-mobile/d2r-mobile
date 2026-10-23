package com.xiaoboluo.d2r;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
public final class PadInputTest {
    public static void main(String[] args) {
        InputDevice.devices.put(1,new InputDevice());
        PadInput.onMotion(new MotionEvent(1).axis(MotionEvent.AXIS_LTRIGGER,1));
        check(DmActivity.buttons==256,"analog trigger pressed");
        PadInput.reset();
        check(DmActivity.buttons==0,"pause releases analog-only trigger");
        PadInput.onMotion(new MotionEvent(1).axis(MotionEvent.AXIS_HAT_Y,-1));
        PadInput.reset();
        PadInput.onMotion(new MotionEvent(1).axis(MotionEvent.AXIS_HAT_Y,-1));
        check(DmActivity.buttons==8192,"same hat direction works after resume");
        PadInput.onKey(new KeyEvent(1,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_DPAD_UP));
        PadInput.onKey(new KeyEvent(1,KeyEvent.ACTION_UP,KeyEvent.KEYCODE_DPAD_UP));
        check(DmActivity.buttons==8192,"key release cannot clear held hat axis");
        PadInput.reset();
        PadInput.onMotion(new MotionEvent(1).axis(MotionEvent.AXIS_X,.8f));
        check(PadInput.isPadMode() && DmActivity.lx>26000,"stick takes controller mode");
        PadInput.onTouch();
        check(!PadInput.isPadMode() && DmActivity.lx==0,"touch suppresses held stick");
        PadInput.onMotion(new MotionEvent(1).axis(MotionEvent.AXIS_X,.8f));
        check(!PadInput.isPadMode(),"held stick cannot steal touch mode");
        PadInput.onMotion(new MotionEvent(1));
        PadInput.onMotion(new MotionEvent(1).axis(MotionEvent.AXIS_X,.8f));
        check(PadInput.isPadMode(),"recenter then move takes over");
        PadInput.reset();
        PadInput.onKey(new KeyEvent(1,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_A));
        check(DmActivity.buttons==1 && DmActivity.glyph==2,"A and Xbox glyph");
        InputDevice.devices.remove(1);
        PadInput.removeDevice(1);
        check(DmActivity.buttons==0 && !PadInput.isPadMode(),"disconnect releases input");
        InputDevice.devices.put(2,new InputDevice()); InputDevice.devices.put(3,new InputDevice());
        PadInput.onKey(new KeyEvent(2,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_A));
        PadInput.onKey(new KeyEvent(3,KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_B));
        check(DmActivity.buttons==3,"multiple controllers merge buttons");
        InputDevice.devices.remove(2); PadInput.sweep();
        check(DmActivity.buttons==2,"disconnect preserves other controller");
        PadInput.reset(); check(DmActivity.buttons==0,"reset clears merged input");
        InputDevice.devices.remove(3); PadInput.removeDevice(3);
        check(!PadInput.isPadMode(),"disconnect during pause restores touch mode");
        System.out.println("PadInput checks passed");
    }
    private static void check(boolean ok,String name){if(!ok)throw new AssertionError(name);}
}
