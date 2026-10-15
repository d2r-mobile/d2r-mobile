#pragma once
#include <stdint.h>
// 底部 UI v4 的 HUD 数字（宿主每个服务端拍读一次，前端 UIKit 画「Lv · %」、角标、难度数字）与难度档位请求。
// 难度走原版 /players 处理函数（main+0x415db0）同一条链：main+0x3102c0 取人数设置对象 → main+0x1199c50(对象, N)；
// 在下一个服务端拍入口（游戏线程）落地；原版规则对此后新生成的怪生效（实测：8 档 stat 100 = 8、最大生命 floor(×4.5)）。
// HUD numbers for bottom UI v4 (read by the host once per server tick; the front end UIKit draws 'Lv · %', badges and the difficulty number) and difficulty level requests.
// Difficulty goes through the same chain as the original game's /players handler (main+0x415db0): main+0x3102c0 gets the players setting object -> main+0x1199c50(object, N);
// it is applied at the next server tick entry (game thread); the original game's rules apply to monsters spawned from then on (measured: level 8 stat 100 = 8, max life floor(×4.5)).
typedef struct {
    int valid;          // 局内且读到受控玩家 | In a run and the controlled player has been read
    int level;          // stat 12
    int exp_permille;   // 本级经验进度 ‰（原版 experience.txt 门槛） | Experience progress within the current level ‰ (thresholds from the original game's experience.txt)
    int belt[4];        // 腰带四列各剩几件（腰带物品 x % 4 = 列） | How many items remain in each of the four belt columns (belt item x % 4 = column)
    int portal;         // 回城次数：回城书 tbk 的数量（stat 70）+ 回城卷轴 tsc 张数 | Town portal count: quantity in the Tome of Town Portal tbk (stat 70) + number of Scroll of Town Portal tsc
    int players;        // 当前人数档（人数设置对象 +0xc）；0 = 未读到 | Current players setting (players setting object +0xc); 0 = not read
    int hp, hp_max, mp, mp_max;   // 生命 / 法力（stat 6–9 ÷ 256，取整） | Life / mana (stat 6–9 ÷ 256, rounded to an integer)
    int area, x, y;     // 所在区域号（levels.txt Id；0 = 未读到）与玩家坐标（格） | Current area number (levels.txt Id; 0 = not read) and player coordinates (cells)
    int points;         // 未分配属性点 stat 4 + 技能点 stat 5（红方十字亮态） | Unassigned attribute points stat 4 + skill points stat 5 (lit state of the red square cross)
    int holding;        // 手上（光标上）物品的 id，0 = 空（拖放没放成、物品还在手上）：前端据此把「点空白处」当取消。来源是客户端玩家，见 dm_hud.c | Id of the item in hand (on the cursor), 0 = empty (drag-and-drop failed, item still in hand): the front end uses it to treat "tap on empty space" as cancel. The source is the client player, see dm_hud.c
    int quest_unread;   // 有新任务待读：原版 HUD 任务提示亮着（客户端界面状态字节 main+0x2d5ff58）；打开任务面板后清（底栏十字红态的另一个条件） | A new quest is waiting to be read: the original game's HUD quest hint is lit (client UI state byte main+0x2d5ff58); cleared after the quest panel is opened (the other condition for the red state of the bottom bar cross)
    int dead;           // 玩家死亡（模式 0 / 17）：前端隐藏 UIKit 底栏，免挡死亡画面 | Player dead (mode 0 / 17): the front end hides the UIKit bottom bar so it does not block the death screen
} dm_hud_info;
// 玩家「已死」判定 = 游戏自己的定义（main+0x6e5c48 / 0x6e5cc8 客户端命令分派：玩家单位 [+0xc] 模式为 0 或 0x11 即按已死处理；
// D2 玩家模式 0 = DEATH 死亡动画中、17 = DEAD 已死，死亡动画结束时 0 → 17，复活回城后变为城镇站立 5）
// Player 'dead' test = the game's own definition (main+0x6e5c48 / 0x6e5cc8 client command dispatch: a player unit whose [+0xc] mode is 0 or 0x11 is handled as dead;
// D2 player mode 0 = DEATH, in the death animation, 17 = DEAD, already dead; 0 -> 17 when the death animation ends, and it becomes town standing 5 after reviving back in town)
static inline int dm_hud_player_mode_dead(unsigned mode) { return mode == 0 || mode == 17; }
// UIKit 底栏显隐：只在局内常态显示——未读到玩家、死亡、导航态（面板 / 读取画面 / 过场）、被面板栈挡住时都让开
// 五幕城镇的区域号（levels.txt Id）：流亡者营地 1、鲁高因 40、库拉斯特海港 75、群魔堡垒 103、哈洛加斯 109
// UIKit bottom bar visibility: shown only in the normal in-run state -- it gets out of the way when the player has not been read, on death, in navigation states (panel / loading screen / cutscene), and when blocked by the panel stack
// Area numbers of the five act towns (levels.txt Id): Rogue Encampment 1, Lut Gholein 40, Kurast Docks 75, Pandemonium Fortress 103, Harrogath 109
static inline int dm_hud_area_is_town(int area) { return area == 1 || area == 40 || area == 75 || area == 103 || area == 109; }
static inline int dm_hud_bar_visible(const dm_hud_info *h, int navigation, int blocked) { return h->valid && !h->dead && !navigation && !blocked; }
void dm_hud_get(dm_hud_info *out);
uint32_t dm_hud_held(void);
void dm_hud_set_held(uint32_t id);   // 游戏主线程：客户端手上物品 id（0 = 空），dm_hud_get 的 holding 取它 | Game main thread: id of the item in the client's hand (0 = empty); the holding field of dm_hud_get takes it
void dm_hud_request_players(int n);   // 1..8；任意线程调用 | 1..8; callable from any thread
// 宿主内部（diag.c 拍入口）：取最新请求、发布本拍读数
// Host internal (diag.c tick entry): take the latest request, publish this tick's readings
int dm_hud_requested_players(void);
void dm_hud_publish(const dm_hud_info *in);
