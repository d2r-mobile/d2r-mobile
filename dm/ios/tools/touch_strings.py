#!/usr/bin/env python3
"""全触摸方案：简中字符串里的手柄说法换成触摸说法（所有的手柄按钮操作介绍都应该换成触摸操作介绍；点一下就能完成的提示直接去掉）。
做法 = RomFS 覆盖层换字符串文件（与布局覆盖同一套机制），只改 zhCN 一列、按键名定位、原文不符即报错。
手势口径：单击看详情并比较、双击使用、拖动装备 / 丢弃 / 出售、长按药水进腰带、底栏十字按钮开面板。
Full-touch scheme: in the Simplified Chinese strings, controller wording is replaced with touch wording (every description of a controller button action should become a description of the touch action; hints for things done with a single tap are removed outright).
Approach = replace the string files through the RomFS overlay (the same mechanism as the layout overlay), change only the zhCN column, locate entries by key name, and report an error if the original text does not match.
Gesture conventions: single tap to see details and compare, double tap to use, drag to equip / drop / sell, long press a potion to put it in the belt, the cross button on the bottom bar opens panels."""
import hashlib, json, os, re
import touch_layout

U = ''   # 确认键图标； 比较键； 加号键 | Confirm key icon; compare key; plus key
# 文件 → {键: (原文校验, 新文)}；原文校验 = 游戏现行文字的 SHA-1 前 8 位（表里不存原文）；新文为空串 = 这句不要了
# file → {key: (original text check, new text)}; original text check = first 8 characters of the SHA-1 of the game's current text (the table does not store the original text); an empty new text = this sentence is dropped
TABLE = {
    'ui-controller.json': {
        'UseItemController': ('4c4adb8b', '双击使用'),
        'OpenHoradricCubeController': ('e2d89ec6', '双击打开'),
        'ReadScrollController': ('61e448cc', '双击阅读'),
        'UsePotionOfLifeController': ('2ccab0ae', '双击永久增加20点生命值'),
        'UseBookOfSkillController': ('619bdd63', '双击增加你的技能点'),
        'UseScrollOfResistanceController': ('bc185cbc', '双击施法'),
        'UseTokenOfAbsolutionController': ('9a7778b6', '双击重设属性技能点'),
        'UseTerrorTokenAct1Controller': ('aefa10c5', '双击恐惧化第一幕'),
        'UseTerrorTokenAct2Controller': ('3005cb15', '双击恐惧化第二幕'),
        'UseTerrorTokenAct3Controller': ('45921fbf', '双击恐惧化第三幕'),
        'UseTerrorTokenAct4Controller': ('5ebcc635', '双击恐惧化第四幕'),
        'UseTerrorTokenAct5Controller': ('0bd3c1fd', '双击恐惧化第五幕'),
        'comparisonTooltipController': ('0abebcab', ''),            # 单击物品就出详情并和身上的比较，不用提示 | A single tap on an item shows its details and compares it with what is equipped, so no hint is needed
        'hireComparisonTooltipController': ('2f147b1d', ''),
        'StrYouDiedConsole': ('5336efaf', '你挂了'),              # 下面就有「继续」按钮 | There is a 「继续」(Continue) button right below
        'ControllerInteractButton': ('54234e14', ''),                                    # NPC / 可交互物名字旁的确认键图标（图已透明，占位也去掉） | The confirm key icon next to the names of NPCs / interactable objects (the image is already transparent; the placeholder is removed too)
        'ControllerTitleScreenPrompt': ('d701b1f2', '点击屏幕开始'),
        'EnterCharacterName': ('c516bdc5', ''),
        # 两件可比装备（如两个戒指）之间切换 = 再单击同一件物品（行为归输入层）
        # Switching between two comparable equipped items (e.g. two rings) = single-tap the same item again (the behavior belongs to the input layer)
        'ControllerCycleComparisonTooltips': ('d53c0a25', '再点一次与另一件比较'),
        'SelectButtonNoteText': ('aa4e91fa', ''),   # 触摸下交互 = 点目标、拾取 = 点物品名，与技能键无关 | Under touch, interact = tap the target and pick up = tap the item name, unrelated to the skill buttons
        'MoviePlaybackHoldToSkip_switch': ('f3a05873', ''),                           # 过场不放（宿主 0.1 秒播完），这句不要 | Cutscenes are not played (the host finishes them in 0.1 s), so this sentence is dropped
    },
    'ui.json': {
        # 通知里不写怎么操作（界面更简洁；十字按钮变红已表达）
        # Notifications do not say how to operate (the UI is cleaner; the cross button turning red already conveys it)
        'LevelUpStatsNotificationController': ('7854f598', '你升级了。'),
        'LevelUpNotificationController': ('7ba311c9', '你升级了。'),
        'QuestLogNotificationController': ('7b65d87d', '任务已更新。'),
        'MoviePlaybackHoldToSkip': ('f3a05873', ''),
        # 大字号排版（不是手柄说法）：角色页「技能名 / 命中值」两行标题在 84 高的格子里放不下两行大字——改成单行「技能名命中值」
        # Large font layout (not controller wording): the two-line title 「技能名 / 命中值」(skill name / Attack Rating) on the character page cannot fit two lines of large text in a cell 84 high -- changed to the single line 「技能名命中值」(skill name Attack Rating)
        'strchratr': ('703341e1', '%s命中值'),
        'strchrrat': ('703341e1', '%s命中值'),
    },
}

