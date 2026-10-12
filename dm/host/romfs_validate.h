#ifndef DM_ROMFS_VALIDATE_H
#define DM_ROMFS_VALIDATE_H
#include <stddef.h>
int dm_romfs_validate(const char *dir, char *why, size_t why_n);   // dir = 直接放着档案文件（NN-XXXXXXXX.data）的那个目录；只核档案文件在不在、字节数对不对 | dir = the directory that directly contains the archive files (NN-XXXXXXXX.data); only checks whether the archive files exist and whether their byte counts are right
int dm_romfs_report(const char *dir, char *out, size_t out_n);   // 给玩家看的完整清单：已有几个 / 共几个、缺哪些（带应有大小）、哪些没拷完；返回缺 / 不全的个数 | The full list shown to the player: how many present / how many in total, which are missing (with expected size), which are incompletely copied; returns the number missing / incomplete
#endif
