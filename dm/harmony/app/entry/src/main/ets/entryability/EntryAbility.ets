// 应用入口：横屏全屏、游戏前台常亮；窗口状态转成原生生命周期事件（0 失焦 1 后台 2 前台 3 激活，同安卓）。
// App entry: landscape full screen, screen kept on while the game is in front; window states become native lifecycle events
// (0 unfocused, 1 background, 2 foreground, 3 active, as on Android).
import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { hilog } from '@kit.PerformanceAnalysisKit';
import dm from 'libdm.so';
import { refreshForms } from '../entryformability/EntryFormAbility';

export default class EntryAbility extends UIAbility {
  // 退到后台时刷新桌面卡片（存档此时已写完）| Refresh the home-screen cards when going to the background (the saves are written by then)
  onBackground(): void { refreshForms(this.context.filesDir); }
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    AppStorage.setOrCreate('filesDir', this.context.filesDir);
    AppStorage.setOrCreate('lang', this.context.config.language ?? 'zh');
  }
  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.getMainWindow().then((win: window.Window) => {
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarEnable([]);
      win.setPreferredOrientation(window.Orientation.AUTO_ROTATION_LANDSCAPE);
      win.setWindowKeepScreenOn(true);
      const avoid = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_CUTOUT);
      AppStorage.setOrCreate('safeLeftPx', avoid.leftRect.width);
      AppStorage.setOrCreate('safeRightPx', avoid.rightRect.width);
      AppStorage.setOrCreate('safeBottomPx', avoid.bottomRect.height);
    }).catch((e: Error) => hilog.warn(0, 'dm', 'window setup: %{public}s', e.message));
    windowStage.on('windowStageEvent', (ev: window.WindowStageEventType) => {
      const map = new Map<window.WindowStageEventType, number>([
        [window.WindowStageEventType.SHOWN, 2], [window.WindowStageEventType.ACTIVE, 3],
        [window.WindowStageEventType.INACTIVE, 0], [window.WindowStageEventType.HIDDEN, 1]]);
      const v = map.get(ev);
      if (v !== undefined) { dm.focus(v); }
    });
    windowStage.loadContent('pages/Index');
  }
}
