# 英雄桌面卡片 / vivo 原子组件候选

新增两种只读 RemoteViews 卡片：2×2 显示最近保存的英雄，4×2 最多显示最近三个英雄。显示名字、等级、职业及能确认读取的经验值和金币，整卡打开游戏，刷新按钮单独更新。没有英雄或存档不可读时显示对应本地化状态。

代码已集成并编译通过，vivo 系统已登记两个 provider；桌面显示、刷新及真实存档兼容仍待验证，**尚未取得 vivo 原子组件后台权限、审核或商店上架资格**。普通 AppWidget 可运行或声明 `vivo_widget=true`，均不等于进入 vivo 原子组件库或获得一键加桌能力。

## 数据边界

只读 `getFilesDir()/save/<目录>/<目录>.d2s`，对应 Android `jni_android.c` 的已提交 `DM_SAVE`；不读取 `save.work`，不写入、导入、删除存档，不启动游戏运算。目录/文件符号链接、超长、截断、校验和错误、非法 UTF-8、未知职业和未知版本均拒绝。文件读取在广播线程之外执行。

字段位置复用本机 Claude 鸿蒙 `SaveSummary.ts` 的语义：职业 `0x18`、等级 `0x1b`、最后游玩时间 `0x20`、UTF-8 名字 `0x12b`，经验和金币来自 `gf` 位流。支持有现有源码依据的 `0x61` 和 `0x69`，不猜其他版本。统计段还要求合法终止、已知有序字段和与头部一致的等级；不能确认统计段时保留有效名字/等级，经验和金币显示不可读。只有两个金币字段均读到才相加。

当前不提供经验百分比或进度条：尚未接入玩家资源生成的经验表，未复制鸿蒙旧代码内的经验表进入 Git。首版也不提供游戏提取头像、上次游玩日期文案、卡片内游戏操作。4×2 用三列自适应展示，没有将鸿蒙 `2*4` 名称直接当成 Android 网格。

## 集成

已集成的 Manifest 双 receiver 配置和24项中英文案表在 [README.md](README.md)。由主负责人维护 Manifest、Activity、Gradle 和统一中英表，本任务未修改这些文件。

receiver 类分别为 `.widget.HeroWidget2x2Provider`、`.widget.HeroWidget4x2Provider`，元数据分别指向 `@xml/widget_2x2_info`、`@xml/widget_4x2_info`。各 receiver 包含：

- `android.appwidget.provider`：对应 XML。
- `vivo_widget=true`。
- `vivoWidgetVersion=1`。
- `vivo.widget.description=@string/dmAndroidWidgetDescription`。

应用退到后台或确认存档成功后可调 `HeroWidgetProvider.requestUpdate(this)`，仅对已添加实例发显式更新广播。退后台不代表存档一定成功，原生提交完成事件尚未接入。用户也可点刷新；周期兜底为12小时。没有新增 native/JNI、第三方 SDK、网络权限、定时轮询或文件监测。

## 验证及未验项

`SaveSummaryReaderTest` 只生成合成存档和独立临时目录，覆盖校验和/长度、未知版本、无符号字段、UTF-8、异常统计段、最近游玩排序、部分失败、拒绝工作目录及读取不改写，运行命令见英文文档。

未进行 Android/native 构建、安装或设备操作。纯 Java 合成测试不证明真实存档版本兼容、不同字号/主题下的桌面显示、刷新投递或 vivo 平台接入已通过；这些需要主线统一验证。

## 官方依据

[技术规范](https://dev.vivo.com.cn/documentCenter/doc/845)规定原生 AppWidget 基础上的专用元数据、2×2/4×2规格、至少12小时的周期刷新、关闭拖拽缩放、禁止卡片内滑动及子线程文件读取。[接入指导](https://dev.vivo.com.cn/documentCenter/doc/850)要求应用先在商店上架，申请组件后台权限并上传 APK 审核；[审核规范](https://dev.vivo.com.cn/documentCenter/doc/829)另有产品/交互要求。官方网页为 JavaScript 壳，核查时读取了其公开官方 `/webapi/doc/info?id=<id>` 正文。本实现没有发送申请邮件或操作开发者账号。
