// 仅允许已有运行档与真实 HID 返回取样设置，不接受任意环境变量或图形捷径。
// Permit only the existing operation profile and actual HID-return sampling; no arbitrary environment variables or graphics shortcuts.
#define _POSIX_C_SOURCE 200809L
#include "launch_settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { SETTINGS_BYTES = 4096, SETTINGS_LINE_BYTES = 256, SETTINGS_PATH_BYTES = 4096 };

static int fail(char *error, size_t capacity, const char *format, ...) {
    if (error && capacity) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, capacity, format, args);
        va_end(args);
    }
    return 0;
}

static char *trim(char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r') text++;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) text[--length] = 0;
    return text;
}

static int make_path(const char *directory, const char *name, char *path,
                     char *error, size_t capacity) {
    if (!directory || directory[0] != '/') return fail(error, capacity, "launch settings require an absolute application temp directory");
    size_t length = strnlen(directory, SETTINGS_PATH_BYTES);
    if (length == SETTINGS_PATH_BYTES) return fail(error, capacity, "launch settings temp path is too long");
    while (length > 1 && directory[length - 1] == '/') length--;
    const char *separator = length == 1 ? "" : "/";
    int n = snprintf(path, SETTINGS_PATH_BYTES, "%.*s%s%s", (int)length, directory, separator, name);
    if (n < 0 || n >= SETTINGS_PATH_BYTES) return fail(error, capacity, "launch settings path is too long");
    return 1;
}

static int read_settings(const char *path, char *content, int *present,
                         char *error, size_t capacity) {
    // 非阻塞打开后要求普通文件：损坏的 FIFO 配置不能让游戏启动永久等待。
    // Open nonblocking and require a regular file: a malformed FIFO configuration must not stall startup forever.
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) { *present = 0; return 1; }
        return fail(error, capacity, "cannot open launch settings: %s", strerror(errno));
    }
    struct stat status;
    if (fstat(fd, &status)) { int saved = errno; close(fd); return fail(error, capacity, "cannot inspect launch settings: %s", strerror(saved)); }
    if (!S_ISREG(status.st_mode)) { close(fd); return fail(error, capacity, "launch settings must be a regular file"); }
    if (status.st_size > SETTINGS_BYTES) { close(fd); return fail(error, capacity, "launch settings exceed %d bytes", SETTINGS_BYTES); }
    FILE *file = fdopen(fd, "rb");
    if (!file) { int saved = errno; close(fd); return fail(error, capacity, "cannot read launch settings: %s", strerror(saved)); }
    size_t length = fread(content, 1, SETTINGS_BYTES, file);
    int extra = fgetc(file), saved = errno, read_error = ferror(file);
    int close_error = fclose(file);
    if (read_error) return fail(error, capacity, "launch settings read failed: %s", strerror(saved));
    if (close_error) return fail(error, capacity, "launch settings close failed: %s", strerror(errno));
    // 即使读取时文件增长也守住总长度；NUL 不能截断后续非法配置。
    // Enforce the total limit even if the file grows during reading; NUL must not hide later invalid configuration.
    if (extra != EOF) return fail(error, capacity, "launch settings exceed %d bytes", SETTINGS_BYTES);
    if (memchr(content, 0, length)) return fail(error, capacity, "launch settings contain a NUL byte");
    content[length] = 0;
    *present = 1;
    return 1;
}

static int parse_settings(char *content, dm_android_launch_settings *settings,
                          char *error, size_t capacity) {
    unsigned line_number = 0, seen = 0;
    char *line = content;
    do {
        line_number++;
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        size_t length = strlen(line);
        if (length && line[length - 1] == '\r') length--;
        if (length > SETTINGS_LINE_BYTES) return fail(error, capacity, "launch settings line %u exceeds %d bytes", line_number, SETTINGS_LINE_BYTES);
        char *key = trim(line);
        if (*key && *key != '#') {
            char *value = strchr(key, '=');
            if (!value) return fail(error, capacity, "launch settings line %u requires key=value", line_number);
            *value++ = 0;
            key = trim(key); value = trim(value);
            unsigned bit;
            if (!strcmp(key, "operation_mode")) bit = 1;
            else if (!strcmp(key, "input_receipts")) bit = 2;
            else if (!strcmp(key, "debug_overlay")) bit = 4;
            else if (!strcmp(key, "resolution")) bit = 8;
            else return fail(error, capacity, "unknown launch settings key on line %u: %s", line_number, key);
            if (seen & bit) return fail(error, capacity, "duplicate launch settings key on line %u: %s", line_number, key);
            if (bit == 1) {
                if (!strcmp(value, "handheld")) settings->operation_mode = 0;
                else if (!strcmp(value, "console")) settings->operation_mode = 1;
                else return fail(error, capacity, "launch settings line %u: operation_mode must be handheld or console", line_number);
            } else if (bit == 8) {
                // 只接受 宽x高；范围限制是为了挡住明显会撑爆渲染目标的输入，而不是替用户猜意图。
                // Only <width>x<height>; the range check rejects values that would blow up the render target rather
                // than guessing what the user meant.
                unsigned width = 0, height = 0; char tail = 0;
                if (sscanf(value, "%ux%u%c", &width, &height, &tail) != 2 ||
                    width < 320 || width > 7680 || height < 240 || height > 4320)
                    return fail(error, capacity, "launch settings line %u: resolution must be <width>x<height> with width 320-7680 and height 240-4320", line_number);
                settings->resolution_width = (int)width; settings->resolution_height = (int)height;
            } else if (bit == 4) {
                if (!strcmp(value, "0")) settings->debug_overlay = 0;
                else if (!strcmp(value, "1")) settings->debug_overlay = 1;
                else return fail(error, capacity, "launch settings line %u: debug_overlay must be 0 or 1", line_number);
            } else {
                if (!strcmp(value, "0")) settings->input_receipts = 0;
                else if (!strcmp(value, "1")) settings->input_receipts = 1;
                else return fail(error, capacity, "launch settings line %u: input_receipts must be 0 or 1", line_number);
            }
            seen |= bit;
        }
        line = next;
    } while (line);
    return 1;
}

