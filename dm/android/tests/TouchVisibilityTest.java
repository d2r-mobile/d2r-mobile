package org.d2r.android.port;

// Execute the production policy; readiness loss must never enable virtual-controller input.
public final class TouchVisibilityTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        check(!TouchVisibility.visible(true,true,true,false,false,false,false), "startup keeps legacy pad hidden");
        check(TouchVisibility.visible(true,true,true,false,false,true,false), "explicit virtual mode shows pad");
        check(!TouchVisibility.visible(false,true,true,false,false,true,false), "controls gate");
        check(!TouchVisibility.visible(true,false,true,false,false,true,false), "pause gate");
        check(!TouchVisibility.visible(true,true,false,false,false,true,false), "focus gate");
        check(!TouchVisibility.visible(true,true,true,true,false,true,false), "text input gate");
        check(!TouchVisibility.visible(true,true,true,false,true,true,false), "physical controller gate");
        check(!TouchVisibility.visible(true,true,true,false,false,true,true), "destroy gate");
        check(TouchVisibility.liveOverlay(true,true,true,false,false,true,false), "ready live layer is visible");
        check(!TouchVisibility.liveOverlay(true,true,true,false,false,false,false), "unready live layer is hidden");
        check(!TouchVisibility.liveOverlay(true,true,true,false,true,true,false), "physical pad hides live layer");
        check(!TouchVisibility.liveOverlay(true,true,true,false,false,true,true), "destroy hides live layer");
        System.out.println("PASS: default hidden legacy pad, explicit virtual mode and lifecycle gates");
    }
}
