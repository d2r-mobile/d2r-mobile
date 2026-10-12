// 苹果托管资源包的下载扩展：系统缺省实现即可，资源包 romfs 按 essential 策略在安装时下载完。
// Download extension for Apple-hosted asset packs: the system default implementation is enough, the asset pack romfs is fully downloaded at install time under the essential policy.
import BackgroundAssets
import ExtensionFoundation
import StoreKit

@main
struct RomfsDownloader: StoreDownloaderExtension {}
