/* 安卓性能对照的Debug专用白名单解析；基于D2固定比例A/B要求，不读取游戏内容。
 * Debug-only Android allowlist parser for D2 fixed-scale A/B tests; reads no game content. */
#pragma once
#ifdef DM_ANDROID_DEBUG_ENV
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

typedef struct { unsigned present; char value[3][16]; } dm_android_debug_values;
static const char *const dm_android_debug_keys[]={"DM_3D_SCALE","DM_3D_SCALE_DYN","DM_3D_SCALE_FLOOR"};

/* 完整解析后才输出；未知键、重复、超限或floor高于起点使整份失效。
 * Publish only a complete parse; unknown keys, duplicates, limits or a floor above the starting scale reject the file. */
static int dm_android_debug_parse(const char *text,size_t bytes,dm_android_debug_values *out) {
    if(!text || !out || bytes>4096)return 0;
    dm_android_debug_values parsed={0};size_t offset=0;
    while(offset<bytes) {
        size_t start=offset;
        while(offset<bytes && text[offset]!='\n')++offset;
        size_t length=offset-start;
        if(offset<bytes)++offset;
        if(length && text[start+length-1]=='\r')--length;
        if(length>127)return 0;
        for(size_t i=0;i<length;++i)if((unsigned char)text[start+i]<32 || (unsigned char)text[start+i]>126)return 0;
        if(!length || text[start]=='#')continue;
        const char *eq=memchr(text+start,'=',length);
        if(!eq)return 0;
        size_t key_length=(size_t)(eq-(text+start)),value_length=length-key_length-1;
        if(!value_length || value_length>=sizeof parsed.value[0])return 0;
        unsigned key;
        for(key=0;key<3;++key)if(strlen(dm_android_debug_keys[key])==key_length && !memcmp(text+start,dm_android_debug_keys[key],key_length))break;
        if(key==3 || (parsed.present&(1u<<key)))return 0;
        memcpy(parsed.value[key],eq+1,value_length); parsed.value[key][value_length]=0;
        if(key==1) { if(strcmp(parsed.value[key],"0") && strcmp(parsed.value[key],"1"))return 0; }
        else {
            int dots=0,digits=0;
            for(size_t i=0;i<value_length;++i) {
                char c=parsed.value[key][i];
                if(c=='.') { if(++dots>1)return 0; }
                else if(c>='0' && c<='9')++digits;
                else return 0;
            }
            if(!digits)return 0;
            double scale=strtod(parsed.value[key],NULL);
            if(!(scale>=.50 && scale<=1.00))return 0;
        }
        parsed.present|=1u<<key;
    }
    double scale=(parsed.present&1)?strtod(parsed.value[0],NULL):.75;
    double floor=(parsed.present&4)?strtod(parsed.value[2],NULL):.60;
    if(floor>scale)return 0;
    *out=parsed;return 1;
}
/* 缺文件返回0，拒绝返回-1，通过返回1；无有效整份结果时不修改输出。
 * Return 0 for absent files, -1 for rejection, 1 for success; never change output without a valid complete result. */
static int dm_android_debug_load(const char *path,dm_android_debug_values *out) {
    FILE *file=fopen(path,"rb");
    if(!file)return errno==ENOENT?0:-1;
    char bytes[4097];size_t count=fread(bytes,1,sizeof bytes,file);
    int failed=ferror(file);fclose(file);
    if(failed || count>4096 || !dm_android_debug_parse(bytes,count,out))return -1;
    return 1;
}
#endif
