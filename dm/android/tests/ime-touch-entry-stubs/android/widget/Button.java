package android.widget;

import android.content.Context;
import android.view.View;

/** Narrow host-test Button boundary; production uses the real android.jar. */
public class Button extends View {
    public interface OnClickListener {
        void onClick(View view);
    }

    private CharSequence text;
    private boolean allCaps;
    private boolean clickable;
    private OnClickListener listener;

    public Button(Context context) {
        super(context);
    }

    public void setText(CharSequence value) {
        text = value;
    }

    public CharSequence getText() {
        return text;
    }

    public void setAllCaps(boolean value) {
        allCaps = value;
    }

    public boolean isAllCaps() {
        return allCaps;
    }

    public void setClickable(boolean value) {
        clickable = value;
    }

    public boolean isClickable() {
        return clickable;
    }

    public void setOnClickListener(OnClickListener value) {
        listener = value;
    }

    public boolean performClick() {
        if (listener == null) return false;
        listener.onClick(this);
        return true;
    }
}
