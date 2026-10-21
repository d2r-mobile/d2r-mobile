// #170 主机测试用最小 Handler stub：把回调投递到目标 Looper 的 FIFO 队列。
// #170 minimal host-test Handler stub: posts callbacks onto the target Looper's FIFO queue.
package android.os;

public class Handler {
    private final Looper looper;

    public Handler(Looper looper) {
        if (looper == null) throw new IllegalArgumentException("looper == null");
        this.looper = looper;
    }

    public boolean post(Runnable runnable) {
        looper.enqueue(runnable);
        return true;
    }
}
