// #170 主机测试用最小 Looper stub：模拟主 looper 线程与 FIFO 队列；不对应完整 Android Looper。
// #170 minimal host-test Looper stub: main-looper thread plus FIFO queue; not a full Android Looper.
package android.os;

import java.util.ArrayDeque;

public final class Looper {
    private static final ThreadLocal<Looper> CURRENT = new ThreadLocal<>();
    private static Looper main;

    private final Thread thread;
    private final ArrayDeque<Runnable> queue = new ArrayDeque<>();

    private Looper(Thread thread) {
        this.thread = thread;
    }

    public static void prepareMainLooper() {
        main = new Looper(Thread.currentThread());
        CURRENT.set(main);
    }

    public static Looper getMainLooper() {
        return main;
    }

    public static Looper myLooper() {
        return CURRENT.get();
    }

    public void enqueue(Runnable runnable) {
        queue.add(runnable);
    }

    /** 测试助手：在调用线程上按 FIFO 执行已排队回调。| Test helper: run queued callbacks FIFO on the caller thread. */
    public int drain() {
        int count = 0;
        Runnable runnable;
        while ((runnable = queue.poll()) != null) {
            runnable.run();
            count++;
        }
        return count;
    }
}
