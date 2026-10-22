package org.d2r.android.port;

/** Focused JDK regression for the production text-dialog validation rules. */
public final class TextEntryRulesTest {
    private static int checks;
    private static int failures;

    private static void check(boolean condition, String label) {
        checks++;
        if (!condition) {
            failures++;
            System.err.println("FAIL " + checks + ": " + label);
        }
    }

    public static void main(String[] args) {
        // A name IME still composing pinyin must not be submittable: accepting
        // "ni" here would store Latin candidate text instead of the chosen name.
        check(!TextEntryRules.valid("ni", true, -1, true), "composing name rejected");
        check(!TextEntryRules.valid("你好", true, -1, true), "composing committed-looking text rejected");
        check(!TextEntryRules.valid("12", false, 99, true), "composing numeric rejected");

        check(TextEntryRules.valid("", true, -1, false), "empty name keeps random-name behavior");
        check(TextEntryRules.valid("Hero", true, -1, false), "English name");
        check(TextEntryRules.valid("a_b", true, -1, false), "one separator inside name");
        check(TextEntryRules.valid("法师", true, -1, false), "two Chinese characters");
        check(TextEntryRules.valid("法师法师法", true, -1, false), "five Chinese chars / 15 UTF-8 bytes");
        check(!TextEntryRules.valid("法师法师法师", true, -1, false), "six Chinese chars / 18 UTF-8 bytes");
        check(!TextEntryRules.valid("A", true, -1, false), "one-character name too short");
        check(!TextEntryRules.valid("-ab", true, -1, false), "leading separator");
        check(!TextEntryRules.valid("ab-", true, -1, false), "trailing separator");
        check(!TextEntryRules.valid("a-b-c", true, -1, false), "more than one separator");
        check(!TextEntryRules.valid("Hero!", true, -1, false), "unsupported ASCII punctuation");
        check(!TextEntryRules.valid("abcdefghijklmnop", true, -1, false), "16-byte English name");

        check(TextEntryRules.valid("", false, -1, false), "ordinary empty text remains valid");
        check(TextEntryRules.valid("123", false, 999, false), "numeric below limit");
        check(TextEntryRules.valid("999", false, 999, false), "numeric at limit");
        check(!TextEntryRules.valid("1000", false, 999, false), "numeric over limit");
        check(!TextEntryRules.valid("", false, 999, false), "empty numeric rejected");
        check(!TextEntryRules.valid("12x", false, 999, false), "non-numeric rejected");

        if (failures != 0) {
            System.err.println("TextEntryRulesTest: " + checks + " checks, " + failures + " failures");
            System.exit(1);
        }
        System.out.println("PASS: production text-entry rules, including IME composition and UTF-8 length: "
            + checks + " checks");
    }
}