typedef struct environment_change {
    const char *name, *value;
    char *previous;
    int had_previous;
} environment_change;

static int set_variable(const environment_change *change, int restore) {
    const char *value = restore ? (change->had_previous ? change->previous : NULL) : change->value;
    return value ? setenv(change->name, value, 1) : unsetenv(change->name);
}

static int apply_environment(environment_change *changes, size_t count, char *error, size_t capacity) {
    // 先保存所有受影响的旧值；分配或校验失败时尚未改任何变量。
    // Save all affected old values first; allocation/validation failures have not changed any variables yet.
    size_t captured = 0;
    for (; captured < count; captured++) {
        const char *previous = getenv(changes[captured].name);
        changes[captured].had_previous = previous != NULL;
        if (previous && !(changes[captured].previous = strdup(previous))) {
            int saved = errno;
            for (size_t i = 0; i < captured; i++) free(changes[i].previous);
            return fail(error, capacity, "cannot save launch settings environment: %s", strerror(saved));
        }
    }
    size_t applied = 0;
    while (applied < count && !set_variable(&changes[applied], 0)) applied++;
    int ok = applied == count;
    if (!ok) {
        int saved = errno, rollback_error = 0;
        // libc 没有跨变量事务；第二次写失败时恢复已应用项。回滚失败必须响亮终止启动。
        // libc has no multi-variable transaction; restore applied entries if the second write fails. A rollback failure must loudly stop startup.
        for (size_t i = applied; i > 0; i--) if (set_variable(&changes[i - 1], 1)) rollback_error = errno;
        if (rollback_error) fail(error, capacity, "cannot apply launch settings: %s; environment rollback also failed: %s; abort startup", strerror(saved), strerror(rollback_error));
        else fail(error, capacity, "cannot apply launch settings: %s; previous environment restored", strerror(saved));
    }
    for (size_t i = 0; i < count; i++) free(changes[i].previous);
    return ok;
}

int dm_android_launch_settings_apply(const char *temporary_directory,
                                     dm_android_launch_settings *result,
                                     char *error, size_t error_capacity) {
    if (error && error_capacity) error[0] = 0;
    char path[SETTINGS_PATH_BYTES], content[SETTINGS_BYTES + 1];
    dm_android_launch_settings settings = {0, -1, -1, -1, -1, -1};
    if (!make_path(temporary_directory, "launch-settings.conf", path, error, error_capacity)) return 0;
    if (!read_settings(path, content, &settings.present, error, error_capacity)) return 0;
    if (!settings.present) { if (result) *result = settings; return 1; }
    if (!parse_settings(content, &settings, error, error_capacity)) return 0;
    // 所有内容与需要的固定路径都验证完才进入环境写入；省略的键不清空其他来源设置。
    // Validate all content and required fixed paths before writing the environment; omitted keys do not clear settings supplied elsewhere.
    if (settings.input_receipts == 1 && !make_path(temporary_directory, "input-receipts.jsonl", path, error, error_capacity)) return 0;
    environment_change changes[4] = {{0}};
    size_t count = 0;
    if (settings.operation_mode != -1) changes[count++] = (environment_change){.name = "DM_OPMODE", .value = settings.operation_mode ? "1" : "0"};
    if (settings.input_receipts != -1) changes[count++] = (environment_change){.name = "DM_HID_RECEIPT_LOG", .value = settings.input_receipts ? path : NULL};
    // 调试浮层开关：显式写 "0" 而不是清空变量，因为“关掉”本身就是要表达的设置。
    // Debug overlay toggle: write "0" explicitly rather than unsetting, because "hidden" is itself the setting.
    if (settings.debug_overlay != -1) changes[count++] = (environment_change){.name = "DM_DEBUG_OVERLAY", .value = settings.debug_overlay ? "1" : "0"};
    // 分辨率：写成 DM_GAME_RES=宽x高，由 front_game_res 读（与 Mac/iOS 同一变量同一语义）。
    // Resolution: written as DM_GAME_RES=<w>x<h>, read by front_game_res, the same variable and semantics as Mac and iOS.
    char resolution[16];
    if (settings.resolution_width > 0 && settings.resolution_height > 0) {
        int n = snprintf(resolution, sizeof resolution, "%dx%d", settings.resolution_width, settings.resolution_height);
        if (n > 0 && (size_t)n < sizeof resolution) changes[count++] = (environment_change){.name = "DM_GAME_RES", .value = resolution};
    }
    if (!apply_environment(changes, count, error, error_capacity)) return 0;
    if (result) *result = settings;
    return 1;
}
