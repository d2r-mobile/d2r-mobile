import XCTest

// 生命周期：进局 → 按主屏键回桌面 30 秒 → 把同一个进程激活回来 → 再跑 20 秒。
// 判据不在这里断言（界面测试看不到游戏内部）：跑完由 tools/lifecycle.sh 拉回 Documents/boot.txt 按日志判
//（同一份日志里前后台事件齐、回来后上屏恢复、客体时钟相接、声卡回调继续涨、红行 0）。这里只保证动作序列做完、进程回到前台。
// Lifecycle: enter the run → press the home button to go to the home screen for 30 seconds → activate the same process back → run another 20 seconds.
// The criterion is not asserted here (UI tests cannot see inside the game): after the run tools/lifecycle.sh pulls back Documents/boot.txt and judges by the log
// (in the same log the foreground / background events are all there, present resumes after returning, the guest clock is continuous, the audio callback count keeps rising, 0 failure lines). Here we only guarantee that the action sequence completes and the process returns to the foreground.
final class LifecycleSmoke: XCTestCase {
    func testBackgroundAndReturn() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-mode", "boot"]
        app.launchEnvironment = ["DM_SAVE_FRESH":"warlock", "DM_UIMSG":"1", "DM_AUDIO":"1",
            "DM_KEYS":"X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,DOWN@OpenPanel:CharacterSelectPanel>UpdateCharacterInfo,A@UpdateCharacterInfo%1>CharacterSelect:SelectCharacter,A@CharacterSelect:SelectCharacter>FrontEndNavigation:ToGame"]
        app.launch()
        defer { app.terminate() }
        sleep(25)                                   // 进局并在营地站定 | enter the run and settle in the camp
        XCTAssertEqual(app.state, .runningForeground)
        XCUIDevice.shared.press(.home)
        sleep(30)                                   // 后台 30 秒 | 30 seconds in the background
        XCTAssertNotEqual(app.state, .notRunning, "后台期间进程被系统结束")
        app.activate()                              // 激活同一个进程，不重开 | activate the same process, do not relaunch
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 10))
        sleep(20)                                   // 回前台后再跑 20 秒 | run another 20 seconds after returning to the foreground
        XCTAssertEqual(app.state, .runningForeground)
    }

    // 多任务界面里划掉：进局后第 8 秒左右替玩家捡一枚 r30 进背包（DM_TEST_GIVE_INV）→ 第 25 秒从底边慢拖上来停住 = 多任务界面 →
    // 停 3 秒 → 结束进程（等同在多任务界面划掉：系统直接杀进程）。判据由 tools/lifecycle.sh switcher 读这一局的 boot.txt 与下一局（testReenter）的物品表
    // Swipe away in the app switcher: around second 8 after entering the run pick up one r30 into the inventory for the player (DM_TEST_GIVE_INV) → at second 25 drag slowly up from the bottom edge and hold = app switcher →
    // pause 3 seconds → terminate the process (equivalent to swiping away in the app switcher: the system kills the process directly). The criterion is judged by tools/lifecycle.sh switcher reading this run's boot.txt and the item table of the next run (testReenter)
    private static let enterKeys = "X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,DOWN@OpenPanel:CharacterSelectPanel>UpdateCharacterInfo,A@UpdateCharacterInfo%1>CharacterSelect:SelectCharacter,A@CharacterSelect:SelectCharacter>FrontEndNavigation:ToGame"
    func testSwitcherKill() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-mode", "boot"]
        app.launchEnvironment = ["DM_SAVE_FRESH":"warlock", "DM_UIMSG":"1", "DM_KEYS":Self.enterKeys, "DM_TEST_GIVE":"r30@200", "DM_TEST_GIVE_INV":"1"]
        app.launch()
        sleep(25)
        XCTAssertEqual(app.state, .runningForeground)
        let from = app.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.998)), to = app.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.55))
        from.press(forDuration: 0.05, thenDragTo: to, withVelocity: .slow, thenHoldForDuration: 1.0)   // 底边慢拖上来停住：进多任务界面（快甩是回桌面） | drag slowly up from the bottom edge and hold: enters the app switcher (a quick flick goes to the home screen)
        sleep(3)
        app.terminate()
    }
    // 下一局：不换存档，进同一个角色，记物品表（DM_STATE）15 秒
    // Next run: no save swap, enter the same character, record the item table (DM_STATE) for 15 seconds
    func testReenter() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-mode", "boot"]
        app.launchEnvironment = ["DM_UIMSG":"1", "DM_KEYS":Self.enterKeys, "DM_STATE":"lc_state.jsonl"]
        app.launch()
        sleep(30)
        app.terminate()
    }
}
