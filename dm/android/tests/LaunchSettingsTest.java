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

        if (failures != 0) throw new AssertionError(failures + " launch-settings failures");
        System.out.println("PASS: debug_overlay persists on/off without dropping unknown keys");
    }
}
