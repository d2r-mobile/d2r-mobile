package android.content;

/** Narrow host-test Context boundary; production code is compiled against android.jar. */
public class Context {
    public static final String INPUT_METHOD_SERVICE = "input_method";

    public Object getSystemService(String name) {
        return null;
    }
}
