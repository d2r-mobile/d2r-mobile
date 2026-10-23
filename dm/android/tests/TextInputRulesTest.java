package com.xiaoboluo.d2r;

// 角色名的 UTF-8 字节限制、UTF-16 截断及数字溢出边界。
// UTF-8 name limits, UTF-16 truncation, and numeric overflow boundaries.
public class TextInputRulesTest {
    private static void equal(Object want, Object actual) {
        if (!want.equals(actual)) throw new AssertionError("want=" + want + " actual=" + actual);
    }
    public static void main(String[] args) {
        equal("你好世界中", TextInputRules.normalize("你好世界中文", 0, true, -1));
        equal("Ab-cd", TextInputRules.normalize("A1b-c_d!", 0, true, -1));
        equal("", TextInputRules.normalize("😀", 1, false, -1));
        equal("😀", TextInputRules.normalize("😀x", 2, false, -1));
        equal("2147483647", TextInputRules.normalize("99999999999999999999999999", 0, false, Integer.MAX_VALUE));
        equal("12", TextInputRules.normalize("1a2", 0, false, 100));
        equal("", TextInputRules.normalize("", 0, false, 100));
        equal(0, TextInputRules.nameError(""));
        equal(0, TextInputRules.nameError("中"));
        equal(1, TextInputRules.nameError("A"));
        equal(2, TextInputRules.nameError("Ab-"));
        equal(0, TextInputRules.nameError("Ab"));
        System.out.println("TextInputRules OK (12 cases)");
    }
}
