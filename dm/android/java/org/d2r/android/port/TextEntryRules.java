package org.d2r.android.port;

import java.nio.charset.StandardCharsets;

/**
 * Bounded text-entry validation for the production Android text dialog.
 *
 * <p>Kept outside D2RActivity so the rule can be exercised on a plain JDK
 * without an Android runtime; D2RActivity supplies the IME composition state
 * and the dialog's enabled state.</p>
 */
final class TextEntryRules {
    private TextEntryRules() {}

    /**
     * @param composing true while the IME still owns marked/composing text
     *                  (for example pinyin before the candidate is chosen)
     */
    static boolean valid(String text, boolean nameRule, int numericMax, boolean composing) {
        if (text == null) text = "";
        if (composing) return false;   // do not submit raw pinyin / marked text
        if (nameRule && !text.isEmpty()) {
            int bytes = text.getBytes(StandardCharsets.UTF_8).length;
            int separators = 0;
            for (int i = 0; i < text.length(); i++) {
                char c = text.charAt(i);
                if (c == '-' || c == '_') separators++;
                else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '\u4e00' && c <= '\u9fff') || (c >= '\u3400' && c <= '\u4dbf'))) return false;
            }
            if (bytes < 2 || bytes > 15 || separators > 1) return false;
            if (text.charAt(0) == '-' || text.charAt(0) == '_') return false;
            if (text.charAt(text.length() - 1) == '-' || text.charAt(text.length() - 1) == '_') return false;
        }
        if (numericMax >= 0) {
            if (text.isEmpty()) return false;
            try {
                long value = Long.parseLong(text);
                return value >= 0 && value <= numericMax;
            } catch (NumberFormatException error) {
                return false;
            }
        }
        return true;
    }
}
