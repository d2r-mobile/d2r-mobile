package org.d2r.android.port;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// #308 overlay setting boundary: on/off survives launcher reload/picker/preflight and keeps foreign keys.
public final class LaunchSettingsTest {
    private static int failures;

    private static void check(boolean ok, String what) {
        if (!ok) {
            failures++;
            System.err.println("FAIL: " + what);
        }
    }

    public static void main(String[] args) throws Exception {
        File file = Files.createTempFile("launch-settings-test", ".conf").toFile();
        Files.write(file.toPath(), "# diagnostic\ninput_receipts=1\n".getBytes(StandardCharsets.UTF_8));

        LaunchSettings.write(file, "debug_overlay", "1");
        check("1".equals(LaunchSettings.read(file, "debug_overlay")), "enabled overlay reads back as 1");
        check("1".equals(LaunchSettings.read(file, "input_receipts")), "overlay write preserves foreign key");

        // Emulates returning from a file picker/preflight: the launcher reloads the same file.
        check("1".equals(LaunchSettings.read(file, "debug_overlay")), "enabled overlay survives reload/picker");

        LaunchSettings.write(file, "debug_overlay", "0");
        check("0".equals(LaunchSettings.read(file, "debug_overlay")), "disabled overlay reads back as 0");
        check("1".equals(LaunchSettings.read(file, "input_receipts")), "off write preserves foreign key");
        check("0".equals(LaunchSettings.read(file, "debug_overlay")), "disabled overlay survives reload/picker");

        String content = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        check(content.contains("# diagnostic"), "comment preserved");
        check(content.contains("input_receipts=1"), "foreign line preserved");

        for (String limit : new String[] {"0", "60", "30"}) {
            LaunchSettings.write(file, "frame_limit", limit);
            check(limit.equals(LaunchSettings.read(file, "frame_limit")), "frame limit survives reload: " + limit);
            check("1".equals(LaunchSettings.read(file, "input_receipts")), "frame limit preserves diagnostics");
            check("0".equals(LaunchSettings.read(file, "debug_overlay")), "frame limit preserves overlay state");
            String foreign = LaunchSettings.foreign(file, new String[] {"debug_overlay", "frame_limit"});
            check(foreign.contains("input_receipts=1") && !foreign.contains("frame_limit="), "launcher rewrite owns frame limit exactly once");
        }
        LaunchSettings.write(file, "resolution", "1920x1080");
        LaunchSettings.write(file, "resolution", null);
        check(LaunchSettings.read(file, "resolution") == null, "automatic resolution removes the explicit native override");
        check("30".equals(LaunchSettings.read(file, "frame_limit")), "resolution reset preserves frame limit");
        check("1".equals(LaunchSettings.read(file, "input_receipts")), "resolution reset preserves foreign diagnostics");
        check(LaunchSettings.read(file, "debug_collection") == null, "collection defaults to off when absent");
        LaunchSettings.write(file, "debug_collection", "1");
        check("1".equals(LaunchSettings.read(file, "debug_collection")), "collection enabled survives reload");
        check("0".equals(LaunchSettings.read(file, "debug_overlay")), "collection does not enable FPS overlay");
        LaunchSettings.write(file, "debug_overlay", "1");
        LaunchSettings.write(file, "debug_collection", "0");
        check("0".equals(LaunchSettings.read(file, "debug_collection")), "collection disabled survives reload");
        check("1".equals(LaunchSettings.read(file, "debug_overlay")), "FPS display remains usable without collection");

        if (failures != 0) throw new AssertionError(failures + " launch-settings failures");
        System.out.println("PASS: overlay and frame limit persist without dropping unknown keys");
    }
}
