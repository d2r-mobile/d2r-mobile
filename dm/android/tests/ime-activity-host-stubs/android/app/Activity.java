package android.app;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/** Narrow host-test Activity boundary exposing one content ViewGroup. */
public class Activity extends Context {
    private ViewGroup content;

    public void setContentView(ViewGroup content) {
        this.content = content;
    }

    @SuppressWarnings("unchecked")
    public <T extends View> T findViewById(int id) {
        return (T) content;
    }
}
