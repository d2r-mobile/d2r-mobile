import XCTest

// Apple UI测试实际生成UITouch，贯穿DmMetalView→纯C归属→dm_input_set→原版UI消息。
// The Apple UI test actually generates a UITouch, passing through DmMetalView → pure C ownership → dm_input_set → the original game's UI message.
final class TouchSmoke: XCTestCase {
    func testJoystickAndPanelRoundTrip() throws {
        continueAfterFailure = false
        let app = XCUIApplication()
        app.launchArguments = ["-mode", "boot"]
        app.launchEnvironment = ["DM_SAVE_FRESH":"warlock", "DM_WATCH":"50", "DM_OPMODE":"0",
            "DM_KEYS":"A@OpenPanel:MainMenuButtonRibbon%0.7>FrontEndNavigation:ToGame",
            "DM_TOUCH_CONTROL_LOG":"1", "DM_UIMSG":"touch_uimsg.jsonl", "DM_C2S_LOG":"touch_c2s.jsonl",
            "DM_TICKS":"touch_ticks.jsonl", "DM_TICKS_FULL":"1",
            "DM_TOUCH_SHOT":"22:touch_overlay.png", "DM_SCREEN_SHOT":"22:touch_window.png"]
        app.launch()
        defer { app.terminate() }
        let surface = app.otherElements["dm_touch_surface"]
        XCTAssertTrue(surface.waitForExistence(timeout:15))
        XCTAssertTrue(XCTNSPredicateExpectation(predicate:NSPredicate(format:"value BEGINSWITH %@", "game|"), object:surface).wait(25))
        // 期望位置由已批准的控件点尺寸与固定弧距算，不读实际rect作为期望。
        // Expected positions are computed from the approved widget point sizes and the fixed arc spacing, not by reading the actual rect as the expectation.
        let box = surface.frame
        let w = box.width, h = box.height
        func at(_ x:CGFloat,_ y:CGFloat) -> XCUICoordinate {
            return surface.coordinate(withNormalizedOffset:CGVector(dx:x/w,dy:y/h))
        }
        at(90,h-100).press(forDuration:0.15, thenDragTo:at(150,h-100))
        at(90,h-100).press(forDuration:0.15, thenDragTo:at(30,h-100))
        // 空白右侧不该变成A，不改变game模式。
        // The blank area on the right should not become A, and does not change the game mode.
        at(w*0.55,h*0.25).tap()
        let sample = (surface.value as? String) ?? ""
        XCTAssertTrue(sample.hasPrefix("game|"))
        let fields = Dictionary(uniqueKeysWithValues:sample.split(separator:"|").dropFirst().map { part -> (String,String) in
            let pair=part.split(separator:"=",maxSplits:1);return (String(pair[0]),String(pair[1]))
        })
        XCTAssertGreaterThanOrEqual(Int(fields["max"] ?? "0") ?? 0,32000)
        XCTAssertLessThanOrEqual(Int(fields["min"] ?? "0") ?? 0,-32000)
        XCTAssertEqual(fields["buttons"],"0");XCTAssertEqual(fields["lx"],"0");XCTAssertEqual(fields["ly"],"0")
        XCTAssertEqual(fields["a"],"0", "空白触点不得生成主技能A")
        // 底部功能PLUS必须经真实触摸打开原版物品栏。
        // The bottom function PLUS must open the original game's inventory panel through a real touch.
        at(w/2+120,h-30).tap()
        XCTAssertTrue(XCTNSPredicateExpectation(predicate:NSPredicate(format:"value BEGINSWITH %@", "navigation|"), object:surface).wait(5))
        let panel = XCTAttachment(screenshot:app.screenshot()); panel.name="touch_inventory";panel.lifetime = .keepAlways;add(panel)
        // 左下第一个技能位在导航态为B返回，必须恢复game。
        // The first skill slot at the bottom left is B (Back) in navigation state, and must restore game.
        at(w-56-112,h-82).tap()
        XCTAssertTrue(XCTNSPredicateExpectation(predicate:NSPredicate(format:"value BEGINSWITH %@", "game|"), object:surface).wait(5))
        let world = XCTAttachment(screenshot:app.screenshot());world.name="touch_game";world.lifetime = .keepAlways;add(world)
        // 留够本次透明层/整窗诊断输出的22秒触发时刻；上限仍受DM_WATCH保护。
        // Leave enough for the 22-second trigger moment of this run's transparent layer / whole-window diagnostic output; the limit is still protected by DM_WATCH.
        XCTAssertTrue(XCTNSPredicateExpectation(predicate:NSPredicate(format:"value CONTAINS %@", "|shot=1"), object:surface).wait(25), "透明操作层写盘完成才结束")
        app.terminate()
    }
}
private extension XCTestExpectation {
    func wait(_ seconds:TimeInterval) -> Bool { XCTWaiter.wait(for:[self],timeout:seconds) == .completed }
}
