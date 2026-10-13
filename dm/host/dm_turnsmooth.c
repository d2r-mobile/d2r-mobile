// 转向回跳平滑（本地玩家）+ 模型转身参数读取。
//
// 原版客户端每个渲染帧把全部单位的路径坐标按「距上一逻辑拍的时间」原地向前外推（主循环 main+0x2c28e8 → 0x461f10），画完帧尾恢复。
// 跑动中急转向时，逻辑在那一拍直接按新方向走，之前沿旧方向多推出去的那段被一帧收回：60 帧下转 90° 那一帧画面倒退约 6 像素、
// 掉头那一帧走平时 2.5 帧的量（镜头直接读这份坐标，所以是整个画面跳一下）。原版自带一套「位置误差平滑」（路径 +0x2a8 / +0x2ac 的浮点误差
// 每帧加回坐标、在走的单位每拍线性收 0.16 格，0x463f40 → 0x5a1810），但只在起步 / 停步时算误差（0x4639c0，排在逻辑拍循环之前），转向接不住。
// 这里在误差平滑函数入口（观测门 OBS_ERR_SMOOTH，main+0x463f40：外推刚做完、误差还没加）给本地玩家补上转向那一帧的误差：
//   误差 +=（旧方向 − 新方向）× 每拍位移 × 上一帧外推的拍比例（正是被收回的那段），每轴夹 ±0.4 格（原版同一个上限），之后由原版逐帧衰减。
// 只在「上一帧与这一帧都在走、方向转过了 TURN_COS 对应的角度」那一帧写一次；转向同时停下的帧不写（原版只在单位在走时衰减，停着写会冻成静止偏移）。
// 逻辑位置、发包、存档都不受影响：改的是外推后的显示坐标上的误差量，帧尾原版照常恢复真值。
// 缺省开；测试开关 DM_TURN_SMOOTH=0 关（改前改后对照用）、=2 挂门只看不写，DM_TEST_TURN_LOG=1 每次补误差 / 停步各记一行，DM_TEST_TURN_TRACE=1 每帧记一行外推坐标与误差（dm/ios/tools/turn_trace.py 出逐帧位移表）。产物没有这个门（r42 之前）就不处理。
//
// 手柄转身手感（本地玩家，看 Mac 上的对比片定的：「去掉并配 360」，真机未看）：
// 3D 模型的朝向由模型朝向组件每帧更新（main+0x9da5b0，观测门 OBS_MODEL_YAW），参数是组件实例里的浮点。原版手柄跑动中掉头（朝向差超过 +0x38 = 100°）
// 会切到一段「原地转身」动画：身体先压低蹬地再转过来，画面约 12 帧（60 帧），这期间逻辑位置已经在往回走——看着像倒着滑一下；
// 转 90° 走限速跟随，转速 = 下限 + 剩余角度 ÷ 180 ×（上限 − 下限），手柄下限 +0x10 / +0x18 = 180 度 / 秒，约 14 帧。
// 这里对本地玩家：手柄门限抬到 YAW_TURN_OFF（不再播那段动画，掉头也走限速跟随、朝向单调转过去），手柄两组下限取 max(原值, YAW_PAD_MIN)。
// 鼠标点地那组（下限 +0x20 / +0x28、门限 +0x3c）不动；别的玩家 / 怪物 / 佣兵不动（只认本地玩家的单位）。纯画面层：这个函数不写路径、不发包。
// 测试开关：DM_TURN_YAW=0 照原版（门限与下限都不改）；DM_TEST_YAW_DUMP=1 打一次原版值并每 5 秒记门的触发频率；
// DM_TEST_YAW_MIN=<度每秒> / DM_TEST_YAW_SET="<偏移十六进制>=<值>[,…]" 试别的值；DM_TEST_YAW_TRACE=1 每帧记一行偏航角。
// Turn snap-back smoothing (local player) + reading the model turning parameters.
// On every rendered frame the original client extrapolates the path coordinates of all units forward in place by "the time since the last logic tick" (main loop main+0x2c28e8 -> 0x461f10) and restores them at the end of the frame.
// On a sharp turn while running, the logic moves along the new direction on that tick, and the part pushed out along the old direction is taken back within one frame: at 60 fps the picture steps back about 6 pixels on the frame of a 90-degree turn,
// and on the frame of a U-turn it moves as much as 2.5 normal frames (the camera reads these coordinates directly, so the whole picture jumps). The original has its own "position error smoothing" (float errors at path +0x2a8 / +0x2ac,
// added back to the coordinates every frame and reduced linearly by 0.16 tiles per tick while the unit is walking, 0x463f40 -> 0x5a1810), but it computes the error only when starting / stopping (0x4639c0, placed before the logic tick loop), so turns are not covered.
// Here, at the entry of the error-smoothing function (observation gate OBS_ERR_SMOOTH, main+0x463f40: extrapolation just done, error not yet added), the error of the turning frame is supplied for the local player:
//   error += (old direction - new direction) x displacement per tick x the tick fraction extrapolated in the previous frame (exactly the part taken back), clamped to +-0.4 tiles per axis (the same limit as the original), and then decayed frame by frame by the original.
// Written once, only on the frame where "the previous frame and this frame are both walking and the direction has turned past the angle of TURN_COS"; frames that turn and stop at the same time are not written (the original decays only while the unit walks, so writing while stopped would freeze into a static offset).
// Logical position, packets and saves are unaffected: what changes is the error on the displayed coordinates after extrapolation, and the original restores the true values at the end of the frame as usual.
// On by default; test switch DM_TURN_SMOOTH=0 turns it off (for before / after comparison), =2 hooks the gate but only observes, DM_TEST_TURN_LOG=1 logs one line per supplied error / per stop, DM_TEST_TURN_TRACE=1 logs one line per frame with the extrapolated coordinates and the error (dm/ios/tools/turn_trace.py prints a per-frame displacement table). Build outputs without this gate (before r42) are left alone.
// Gamepad turning feel (local player; decided from a side-by-side clip on a Mac, not looked at on a device):
// The facing of the 3D model is updated every frame by the model facing component (main+0x9da5b0, observation gate OBS_MODEL_YAW); the parameters are floats in the component instance. In the original, a U-turn while running with a gamepad (facing difference above +0x38 = 100 degrees)
// switches to a "turn in place" animation: the body crouches, pushes off and then turns around, about 12 frames on screen (at 60 fps), while the logical position is already heading back - it looks like sliding backwards for a moment;
// a 90-degree turn uses rate-limited following, turning speed = minimum + remaining angle / 180 x (maximum - minimum), with the gamepad minimum +0x10 / +0x18 = 180 degrees / second, about 14 frames.
// Here, for the local player: the gamepad threshold is raised to YAW_TURN_OFF (that animation no longer plays; a U-turn also uses rate-limited following and the facing turns monotonically), and the two gamepad minimums become max(original, YAW_PAD_MIN).
// The click-to-move group (minimums +0x20 / +0x28, threshold +0x3c) is untouched; other players / monsters / mercenaries are untouched (only the local player's unit is recognised). Purely visual: this function writes no path and sends no packet.
// Test switches: DM_TURN_YAW=0 behaves as the original (neither threshold nor minimum changed); DM_TEST_YAW_DUMP=1 prints the original values once and logs the gate's firing rate every 5 seconds;
// DM_TEST_YAW_MIN=<degrees per second> / DM_TEST_YAW_SET="<offset in hex>=<value>[,...]" try other values; DM_TEST_YAW_TRACE=1 logs one line of yaw per frame.
#include "dm.h"
#include "hle.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TURN_COS 0.94      /* 方向夹角余弦低于它（约 20° 以上）才算急转；每拍转 10° 左右的缓转原本就平滑，不动 | only when the cosine of the angle between directions is below this (about 20 degrees and up) does it count as a sharp turn; gentle turns of about 10 degrees per tick are already smooth and are left alone */
#define YAW_PAD_MIN 360.0f   /* 手柄转速下限（度 / 秒），原版 180 | gamepad minimum turning speed (degrees / second), 180 in the original */
#define YAW_TURN_OFF 999.0f  /* 手柄「原地转身」门限（度），原版 100；大于 180 = 永不触发 | gamepad "turn in place" threshold (degrees), 100 in the original; above 180 = never triggers */
#define ERR_MAX 26214.4f   /* ±0.4 格（16.16 定点单位），与原版 0x5a1710 的夹值相同 | +-0.4 tiles (16.16 fixed-point units), the same clamp as the original at 0x5a1710 */

