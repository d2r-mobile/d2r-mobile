package org.d2r.android.port;

import android.view.HapticFeedbackConstants;

// Android implementation of the same three drop tiers consumed by the iOS
// front end. The system constants keep this aligned with the platform API.
final class DropHaptic {
    static final int HEAVY = HapticFeedbackConstants.LONG_PRESS;
    static final int MEDIUM = HapticFeedbackConstants.CONTEXT_CLICK;
    static final int LIGHT = HapticFeedbackConstants.KEYBOARD_TAP;
    static final int INVALID = -1;

    private DropHaptic() {}

    static int feedbackForTier(int tier) {
        if (tier == 1) return HEAVY;
        if (tier == 2) return MEDIUM;
        if (tier == 3) return LIGHT;
        return INVALID;
    }
}