# 英文列（英文版）：同一批键的 enUS 改成触摸说法；口径同上（空串 = 这句不要）。不在这里的键英文照原版。
# 人物页「%s\n命中值」那两条是中文大字号排版问题，英文不动。别的语言列不改（产品只做中英，别的语言我们自己的字按英文）
# 与中文表同一写法：原文只存校验（SHA-1 前 8 位），表里不存原版文字
# English column (English version): enUS for the same set of keys is changed to touch wording; same rules as above (empty string = this sentence is
# dropped). Keys not listed here keep the original English.
# The two character-sheet entries of the form "%s, newline, Attack Rating" are a layout issue of the large Chinese font and are left alone in English.
# Other language columns are not changed (the product covers only Chinese and English; in other languages our own text uses English)
# Written the same way as the Chinese table: for the source text only a check is stored (first 8 characters of the SHA-1), and the original game's text
# is not stored in the table
EN = {
    'ui-controller.json': {
        'UseItemController': ('3c07d84d', 'Double-tap to Use'),
        'OpenHoradricCubeController': ('3c06b741', 'Double-tap to Open'),
        'ReadScrollController': ('5b76ecf7', 'Double-tap to Read'),
        'UsePotionOfLifeController': ('74604765', 'Double-tap to permanently add 20 to Life'),
        'UseBookOfSkillController': ('3a031cc6', 'Double-tap to learn skill of your choice'),
        'UseScrollOfResistanceController': ('ce192aed', 'Double-tap to Cast'),
        'UseTokenOfAbsolutionController': ('3ba19ef3', 'Double-tap to reset Stat/Skill Points'),
        'UseTerrorTokenAct1Controller': ('73cb07eb', 'Double-tap to terrorize Act 1'),
        'UseTerrorTokenAct2Controller': ('8c394882', 'Double-tap to terrorize Act 2'),
        'UseTerrorTokenAct3Controller': ('558bcf92', 'Double-tap to terrorize Act 3'),
        'UseTerrorTokenAct4Controller': ('07eadb68', 'Double-tap to terrorize Act 4'),
        'UseTerrorTokenAct5Controller': ('9f1ff50e', 'Double-tap to terrorize Act 5'),
        'comparisonTooltipController': ('48cc0c36', ''),
        'hireComparisonTooltipController': ('a97423d0', ''),
        'StrYouDiedConsole': ('e2908c71', 'You have died'),
        'ControllerInteractButton': ('54234e14', ''),
        'ControllerTitleScreenPrompt': ('75c1a3c8', 'Tap the Screen to Begin'),
        'EnterCharacterName': ('8334a399', ''),
        'ControllerCycleComparisonTooltips': ('389c3ec9', 'Tap again to compare with the other'),
        'SelectButtonNoteText': ('e0bafb15', ''),
        'MoviePlaybackHoldToSkip_switch': ('dab56a9e', ''),
    },
    'ui.json': {
        'LevelUpStatsNotificationController': ('525fb31a', "You've leveled up."),
        'LevelUpNotificationController': ('1e4882b2', "You've leveled up."),
        'QuestLogNotificationController': ('94d110a8', 'Quest has been updated.'),
        'MoviePlaybackHoldToSkip': ('dab56a9e', ''),
    },
}

