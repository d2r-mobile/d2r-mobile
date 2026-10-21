// #170 主机测试用窄 stub；仅覆盖 helper 使用的属性子集。
// #170 narrow host-test stub covering only the attributes used by the helper.
package android.media;

public class AudioAttributes {
    public static final int USAGE_GAME = 14;
    public static final int CONTENT_TYPE_MUSIC = 2;

    private final int usage;
    private final int contentType;

    private AudioAttributes(Builder b) {
        this.usage = b.usage;
        this.contentType = b.contentType;
    }

    public int getUsage() { return usage; }
    public int getContentType() { return contentType; }

    public static final class Builder {
        private int usage;
        private int contentType;

        public Builder setUsage(int usage) { this.usage = usage; return this; }
        public Builder setContentType(int contentType) { this.contentType = contentType; return this; }
        public AudioAttributes build() { return new AudioAttributes(this); }
    }
}
