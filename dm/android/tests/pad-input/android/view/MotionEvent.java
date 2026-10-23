package android.view;
public class MotionEvent {
    public static final int AXIS_X=0, AXIS_Y=1, AXIS_Z=11, AXIS_RZ=14, AXIS_HAT_X=15, AXIS_HAT_Y=16,
        AXIS_LTRIGGER=17, AXIS_RTRIGGER=18, AXIS_GAS=22, AXIS_BRAKE=23;
    private final int id;
    private final float[] values=new float[24];
    public MotionEvent(int id){this.id=id;}
    public MotionEvent axis(int axis,float value){values[axis]=value;return this;}
    public float getAxisValue(int axis){return values[axis];}
    public int getSource(){return InputDevice.SOURCE_JOYSTICK;}
    public int getDeviceId(){return id;}
    public InputDevice getDevice(){return InputDevice.getDevice(id);}
}
