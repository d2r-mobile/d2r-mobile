package org.d2r.android.port;

public final class DropHapticPollerTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        int[] slot = {7};
        int[] takes = {0};
        DropHapticPoller poller = new DropHapticPoller(() -> {
            takes[0]++;
            int value = slot[0];
            slot[0] = 0;
            return value;
        });

        poller.resume();                 // stale pre-resume slot is discarded
        check(takes[0] == 1, "resume takes exactly one stale slot");
        check(poller.poll() == 0, "discarded slot is not replayed");

        slot[0] = 2;
        check(poller.poll() == 2, "fresh foreground drop is delivered");

        poller.pause();
        slot[0] = 3;                     // producer publishes while paused
        check(poller.poll() == 0, "paused poll never consumes or returns");
        poller.resume();                 // discard the pause-period slot
        check(takes[0] == 4, "resume discards once");
        check(poller.poll() == 0, "pause-period slot is not replayed");

        slot[0] = 4;
        check(poller.poll() == 4, "post-resume fresh drop vibrates");
        int before = takes[0];
        poller.resume();                 // repeated resume must not drain a live slot
        check(takes[0] == before, "running resume does not discard");
        check(poller.poll() == 0, "live slot was already consumed");

        poller.pause();
        check(!poller.running(), "pause stops polling");
        slot[0] = 5;
        check(poller.poll() == 0 && slot[0] == 5, "paused poll leaves producer slot untouched");

        System.out.println("PASS: resume discards stale pause slot, fresh foreground tiers survive, paused polls do not consume");
    }
}
