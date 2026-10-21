package org.d2rmobile.android.diagnostic;

import android.os.Build;
import android.os.Looper;
import android.view.View;
import android.view.WindowInsets;

/**
 * Additive observer for the accepted editor View's IME insets.
 *
 * <p>It installs an {@link View.OnApplyWindowInsetsListener} on the owned
 * editor only; it never touches the Activity content/root listener. API 30+
 * observations use the real {@link WindowInsets.Type#ime()} contract. API 29
 * and below publish an explicit unknown observation and never call the API 30
 * Type/isVisible/getInsets methods. The observer does not infer visibility from
 * window height or any layout heuristic.</p>
 */
public final class DmImeInsetsObserver {
    private final Looper mainLooper;
    private View view;
    private long sessionGeneration;
    private long sequence;
    private long attachToken;
    private boolean attached;
    private DmImeInsetsObservation observation = DmImeInsetsObservation.none();

    public DmImeInsetsObserver(Looper mainLooper) {
        if (mainLooper == null) throw new IllegalArgumentException("mainLooper == null");
        this.mainLooper = mainLooper;
    }

    /** Attaches to the owned editor after an accepted show. */
    public void attach(View ownedView, long generation) {
        requireMainLooper();
        if (ownedView == null) throw new IllegalArgumentException("ownedView == null");
        detach();
        view = ownedView;
        sessionGeneration = generation;
        sequence++;
        observation = DmImeInsetsObservation.none();
        attached = true;
        final long token = ++attachToken;
        view.setOnApplyWindowInsetsListener((source, insets) -> onApplyWindowInsets(token, source, insets));
        view.requestApplyInsets();
    }

    /** Detaches and invalidates this session before hide/release. */
    public void detach() {
        requireMainLooper();
        if (view != null) {
            view.setOnApplyWindowInsetsListener(null);
        }
        view = null;
        attached = false;
        attachToken++;
        sessionGeneration = 0;
        sequence++;
        observation = DmImeInsetsObservation.none();
    }

    /** Current immutable observation; unknown on API 29 and after detach. */
    public DmImeInsetsObservation observation() {
        return observation;
    }

    private WindowInsets onApplyWindowInsets(long token, View source, WindowInsets insets) {
        if (!attached || token != attachToken || source != view) return insets;
        if (Looper.myLooper() != mainLooper) return insets;
        sequence++;
        if (Build.VERSION.SDK_INT < 30) {
            observation = DmImeInsetsObservation.unknown(sessionGeneration, sequence);
            return insets;
        }
        View root = source.getRootView();
        int width = root == null ? 0 : root.getWidth();
        int height = root == null ? 0 : root.getHeight();
        observation = DmImeInsetsApi30.observe(insets, width, height, sessionGeneration, sequence);
        return insets;
    }

    private void requireMainLooper() {
        if (Looper.myLooper() != mainLooper) {
            throw new IllegalStateException("DmImeInsetsObserver must be called on the main looper");
        }
    }
}
