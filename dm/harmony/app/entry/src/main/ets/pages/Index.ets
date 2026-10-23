// 游戏页：XComponent（原生 Vulkan 画面，libdm.so）+ 透明触控层 + 起名输入框 + 启动错误提示。
// 数据目录沿用老前端在同一应用沙箱里已导入的游戏数据（files/game-data）；新核心的存档单独放 files/save，不碰老存档 files/saves。
// Game page: XComponent (native Vulkan output from libdm.so) + transparent touch overlay + naming dialog + boot error text.
// Game data reuses what the old front end already imported into this app's sandbox (files/game-data); the new core keeps its saves in
// files/save, separate from the old saves in files/saves.
import dm from 'libdm.so';
import { fileIo, fileUri, picker } from '@kit.CoreFileKit';
import { zlib, BusinessError } from '@kit.BasicServicesKit';
import { common } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { display } from '@kit.ArkUI';
import { IosOverlay } from '../components/IosOverlay';
import { UI_TEXT_EN, UI_TEXT_ZH } from '../generated/UiText';

interface InputRequest { id: number; title: string; sub: string; guide: string; initial: string; maxChars: number; nameRule: number; numericMax: number }

@Entry
@Component
struct Index {
  @State started: boolean = false;
  @State error: string = '';
  @State request: InputRequest | null = null;
  @State text: string = '';
  @State fps: string = '';
  @State canImport: boolean = false;
  @State importing: string = '';
  private fpsTimer: number = -1;
  private lastPresents: number = 0;
  private lastFpsAt: number = 0;
  @StorageProp('safeLeftPx') safeLeftPx: number = 0;
  @StorageProp('safeRightPx') safeRightPx: number = 0;
  @StorageProp('safeBottomPx') safeBottomPx: number = 0;
  private files: string = AppStorage.get<string>('filesDir') ?? '';
  private lang: string = AppStorage.get<string>('lang') ?? 'zh';