static uint8_t *g_b; static int g_log, g_dry, g_trace; static float g_yaw_min; static int g_yaw_trace, g_yaw_stat, g_yaw_native; static struct { unsigned off; float v; } g_set[8]; static int g_nset;
static struct { uint64_t path; int ok, moving; int32_t x, y, dx, dy; float tau; double per_tick; } g_p;

static uint64_t local_player(void) { return ((uint64_t (*)(uint32_t))(g_b + 0x45f9d0))(*(uint32_t *)(uintptr_t)(g_b + 0x2d20e4c)); }

static void ts_obs(const uint64_t *regs) {
    float frame = *(const float *)(const void *)&regs[32];   /* s0 = 本帧帧长折成的拍数（不出拍的帧是 0） | s0 = this frame's length expressed in ticks (0 on frames that produce no tick) */
    uint64_t G = *(uint64_t *)(uintptr_t)(g_b + 0x2d20b80); float tau = G ? (float)(*(uint32_t *)(uintptr_t)(G + 8) - *(uint32_t *)(uintptr_t)(G + 0xc)) / 40.0f : 0;   /* 这一帧外推用的拍比例 | the tick fraction used for extrapolation in this frame */
    uint64_t pl = local_player(), pp = pl ? *(uint64_t *)(uintptr_t)(pl + 0x38) : 0;
    if (!G || !pp || !*(uint8_t *)(uintptr_t)(pp + 0x260)) { g_p.ok = 0; return; }
    int32_t x = *(int32_t *)(uintptr_t)pp, y = *(int32_t *)(uintptr_t)(pp + 4), dx, dy; memcpy(&dx, (void *)(uintptr_t)(pp + 0x8e), 4); memcpy(&dy, (void *)(uintptr_t)(pp + 0x92), 4);
    int moving = (*(uint8_t *)(uintptr_t)(pp + 0x48) >> 5) & 1; float *ex = (float *)(uintptr_t)(pp + 0x2a8), *ey = (float *)(uintptr_t)(pp + 0x2ac);
    int same_dir = g_p.ok && g_p.path == pp && dx == g_p.dx && dy == g_p.dy;
    if (g_p.ok && g_p.path == pp && moving && g_p.moving) {
        double n0 = hypot(g_p.dx, g_p.dy), n1 = hypot(dx, dy);
        /* 方向没变的帧：外推坐标这一帧走了「每拍位移 × 帧长」，由此记下每拍位移（速度变了下一帧就跟上） | frames where the direction did not change: the extrapolated coordinates moved "displacement per tick x frame length" this frame, which gives the displacement per tick (a speed change is picked up on the next frame) */
        if (same_dir && frame > 0) g_p.per_tick = hypot((double)x - g_p.x, (double)y - g_p.y) / frame;
        else if (!same_dir && n0 > 0 && n1 > 0 && g_p.per_tick > 0 && ((double)g_p.dx * dx + (double)g_p.dy * dy) / (n0 * n1) < TURN_COS) {
            /* 急转：上一帧沿旧方向外推了 tau 拍，逻辑这一拍改按新方向走——画面上被收回的那段 =（旧方向 − 新方向）× 每拍位移 × 上一帧的 tau | sharp turn: the previous frame extrapolated tau ticks along the old direction, and this tick the logic moves along the new one - the part taken back on screen = (old direction - new direction) x displacement per tick x the previous frame's tau */
            double k = g_p.per_tick * g_p.tau; float ox = *ex, oy = *ey;
            float tx = (float)(ox + (g_p.dx / n0 - dx / n1) * k), ty = (float)(oy + (g_p.dy / n0 - dy / n1) * k);
            tx = fminf(ERR_MAX, fmaxf(-ERR_MAX, tx)); ty = fminf(ERR_MAX, fmaxf(-ERR_MAX, ty)); if (!g_dry) { *ex = tx; *ey = ty; }
            if (g_log) LOG("转向平滑：方向 (%d,%d) → (%d,%d)，误差 (%.0f,%.0f) → (%.0f,%.0f)（单位 1/65536 格），每拍位移 %.0f，上一帧外推 %.2f 拍%s", g_p.dx, g_p.dy, dx, dy, ox, oy, tx, ty, g_p.per_tick, g_p.tau, g_dry ? "（只看不写）" : "");
        }
    }
    if (g_log) { static int cnt = -1;   /* 停步后误差有没有留下静止偏移：停步那一帧与其后第 30 帧各记一行 | whether the error leaves a static offset after stopping: one line at the stopping frame and one at the 30th frame after it */
        if (g_p.ok && g_p.moving && !moving) { cnt = 30; LOG("转向平滑：停步这一帧误差 (%.0f,%.0f)", *ex, *ey); }
        else if (cnt > 0 && --cnt == 0) LOG("转向平滑：停步后第 30 帧误差 (%.0f,%.0f)，在走 %d", *ex, *ey, moving); }
    if (g_trace) LOG("转向帧迹 %d %d %.0f %.0f %d %.3f %.3f", x, y, *ex, *ey, moving, tau, frame);   /* 外推坐标、将加回的误差：画出来的位置 = 两者之和 | extrapolated coordinates and the error about to be added back: the drawn position = the sum of the two */
    g_p.path = pp; g_p.ok = 1; g_p.moving = moving; g_p.x = x; g_p.y = y; g_p.dx = dx; g_p.dy = dy; g_p.tau = tau;
}

