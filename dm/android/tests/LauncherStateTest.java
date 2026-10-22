package org.d2r.android.port;

// #308 launcher display boundary: verified readiness wins, saved summary is only text.
public final class LauncherStateTest {
    private static int failures;

    private static void check(boolean ok, String what) {
        if (!ok) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    public static void main(String[] args) throws Exception {
        LauncherState.ImportView ready = LauncherState.importView(true, "12 个资源、3 个着色器");
        check(ready.installed, "verified install is installed");
        check("12 个资源、3 个着色器".equals(ready.summary), "saved summary is display text");

        LauncherState.ImportView recovered = LauncherState.importView(true, null);
        check(recovered.installed, "prior install without launcher summary is installed");
        check(recovered.summary.isEmpty(), "missing summary stays empty, never invented");

        LauncherState.ImportView stale = LauncherState.importView(false, "stale summary");
        check(!stale.installed, "stale summary must not make an unready package installed");
        check(stale.summary.isEmpty(), "unready package exposes no summary");

        LauncherState.ImportView absent = LauncherState.importView(false, null);
        check(!absent.installed && absent.summary.isEmpty(), "absent package is not installed");

        check("keep me".equals(LauncherState.retainedSummary("", "keep me")),
                "driver write keeps prior summary when memory is empty");
        check("new".equals(LauncherState.retainedSummary("new", "old")),
                "current summary wins over stale saved text");

        check(LauncherState.startAllowed(true, false, null), "ready package with no recovery error may start");
        check(!LauncherState.startAllowed(true, false, "recovery failed"),
                "save recovery failure blocks start even when the package is ready");
        check(!LauncherState.startAllowed(true, true, null), "importing still blocks start");

        if (failures != 0) throw new AssertionError(failures + " launcher-state failures");
        System.out.println("PASS: launcher status uses verified readiness and retains display summary");
    }
}
