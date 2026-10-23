import { BackupExtensionAbility, BundleVersion } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

// 系统备份（设置 > 华为云空间 / 手机克隆 / 电脑备份）：只备份存档目录 files/save/（backup_config.json）；导入的游戏资源是玩家自己的那份，从不备份。
// System backup (Settings > HUAWEI Cloud / Phone Clone / PC backup): only the saves directory files/save/ (backup_config.json); the imported game
// resources are the player's own copy and are never backed up.
export default class BackupExtension extends BackupExtensionAbility {
  async onBackup(): Promise<void> { hilog.info(0xD2E0, 'XblBackup', 'backup saves'); }
  async onRestore(bundleVersion: BundleVersion): Promise<void> { hilog.info(0xD2E0, 'XblBackup', 'restore from %{public}s', bundleVersion.name); }
}