static void yaw_obs(const uint64_t *regs) {   /* x1 = 参数块：+8 组件，+0x20 → +8 单位。每帧对每个带朝向组件的单位触发一次 | x1 = parameter block: +8 component, +0x20 -> +8 unit. Fires once per frame for every unit that has a facing component */
    uint64_t a = regs[1], comp = a ? *(uint64_t *)(uintptr_t)(a + 8) : 0, h = a ? *(uint64_t *)(uintptr_t)(a + 0x20) : 0, unit = h ? *(uint64_t *)(uintptr_t)(h + 8) : 0;
    static uint64_t lp_comp; static uint64_t n, t0, shared;
    if (g_yaw_stat) { uint64_t t = guest_ns(); n++; if (!t0) t0 = t; if (comp && comp == lp_comp && unit != local_player()) shared++;   /* 触发频率、别的单位是不是和本地玩家共用同一个组件 | how often it fires, and whether other units share the same component with the local player */
        if (t - t0 >= 5000000000ull) { LOG("模型转身参数：观测门 %d 在 %.1f 秒内触发 %llu 次（%.0f 次 / 秒），别的单位用到本地玩家那个组件 %llu 次", OBS_MODEL_YAW, (t - t0) / 1e9, (unsigned long long)n, n * 1e9 / (double)(t - t0), (unsigned long long)shared); n = 0; shared = 0; t0 = t; } }
    if (!comp || !unit || unit != local_player()) return;
    lp_comp = comp; float *w = (float *)(uintptr_t)comp; static uint64_t dumped;
    if (g_yaw_stat && dumped != comp) { dumped = comp; uint64_t pp = *(uint64_t *)(uintptr_t)(unit + 0x38);   /* 改值之前的原版值，换了组件（换角色 / 重进）再打一次 | the original values before the change; printed again when the component changes (another character / re-entering) */
        LOG("模型转身参数（本地玩家，职业 %u，路径模式 0x%x）：手柄转速（度/秒）+10 %.1f +14 %.1f | +18 %.1f +1c %.1f | 鼠标转速 +20 %.1f +24 %.1f | +28 %.1f +2c %.1f | 弹簧直接到位门限 +30 %.1f | 弹簧参数 +34 %.4f | 原地转身门限 手柄 +38 %.1f 其余 +3c %.1f | +40 %.3f +44 %.3f +48 %.3f +4c %.3f +50 %.3f",
            *(uint32_t *)(uintptr_t)(unit + 4), pp ? *(uint32_t *)(uintptr_t)(pp + 0x50) : 0, w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15], w[16], w[17], w[18], w[19], w[20]); }
    if (!g_yaw_native) {   /* 手感（见文件头）：只动手柄那一组，原值更快的不压低 | feel (see the file header): only the gamepad group is touched, and original values that are already faster are not lowered */
        if (w[14] < 181.0f) w[14] = YAW_TURN_OFF;
        if (w[4] < YAW_PAD_MIN) w[4] = YAW_PAD_MIN;
        if (w[6] < YAW_PAD_MIN) w[6] = YAW_PAD_MIN; }
    if (g_yaw_min > 0) w[4] = w[6] = g_yaw_min;
    for (int i = 0; i < g_nset; i++) *(float *)(uintptr_t)(comp + g_set[i].off) = g_set[i].v;
    if (g_yaw_trace) LOG("转身帧迹 %.1f %.4f %.4f", guest_ns() / 1e6, w[1], w[2]);   /* 客体时钟毫秒、身体偏航、平滑偏航（弧度） | guest clock in milliseconds, body yaw, smoothed yaw (radians) */
}

