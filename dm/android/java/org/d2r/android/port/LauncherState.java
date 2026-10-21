// 启动界面的纯状态规则：显示层只依据真实安装就绪状态，摘要只作为展示文本。
// 这样已安装但丢失旧摘要的包仍显示为可用，而残留摘要永远不能把未就绪的包变成“已导入”。
// Pure launcher display rules: readiness comes from the verified installed package; the summary is
// display text only. A ready install without a saved summary still shows as installed, while a stale
// summary can never make an absent/broken package look imported.
package org.d2r.android.port;

final class LauncherState {
    private LauncherState() { }

    static final class ImportView {
        final boolean installed;
        final String summary;

        ImportView(boolean installed, String summary) {
            this.installed = installed;
            this.summary = summary;
        }
    }

    static ImportView importView(boolean ready, String savedSummary) {
        if (!ready) return new ImportView(false, "");
        return new ImportView(true, savedSummary == null ? "" : savedSummary.trim());
    }

    // The launcher state file is display-only, but choosing a driver rewrites its other key.
    // Keep a previously saved summary when the in-memory copy is empty.
    static String retainedSummary(String current, String saved) {
        if (current != null && !current.isEmpty()) return current.trim();
        return saved == null ? "" : saved.trim();
    }
}
