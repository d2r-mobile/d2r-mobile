package com.xiaoboluo.d2r;

import java.nio.charset.StandardCharsets;

// 同 iOS 输入框的角色名 / 金币规则；纯 Java 部分可在主机验证。
// Character-name and quantity rules shared with the iOS dialog; testable on the host JVM.
final class TextInputRules {
    private TextInputRules() {}

    static String normalize(String value, int maxChars, boolean name, int numericMax) {
        if (numericMax >= 0) {
            long number = 0; boolean any = false;
            for (int i = 0; i < value.length(); i++) {
                char c = value.charAt(i);
                if (c >= '0' && c <= '9') { any = true; number = Math.min(numericMax, number * 10 + c - '0'); }
            }
            value = any ? Long.toString(number) : "";
        } else if (name) {
            StringBuilder filtered = new StringBuilder(); int bytes = 0; boolean dash = false;
            for (int i = 0; i < value.length(); i++) {
                char c = value.charAt(i); int n = 0;
                if (c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z') n = 1;
                else if ((c == '-' || c == '_') && !dash) { n = 1; dash = true; }
                else if (c >= 0x4e00 && c <= 0x9fff || c >= 0x3400 && c <= 0x4dbf) n = 3;
                if (n > 0 && bytes + n <= 15) { filtered.append(c); bytes += n; }
            }
            value = filtered.toString();
        }
        if (maxChars > 0 && value.length() > maxChars) {
            int end = maxChars;
            if (Character.isHighSurrogate(value.charAt(end - 1)) && Character.isLowSurrogate(value.charAt(end))) end--;
            value = value.substring(0, end);
        }
        return value;
    }

    static int nameError(String value) {
        if (value.isEmpty()) return 0;
        if (value.getBytes(StandardCharsets.UTF_8).length < 2) return 1;
        char first = value.charAt(0), last = value.charAt(value.length() - 1);
        return first == '-' || first == '_' || last == '-' || last == '_' ? 2 : 0;
    }
}
