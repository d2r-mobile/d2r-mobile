package org.d2r.android.port;

public final class OutputResolutionTest {
    private static void equal(String expected, String actual) {
        if (!expected.equals(actual)) throw new AssertionError(expected + " != " + actual);
    }
    public static void main(String[] args) {
        equal("67", OutputResolution.renderScale(0));
        equal("89", OutputResolution.renderScale(1));
        for (int i=2;i<7;i++) equal("100", OutputResolution.renderScale(i));
        equal("1424x640", OutputResolution.fullScreen(3200,1440,640));
        equal("1600x720", OutputResolution.fullScreen(1440,3200,720));
        equal("2000x900", OutputResolution.fullScreen(3200,1440,900));
        equal("1280x720", OutputResolution.migrate("1600x900",true));
        equal("1600x900", OutputResolution.migrate("1600x900",false));
        if (OutputResolution.migrate("invalid",true)!=null) throw new AssertionError("invalid migration");
        System.out.println("OutputResolutionTest PASS");
    }
}