void dm_turnsmooth_install(uint8_t *main_base) {
    g_b = main_base; const char *e = getenv("DM_TURN_SMOOTH"); g_log = getenv("DM_TEST_TURN_LOG") != NULL; g_trace = getenv("DM_TEST_TURN_TRACE") != NULL;
    if (!gate_obs_present(OBS_ERR_SMOOTH)) LOG("转向平滑：产物没有观测门 %d（r42 起），不处理", OBS_ERR_SMOOTH);
    else if (e && atoi(e) == 0) LOG("转向平滑：测试开关 DM_TURN_SMOOTH=0，不挂门（改前改后对照用）");
    else { g_dry = e && atoi(e) == 2; gate_obs_set(OBS_ERR_SMOOTH, ts_obs); }
    { const char *m = getenv("DM_TEST_YAW_MIN"); g_yaw_min = m ? (float)atof(m) : 0; g_yaw_trace = getenv("DM_TEST_YAW_TRACE") != NULL; g_yaw_stat = getenv("DM_TEST_YAW_DUMP") != NULL;
      const char *nv = getenv("DM_TURN_YAW"); g_yaw_native = nv && atoi(nv) == 0;
      if (g_yaw_native) LOG("模型转身参数：测试开关 DM_TURN_YAW=0，本地玩家的手柄转身照原版（门限 100°、下限 180 度 / 秒）");
      if (g_yaw_min > 0) LOG("模型转身参数：测试开关 DM_TEST_YAW_MIN=%.0f，本地玩家手柄转速下限改成这个值", g_yaw_min); }
    { const char *q = getenv("DM_TEST_YAW_SET"); unsigned off; float v; int k;   /* "38=999,48=0.1"：偏移十六进制（4 的倍数、0x10..0x50）= 浮点值 | "38=999,48=0.1": offset in hex (a multiple of 4, 0x10..0x50) = float value */
      while (q && g_nset < 8 && sscanf(q, "%x=%f%n", &off, &v, &k) == 2) { if (off >= 0x10 && off <= 0x50 && !(off & 3)) { g_set[g_nset].off = off; g_set[g_nset++].v = v; LOG("模型转身参数：测试开关 DM_TEST_YAW_SET，本地玩家组件 +0x%x 逐帧写成 %g", off, v); } q += k; if (*q == ',') q++; else break; } }
    if (!gate_obs_present(OBS_MODEL_YAW)) LOG("模型转身参数：产物没有观测门 %d（r42 起），手柄转身照原版", OBS_MODEL_YAW);
    else if (!g_yaw_native || g_yaw_min > 0 || g_yaw_trace || g_yaw_stat || g_nset) gate_obs_set(OBS_MODEL_YAW, yaw_obs);
}
