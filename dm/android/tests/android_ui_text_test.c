// #308 round89: actual native dm_ui lookup + format path for Android debug
// metrics. The bilingual table is generated from the real Android TSV; the
// old missing-lookup path returned the key, so every value assertion fails.
#include "dm_i18n.h"

#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static int line_count(const char *text) {
    int lines = 0;
    for (; text && *text; ++text) if (*text == '\n') ++lines;
    return lines;
}

static void format_metrics(char *text, size_t capacity) {
    char power[160];
    snprintf(power, sizeof power, dm_ui("dmAndroidPower"),
             "1234 mA | 4.00 V | 4.9 W", 77.0);
    snprintf(text, capacity, dm_ui("dmAndroidDebugMetrics"),
             59.9, 16.7, 4.2, "3.1", 0.5, power,
             120.0, 3.0, 2.0, 1.5, 4.0, 5.5,
             dm_ui("dmAndroidStageGame"), 7, 12, 34, 5678ULL);
}

int main(void) {
    char text[1024], power[160];

    g_dm_lang = 0;
    CHECK(strcmp(dm_ui("dmAndroidDebugMetrics"), "dmAndroidDebugMetrics") != 0);
    CHECK(strcmp(dm_ui("dmAndroidStageGame"), "dmAndroidStageGame") != 0);
    // Explicit group must win over the global language: Chinese while global
    // is English, and English while global is Chinese (below).
    CHECK(strcmp(dm_ui_group("dmAndroidStageGame", DM_LG_EN), "In game") == 0);
    CHECK(strcmp(dm_ui_group("dmAndroidStageGame", DM_LG_ZH), "游戏中") == 0);
    format_metrics(text, sizeof text);
    CHECK(!strstr(text, "dmAndroidDebugMetrics"));
    CHECK(strstr(text, "FPS 59.9") != NULL);
    CHECK(strstr(text, "Frame 16.7 ms") != NULL);
    CHECK(strstr(text, "CPU 4.2 ms") != NULL);
    CHECK(strstr(text, "GPU 3.1 ms") != NULL);
    CHECK(strstr(text, "Wait 0.5 ms") != NULL);
    CHECK(strstr(text, "Draws 120") != NULL);
    CHECK(strstr(text, "Passes 3") != NULL);
    CHECK(strstr(text, "Submits 2.0") != NULL);
    CHECK(strstr(text, "Copies 1.5 MiB/frame") != NULL);
    CHECK(strstr(text, "Buffers 4 MiB") != NULL);
    CHECK(strstr(text, "RSS 6 MiB") != NULL);
    CHECK(strstr(text, "In game") != NULL);
    CHECK(strstr(text, "Area 7") != NULL);
    CHECK(strstr(text, "Position 12,34") != NULL);
    CHECK(strstr(text, "Input 5678") != NULL);
    CHECK(line_count(text) == 5);

    snprintf(power, sizeof power, dm_ui("dmAndroidPower"),
             "1234 mA | 4.00 V | 4.9 W", 77.0);
    CHECK(!strstr(power, "dmAndroidPower"));
    CHECK(strstr(power, "Power 1234 mA") != NULL);
    CHECK(strstr(power, "Battery 77%") != NULL);
    snprintf(power, sizeof power, dm_ui("dmAndroidCharging"),
             "1234 mA | 4.00 V | 4.9 W", 77.0);
    CHECK(strstr(power, "Charging 1234 mA") != NULL);
    CHECK(!strstr(power, "Power 1234"));
    snprintf(power, sizeof power, dm_ui("dmAndroidPower"), "--", 0.0);
    CHECK(strstr(power, "--") != NULL && !strstr(power, "dmAndroidPower"));

    g_dm_lang = 8;
    CHECK(strcmp(dm_ui("dmAndroidStageGame"), "游戏中") == 0);
    CHECK(strcmp(dm_ui_group("dmAndroidStageGame", DM_LG_EN), "In game") == 0);
    CHECK(strcmp(dm_ui_group("dmAndroidStageGame", DM_LG_ZH), "游戏中") == 0);
    format_metrics(text, sizeof text);
    CHECK(!strstr(text, "dmAndroidDebugMetrics"));
    CHECK(strstr(text, "FPS 59.9 | 帧 16.7 ms") != NULL);
    CHECK(strstr(text, "游戏中") != NULL);
    CHECK(strstr(text, "功耗 1234 mA") != NULL);
    CHECK(line_count(text) == 5);
    snprintf(power, sizeof power, dm_ui("dmAndroidCharging"),
             "1234 mA | 4.00 V | 4.9 W", 77.0);
    CHECK(strstr(power, "充电 1234 mA") != NULL);
    CHECK(strcmp(dm_ui("dmAndroidStageText"), "文字输入") == 0);

    if (failures) {
        fprintf(stderr, "android_ui_text: %d checks, %d failures\n", checks, failures);
        return 1;
    }
    printf("PASS: dm_ui Android EN/ZH metrics, power, charging, unavailable and stages formatted from generated TSV\n");
    return 0;
}
