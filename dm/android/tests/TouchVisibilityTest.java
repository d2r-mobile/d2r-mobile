package org.d2r.android.port;

// Host-JDK fixture for the production visibility policy. No Android APIs.
public final class TouchVisibilityTest {
    private static int failures;
    private static void check(boolean ok, String what) {
        if (!ok) { failures++; System.err.println("FAIL: " + what); }
    }
    public static void main(String[] args) {
        // Startup: native live is false even though the property may be enabled; legacy layer is shown.
        check(TouchVisibility.visible(true, true, true, false, false, false, false),
            "startup false shows legacy controls");
        // The native readiness transition is enough; no menu/focus/lifecycle call is needed.
        check(!TouchVisibility.visible(true, true, true, false, false, true, false),
            "false -> true hides legacy controls without lifecycle");
        // Provider loss returns to the legacy fallback.
        check(TouchVisibility.visible(true, true, true, false, false, false, false),
            "provider loss restores fallback");
        // Existing gates stay authoritative.
        check(!TouchVisibility.visible(false, true, true, false, false, false, false),
            "controls disabled stays hidden");
        check(!TouchVisibility.visible(true, false, true, false, false, false, false),
            "not resumed stays hidden");
        check(!TouchVisibility.visible(true, true, false, false, false, false, false),
            "not focused stays hidden");
        check(!TouchVisibility.visible(true, true, true, true, false, false, false),
            "text dialog stays hidden");
        check(!TouchVisibility.visible(true, true, true, false, true, false, false),
            "physical controller stays hidden");
        check(!TouchVisibility.visible(true, true, true, false, false, false, true),
            "destroyed activity stays hidden");
        // A repeated true notification is idempotent at this boundary.
        check(!TouchVisibility.visible(true, true, true, false, false, true, false),
            "repeated live true remains hidden");
        // Draw-only live layer follows the same gates, but appears only when the core is ready.
        check(!TouchVisibility.liveOverlay(true, true, true, false, false, false, false),
            "live layer hidden while fallback is shown");
        check(TouchVisibility.liveOverlay(true, true, true, false, false, true, false),
            "live layer appears on readiness without lifecycle");
        check(!TouchVisibility.liveOverlay(true, true, true, false, false, true, true),
            "destroyed activity has no live layer");
        if (failures != 0) throw new AssertionError(failures + " visibility checks failed");
        System.out.println("PASS: touch visibility false/true/fallback and existing gates");
    }
}
