package org.d2r.android.port;

public final class DropHapticTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        check(DropHaptic.feedbackForTier(1) == DropHaptic.HEAVY, "tier 1 heavy");
        check(DropHaptic.feedbackForTier(2) == DropHaptic.MEDIUM, "tier 2 medium");
        check(DropHaptic.feedbackForTier(3) == DropHaptic.LIGHT, "tier 3 light");
        check(DropHaptic.feedbackForTier(0) == DropHaptic.INVALID, "none is ignored");
        check(DropHaptic.feedbackForTier(4) == DropHaptic.INVALID, "out of range is ignored");
        System.out.println("PASS: drop haptic tiers map to Android feedback and unknown tiers are ignored");
    }
}
