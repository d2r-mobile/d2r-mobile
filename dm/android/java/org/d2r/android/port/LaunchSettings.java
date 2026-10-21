// launch-settings.conf 的读写：启动界面与游戏内菜单共用（菜单要把「帧率显示」写回去，重启后还记得）。
// 约定：这个文件里可能有界面上没有的键（诊断用的 input_receipts 等，见 native 侧的键白名单），
// 所以写一个键时必须把其余行**原样保留** —— 曾经因为整份重写，把 input_receipts 悄悄抹掉过。
// Reading and writing launch-settings.conf, shared by the launcher and the in-game menu (the menu writes "frame
// rate display" back so a restart remembers it). The file may hold keys the UI does not manage (a diagnostic such
// as input_receipts; see the native whitelist), so writing one key has to preserve every other line verbatim --
// rewriting the whole file once silently dropped input_receipts.
package org.d2r.android.port;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

final class LaunchSettings {
    private LaunchSettings() { }

    // 取一个键的值；没有这个文件 / 没有这个键都返回 null。
    // Read one key; returns null when the file or the key is absent.
    static String read(File file, String key) {
        if (!file.isFile()) return null;
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int separator = line.indexOf('=');
                if (separator <= 0) continue;
                if (line.substring(0, separator).trim().equals(key)) return line.substring(separator + 1).trim();
            }
        } catch (IOException ignored) {
        }
        return null;
    }

    // 文件里不归调用者管的行，原样返回（注释与空行由调用者自己写，这里只带回真正的键值行）。
    // The lines the caller does not own, verbatim (comments and blank lines are the caller's to write; only real
    // key=value lines come back here).
    static String foreign(File file, String[] owned) {
        StringBuilder kept = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int separator = line.indexOf('=');
                if (separator <= 0) continue;
                String key = line.substring(0, separator).trim();
                if (key.isEmpty() || key.startsWith("#")) continue;
                boolean mine = false;
                for (String ownedKey : owned) if (ownedKey.equals(key)) { mine = true; break; }
                if (!mine) kept.append(line.trim()).append('\n');
            }
        } catch (IOException ignored) {
        }
        return kept.toString();
    }

    // 改一个键：其余行（含注释）原样保留；目标键不存在就追加。
    // Change one key, keeping every other line (comments included); append it when absent.
    static void write(File file, String key, String value) {
        StringBuilder content = new StringBuilder();
        boolean replaced = false;
        if (file.isFile()) {
            try (BufferedReader reader = new BufferedReader(
                    new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    int separator = line.indexOf('=');
                    if (separator > 0 && line.substring(0, separator).trim().equals(key)) {
                        content.append(key).append('=').append(value).append('\n');
                        replaced = true;
                    } else {
                        content.append(line).append('\n');
                    }
                }
            } catch (IOException ignored) {
            }
        }
        if (!replaced) content.append(key).append('=').append(value).append('\n');
        try (OutputStreamWriter writer = new OutputStreamWriter(
                new FileOutputStream(file, false), StandardCharsets.UTF_8)) {
            writer.write(content.toString());
        } catch (IOException ignored) {
        }
    }
}