# 手柄支持：手柄说法 ↔ 触摸说法按模式切，不再改字符串文件——build_sidecar 出旁路表，宿主触屏模式把串表里这几个键的串换成触摸说法、
# 手柄模式换回原文（dm/host/dm_padmode.c）。只有下面这两条是排版用的（大字号，两种模式都要），照旧改文件
# Controller support: controller wording ↔ touch wording is switched by mode and the string files are no longer modified. build_sidecar emits a sidecar table;
# in touch mode the host swaps the strings of these keys in the string table to the touch wording,
# and in controller mode swaps the original text back (dm/host/dm_padmode.c). Only the two entries below are for typesetting (large font size, needed in
# both modes) and still modify the files as before
FILE_ONLY = {'strchratr', 'strchrrat'}

def build_sidecar(romfs):
    """[(键, 原文校验, 触摸说法)]：FILE_ONLY 以外的全部条目；原文校验同 TABLE（宿主按它认串表里的原文，对不上不换）。 | [(key, source checksum, touch wording)]: all entries other than FILE_ONLY; the source checksum is the same as in TABLE (the host uses it to identify the original text in the string table and does not swap on a mismatch)."""
    out = []
    for name, items in TABLE.items():
        path, text = touch_layout.read_layout(romfs, None, suffix='/lng/strings/' + name)
        for key, (old, new) in items.items():
            if key in FILE_ONLY: continue
            m = re.search(r'"Key":\s*"%s",.*?"zhCN":\s*("(?:[^"\\]|\\.)*")' % re.escape(key), text, re.S)
            if not m: raise SystemExit('touch_strings：%s 里没有键 %s' % (name, key))
            cur = json.loads(m.group(1))
            if hashlib.sha1(cur.encode('utf-8')).hexdigest()[:8] != old: raise SystemExit('touch_strings：%s 的原文变了（校验 %s，现行「%s」）——原包换了版本，核过再改表' % (key, old, cur))
            out.append((key, old, new))
        # 英文列（英文版）：同一批键再各出一行，原文校验是 enUS 的原文。宿主按「串表里此刻的原文」认校验（dm_padmode.c str_apply），
        # 游戏语言是英文时中文那行对不上不换、英文这行对上才换，反之亦然；不用宿主另跟语言
        # English column (English version): the same set of keys gets one more row each, whose source-text check is against the enUS source text. The host
        # matches the check against "the source text in the string table at this moment" (dm_padmode.c str_apply):
        # when the game language is English the Chinese row does not match and is not applied while the English row matches and is applied, and vice versa; the
        # host does not have to follow the language separately
        for key, (old, new) in EN.get(name, {}).items():
            m = re.search(r'"Key":\s*"%s",.*?"enUS":\s*("(?:[^"\\]|\\.)*")' % re.escape(key), text, re.S)
            if not m: raise SystemExit('touch_strings：%s 里没有键 %s 的 enUS' % (name, key))
            cur = json.loads(m.group(1))
            if hashlib.sha1(cur.encode('utf-8')).hexdigest()[:8] != old: raise SystemExit('touch_strings：%s 的 enUS 原文变了（校验 %s，现行「%s」）——原包换了版本，核过再改表' % (key, old, cur))
            out.append((key, old, new))
    return out

def build(romfs, outdir):
    """返回 ({包内逻辑路径(小写): 本地文件}, 改了几条)。 | Returns ({logical path in package (lowercase): local file}, number of entries changed)."""
    repl = {}; total = 0
    for name, items in TABLE.items():
        path, text = touch_layout.read_layout(romfs, None, suffix='/lng/strings/' + name)
        if not any(k in FILE_ONLY for k in items): continue
        for key, (old, new) in items.items():
            if key not in FILE_ONLY: continue
            m = re.search(r'"Key":\s*"%s",.*?"zhCN":\s*("(?:[^"\\]|\\.)*")' % re.escape(key), text, re.S)
            if not m: raise SystemExit('touch_strings：%s 里没有键 %s' % (name, key))
            cur = json.loads(m.group(1))
            if hashlib.sha1(cur.encode('utf-8')).hexdigest()[:8] != old: raise SystemExit('touch_strings：%s 的原文变了（校验 %s，现行「%s」）——原包换了版本，核过再改表' % (key, old, cur))
            text = text[:m.start(1)] + json.dumps(new, ensure_ascii=False) + text[m.end(1):]; total += 1
        o = os.path.join(outdir, 'str_' + name); open(o, 'w', encoding='utf-8-sig', newline='').write(text); repl[path.lower()] = o
    return repl, total
