package org.d2rmobile.android.diagnostic;

import android.app.Activity;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewParent;
import android.widget.Button;

/**
 * Single owned diagnostic touch entry for the accepted NativeActivity text
 * host.
 *
 * <p>The launcher installs one clearly labelled {@link Button} into the
 * existing {@code android.R.id.content}; it never replaces the NativeActivity
 * surface, HUD or other views. Construction/installation does not request the
 * IME. Only a user click asks the registered native owner, using an opaque
 * numeric token rather than a native pointer.</p>
 *
 * <p>Native validates the owner/token and the existing active-App text guard
 * before calling the real {@code text_begin}. Late clicks after teardown,
 * paused/focus-lost/windowless Apps and a second owner are inert. Teardown
 * invalidates the token before removing only this owned button.</p>
 */
public final class DmImeTouchEntry {
    private static final String LABEL = "IME test / 输入测试";

    /** Narrow test seam; production uses the registered native method. */
    interface RequestSink {
        boolean request(long token);
    }

    private static Button ownedView;
    private static Activity ownedActivity;
    private static long ownedToken;
    private static boolean clickAuthorized;
    private static RequestSink testSink;

    private DmImeTouchEntry() {
    }

    public static boolean install(Activity activity, long token) {
        requireMainLooper();
        if (activity == null || token == 0) return false;
        if (ownedView != null) {
            if (ownedActivity != activity) return false;
            if (clickAuthorized && ownedToken == token && ownedView.getParent() != null) {
                return true;
            }
            if (!cleanupOwned()) return false;
        }
        View content = activity.findViewById(android.R.id.content);
        if (!(content instanceof ViewGroup)) return false;
        Button button = new Button(activity);
        button.setText(LABEL);
        button.setAllCaps(false);
        button.setClickable(true);
        // A launcher must consume its own click without taking text/game focus.
        button.setFocusable(false);
        button.setFocusableInTouchMode(false);
        button.setOnClickListener(view -> {
            if (clickAuthorized && ownedView == view
                    && ownedActivity == activity && ownedToken == token) {
                request(token);
            }
        });
        ViewGroup group = (ViewGroup) content;
        // Retain the cleanup handle before attaching so an attach failure can
        // be retried from cleanupOwned().
        ownedView = button;
        ownedActivity = activity;
        ownedToken = token;
        clickAuthorized = false;
        try {
            group.addView(button, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT));
        } catch (RuntimeException failure) {
            cleanupOwned();
            return false;
        }
        clickAuthorized = true;
        return true;
    }

    /**
     * Invalidates click authorization immediately, then removes exactly the
     * button this class installed. If platform removal throws, the cleanup
     * handle is retained (click remains disabled) so a retry can finish it.
     */
    public static boolean uninstall(Activity activity, long token) {
        requireMainLooper();
        if (ownedView == null) return true;
        if (ownedActivity != activity || ownedToken != token) {
            return false;
        }
        clickAuthorized = false;
        Button view = ownedView;
        view.setOnClickListener(null);
        ViewParent parent = view.getParent();
        if (parent instanceof ViewGroup) {
            ((ViewGroup) parent).removeView(view);
        }
        clearOwned();
        return true;
    }

    /**
     * Retries removal of the retained owned view without reviving its click.
     * Returns false while the platform removal still fails.
     */
    private static boolean cleanupOwned() {
        if (ownedView == null) return true;
        clickAuthorized = false;
        Button view = ownedView;
        view.setOnClickListener(null);
        ViewParent parent = view.getParent();
        if (parent instanceof ViewGroup) {
            try {
                ((ViewGroup) parent).removeView(view);
            } catch (RuntimeException failure) {
                return false;
            }
        }
        clearOwned();
        return true;
    }

    private static void clearOwned() {
        ownedView = null;
        ownedActivity = null;
        ownedToken = 0;
        clickAuthorized = false;
    }

    /** Test-only sink; never set by production callers. */
    static void setTestSink(RequestSink sink) {
        testSink = sink;
    }

    private static boolean request(long token) {
        RequestSink sink = testSink;
        return sink != null ? sink.request(token) : nativeRequest(token);
    }

    private static void requireMainLooper() {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            throw new IllegalStateException("DmImeTouchEntry must run on the main looper");
        }
    }

    private static native boolean nativeRequest(long token);
}
