package android.view;

import android.content.Context;

import java.util.ArrayList;
import java.util.List;

/** Narrow host-test ViewGroup boundary with real child ownership. */
public class ViewGroup extends View implements ViewParent {
    public static class LayoutParams {
        public final int width;
        public final int height;

        public LayoutParams(int width, int height) {
            this.width = width;
            this.height = height;
        }
    }

    private final List<View> children = new ArrayList<>();

    public ViewGroup(Context context) {
        super(context);
    }

    public void addView(View child) {
        addView(child, new LayoutParams(1, 1));
    }

    public void addView(View child, LayoutParams params) {
        if (child == null) throw new IllegalArgumentException("child == null");
        if (child.getParent() != null) throw new IllegalStateException("child already attached");
        children.add(child);
        child.setParent(this);
    }

    public void removeView(View child) {
        if (children.remove(child)) child.setParent(null);
    }

    public int getChildCount() {
        return children.size();
    }

    public View getChildAt(int index) {
        return children.get(index);
    }
}
