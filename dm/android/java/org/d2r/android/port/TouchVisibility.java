package org.d2r.android.port;

// Pure visibility policy shared by D2RActivity and the host-JDK fixture.
// Actual native live readiness only; the debug property alone never hides the fallback.
final class TouchVisibility {
    static boolean visible(boolean controlsEnabled, boolean activityResumed, boolean focused,
                           boolean textDialog, boolean physicalController, boolean liveReady, boolean destroyed) {
        return controlsEnabled && activityResumed && focused && !textDialog &&
               !physicalController && !liveReady && !destroyed;
    }
}
