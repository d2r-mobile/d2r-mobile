package org.d2r.android.port;

// Pure visibility policy shared by D2RActivity and the host-JDK fixture.
// The legacy pad is an explicit user mode, never a startup/readiness fallback.
final class TouchVisibility {
    static boolean visible(boolean controlsEnabled, boolean activityResumed, boolean focused,
                           boolean textDialog, boolean physicalController, boolean virtualController, boolean destroyed) {
        return controlsEnabled && activityResumed && focused && !textDialog &&
               !physicalController && virtualController && !destroyed;
    }

    // Draw-only live layer appears only while the actual provider is ready.
    static boolean liveOverlay(boolean controlsEnabled, boolean activityResumed, boolean focused,
                               boolean textDialog, boolean physicalController, boolean liveReady, boolean destroyed) {
        return controlsEnabled && activityResumed && focused && !textDialog &&
               !physicalController && liveReady && !destroyed;
    }
}