  private t(key: string): string { return (this.lang.startsWith('zh') ? UI_TEXT_ZH : UI_TEXT_EN)[key] ?? key; }
  private dataDir(): string {
    const base = this.files + '/game-data';
    try { if (fileIo.accessSync(base + '/data/.build.config') || fileIo.accessSync(base + '/data/build.config')) { return base + '/data'; } } catch (e) { }
    return base;
  }
  private boot(): void {
    dm.setCallbacks(
      (id: number, title: string, sub: string, guide: string, initial: string, maxChars: number, nameRule: number, numericMax: number) => {
        this.text = initial; this.request = { id, title, sub, guide, initial, maxChars, nameRule, numericMax };
      },
      (code: number) => { this.started = false; this.error = `${this.t('dmAndroidStartFailed') || '启动失败'} (${code})`; });
    try { display.getDefaultDisplaySync() && dm.refresh(display.getDefaultDisplaySync().refreshRate); } catch (e) { }
    const rc = dm.start(this.files, this.files, this.dataDir(), this.lang.startsWith('zh') ? 'zh-Hans' : 'en-US');
    if (rc > 0) {
      this.started = true;
      // 调试帧率（顶部居中，同老版）：每秒取出图帧数差 | Debug FPS (top center, as the old build): presented-frame difference per second
      this.lastPresents = dm.presentCount(); this.lastFpsAt = Date.now();
      this.fpsTimer = setInterval(() => {
        const n = dm.presentCount(), t = Date.now();
        this.fps = `${Math.round((n - this.lastPresents) * 1000 / Math.max(1, t - this.lastFpsAt))} FPS`;
        this.lastPresents = n; this.lastFpsAt = t;
      }, 1000);
    } else {
      // -2 没有游戏数据，-3 没有执行映像，-4 没有着色器，-5 缺 build.config，-9/-10 overlay 缺或不符，-11 导入包与应用不配套，-12 衍生表没读全：都可以用「导入游戏资源」补齐
      // -2 no game data, -3 no execution images, -4 no shaders, -5 missing build.config, -9/-10 overlay missing or mismatched, -11 import pack does not match the app, -12 derived tables incomplete: all fixed by importing resources
      this.canImport = [-2, -3, -4, -5, -9, -10, -11, -12].indexOf(rc) >= 0;
      this.error = `${(this.t('dmAndroidStartFailed') || '启动失败（%d）').replace('%d', String(rc))}${!this.canImport ? '' : (rc === -10 || rc === -11) ? (this.lang.startsWith('zh') ? '\n导入包与这版应用不配套：请用与这版应用同一版本的工具重新生成 dm 文件夹，再点下面的按钮导入' : '\nThe import pack does not match this version of the app: regenerate the dm folder with the tools of the same version, then import it with the button below') : rc === -12 ? (this.lang.startsWith('zh') ? '\n导入包缺少 derived.tsv 或内容不全：请用这版工具重新生成 dm 文件夹，再点下面的按钮导入' : '\nThe import pack lacks a complete derived.tsv: regenerate the dm folder with this version of the tools, then import it with the button below') : [-2, -5].indexOf(rc) >= 0 ? '\n缺少游戏数据：请把游戏的 data 文件夹放进手机上一个文件夹（例如「D2R Mobile」）里，再点下面的按钮选中那个文件夹' : '\n缺少运行组件：这个版本没有内置组件，请把 dm 文件夹（out、shaders、overlay、ui-art）和 data 放进同一个文件夹，再点下面的按钮选中它'}
可执行内存自检 / exec self-test: ${dm.execProbe()}`;
    }
  }
  // 调试版内置运行包（out / shaders / overlay / ui-art 压成 dm-pack.zip，见 entry/build-profile.json5 的 default 目标和 scripts/stage_debug_pack.sh）：
  // 首次启动或包版本变了，解压到 files/dm（游戏照常从那里读），玩家只需导入游戏的 data。发布版没有内置包，直接跳过
  // Debug builds bundle the runtime pack (out / shaders / overlay / ui-art zipped as dm-pack.zip, see the default target in entry/build-profile.json5 and
  // scripts/stage_debug_pack.sh): on first launch or when the pack version changed it is unpacked into files/dm (where the game reads it), so players only import the
  // game's data. Release builds have no bundled pack and skip this
  private async preparePack(): Promise<void> {
    const src = getContext(this).resourceDir + '/dm';
    let want = '', have = '';
    try { want = fileIo.readTextSync(src + '/pack-version.txt').trim(); } catch (e) { return; }
    try { have = fileIo.readTextSync(this.files + '/dm/pack-version.txt').trim(); } catch (e) { }
    if (want === have) { return; }
    try {
      if (!fileIo.accessSync(this.files + '/dm')) { fileIo.mkdirSync(this.files + '/dm', true); }   // 已存在时 mkdirSync 抛 13900015（以前包一更新就在这里失败）| mkdirSync throws 13900015 when it exists (pack updates used to fail right here)
      try { fileIo.unlinkSync(this.files + '/dm/pack-version.txt'); } catch (e) { }   // 解压到一半被杀：下次重来 | killed halfway: unpack again next time
      this.importing = '首次启动，正在解压游戏组件（约 1 分钟）…';
      // 先解到临时目录再逐项替换：decompressFile 不覆盖已有文件（13900015），直接解到 files/dm 时包一更新就失败、旧包一直留着。
      // 只替换包里有的顶层项（out / shaders / overlay / ui-art / 管线种子），本机清单、缓存、env、标记文件不动
      // Unpack into a temporary folder, then replace entry by entry: decompressFile does not overwrite existing files (13900015), so unpacking straight into files/dm failed
      // on every pack update and the old pack stayed. Only top-level entries present in the pack (out / shaders / overlay / ui-art / pipeline seed) are replaced; the device
      // list, caches, env and marker files are left alone
      const tmp = this.files + '/dm-pack-tmp';
      try { fileIo.rmdirSync(tmp); } catch (e) { }
      if (!fileIo.accessSync(tmp)) { fileIo.mkdirSync(tmp, true); }
      await zlib.decompressFile(src + '/dm-pack.zip', tmp);
      for (const name of fileIo.listFileSync(tmp)) {
        const dst = this.files + '/dm/' + name;
        try { if (fileIo.statSync(dst).isDirectory()) { fileIo.rmdirSync(dst); } else { fileIo.unlinkSync(dst); } } catch (e) { }
        fileIo.renameSync(tmp + '/' + name, dst);
      }
      try { fileIo.rmdirSync(tmp); } catch (e) { }
      fileIo.copyFileSync(src + '/pack-version.txt', this.files + '/dm/pack-version.txt');
      hilog.info(0xD2E0, 'XblPack', 'runtime pack %{public}s unpacked', want);
    } catch (e) { hilog.error(0xD2E0, 'XblPack', 'pack unpack failed: %{public}s', JSON.stringify(e)); }
    this.importing = '';
  }
  // 导入游戏资源：选手机上的「D2R Mobile」文件夹（里面 data/ = 游戏数据，dm/ = 电脑上生成的运行包：out、shaders、overlay、ui-art），
  // 拷进本应用：data → files/romfs/data，dm/* → files/dm/*。拷完重新启动。不删源文件夹。
  // Import game resources: pick the "D2R Mobile" folder on the phone (data/ = game data, dm/ = runtime pack generated on the PC: out, shaders, overlay, ui-art),
  // copied into this app: data → files/romfs/data, dm/* → files/dm/*. Restart when done. The source folder is not deleted.
  private async importResources(): Promise<void> {
    try {
      const opts = new picker.DocumentSelectOptions(); opts.selectMode = picker.DocumentSelectMode.FOLDER; opts.maxSelectNumber = 1;
      const uris = await new picker.DocumentViewPicker(getContext(this) as common.UIAbilityContext).select(opts);
      if (!uris || uris.length === 0) { return; }
      const root = uris[0];
      this.importing = '正在检查文件夹…';
      const parts: string[][] = [['data', this.files + '/romfs'], ['dm/out', this.files + '/dm'], ['dm/shaders', this.files + '/dm'], ['dm/overlay', this.files + '/dm'], ['dm/ui-art', this.files + '/dm'], ['dm/derived.tsv', this.files + '/dm'], ['dm/pack_info.txt', this.files + '/dm']];
      let copied = 0;
      const failed: string[] = [];
      for (const part of parts) {
        const src = root + '/' + part[0];
        // 目录已存在时 mkdirSync 抛 13900015（同 preparePack）| mkdirSync throws 13900015 when the directory exists (as in preparePack)
        if (!fileIo.accessSync(part[1])) { fileIo.mkdirSync(part[1], true); }
        const name = part[0].split('/').pop() as string, target = part[1] + '/' + name;
        // 单个文件（derived.tsv、pack_info.txt）先删旧的再拷 | single files (derived.tsv, pack_info.txt): remove the old one before copying
        if (name.indexOf('.') >= 0) { try { fileIo.unlinkSync(target); } catch (e) { } }
        try {
          // copy 的目录目标必须先存在；全新壳没有 out/shaders/overlay/ui-art，不能把目标缺失误判成源缺失。
          // Directory copy needs an existing destination; a fresh shell has none of these runtime directories yet.
          if (name.indexOf('.') < 0 && !fileIo.accessSync(target)) { fileIo.mkdirSync(target, true); }
          await fileIo.copy(src, fileUri.getUriFromPath(target), {
            progressListener: (p: fileIo.Progress) => {
              this.importing = `正在导入 ${part[0]}：${Math.floor(p.processedSize / 1048576)} / ${Math.floor(p.totalSize / 1048576)} MB`;
            }
          });
        } catch (e) {
          // 13900002 = 选中的文件夹里没有这一项（只导入 data 或只导入 dm 都可以）；其余错误算导入失败，不启动，免得新旧组件混用
          // 13900002 = the picked folder has no such entry (importing only data or only dm is fine); any other error fails the import without starting, so old and new parts never mix
          const code = (e as BusinessError).code;
          hilog.warn(0xD2E0, 'XblImport', 'copy %{public}s: %{public}s', part[0], JSON.stringify(e));
          if (code !== 13900002) { failed.push(`${part[0]} (${code})`); }
          continue;
        }
        // 有的系统把源目录拷成目标目录下的子目录：拉平 | Some systems copy the source directory as a subdirectory of the target: flatten it
        try { if (fileIo.accessSync(target + '/' + name)) { const inner = target + '/' + name; for (const f of fileIo.listFileSync(inner)) { fileIo.renameSync(inner + '/' + f, target + '/' + f); } fileIo.rmdirSync(inner); } } catch (e) { }
        copied++;
      }
      if (failed.length) { this.importing = `导入没有完成（${failed.join('、')}），请检查手机存储空间后重新导入`; return; }
      if (!copied) { this.importing = '这个文件夹里没有 data 或 dm，请选中「装着它们的那个文件夹」（例如「D2R Mobile」）'; return; }
      this.importing = '导入完成，正在启动…'; this.error = ''; this.canImport = false;
      this.boot(); this.importing = '';
    } catch (e) { this.importing = `导入失败：${JSON.stringify(e)}`; }
  }
  private finishInput(accepted: boolean): void {
    if (!this.request) { return; }
    dm.textResult(this.request.id, accepted, accepted ? this.text : ''); this.request = null;
  }

