# Controller state checks

Compile the production `PadInput.java` against the small event/device/JNI fakes:

```sh
javac -d /tmp/dm-pad-tests dm/android/tests/pad-input/android/view/*.java dm/android/tests/pad-input/android/util/Log.java dm/android/tests/pad-input/com/xiaoboluo/d2r/*.java dm/android/app/src/main/java/com/xiaoboluo/d2r/PadInput.java
java -cp /tmp/dm-pad-tests com.xiaoboluo.d2r.PadInputTest
```

Checks analog-only trigger release, hat/key merging, pause/resume, stick recentering before touch takeover, device removal, glyph selection and multiple controllers. The trigger-release assertion detects the earlier lifecycle regression. These are state-machine checks; Android event routing, vendor-specific controller axes, physical disconnection callbacks and on-screen layout switching still require device validation.