  build() {
    Stack() {
      XComponent({ id: 'dm', type: XComponentType.SURFACE, libraryname: 'dm' })
        .width('100%').height('100%')
        .onLoad(() => { this.preparePack().then(() => this.boot()); })
      IosOverlay({
        inputEnabled: this.started && this.request === null,
        overlayRoot: this.files + '/dm/overlay',
        artRoot: this.files + '/dm/ui-art',
        safeLeft: px2vp(this.safeLeftPx), safeRight: px2vp(this.safeRightPx), safeBottom: px2vp(this.safeBottomPx)
      }).width('100%').height('100%')
      if (this.fps) {
        Text(this.fps).fontColor('#ff66dd66').fontSize(14).backgroundColor('#66000000').padding({ left: 8, right: 8, top: 2, bottom: 2 })
          .borderRadius(6).position({ x: '50%', y: 6 }).translate({ x: '-50%' }).hitTestBehavior(HitTestMode.Transparent)
      }
      if (this.error || this.importing) {
        Column({ space: 16 }) {
          if (this.error) { Text(this.error).fontColor('#ffcdb98c').fontSize(16) }
          if (this.canImport && !this.importing) {
            Button('导入（选择装着 data 的文件夹）').onClick(() => { this.importResources(); })
          }
          if (this.importing) { Text(this.importing).fontColor('#ffd8d0bc').fontSize(15) }
        }.padding(24).backgroundColor('#cc000000').borderRadius(8)
      }
      if (this.request) {
        Column({ space: 12 }) {
          Text(this.request.title || this.t('dmInputTitle')).fontColor('#ffcdb98c').fontSize(18)
          if (this.request.sub) { Text(this.request.sub).fontColor('#ffd8d0bc').fontSize(14) }
          TextInput({ text: this.text, placeholder: this.request.nameRule ? this.t('dmNameBlankRandom') : this.request.guide })
            .maxLength(this.request.maxChars > 0 ? this.request.maxChars : 64)
            .type(this.request.numericMax >= 0 ? InputType.Number : InputType.Normal)
            .onChange((v: string) => { this.text = v; })
            .onSubmit(() => this.finishInput(true))
          Row({ space: 24 }) {
            Button(this.t('dmCancel')).onClick(() => this.finishInput(false))
            Button(this.t('dmConfirm')).onClick(() => this.finishInput(true))
          }
        }.padding(24).backgroundColor('#ee141210').borderRadius(12).width('60%')
      }
    }.width('100%').height('100%').backgroundColor('#000000')
  }
}
