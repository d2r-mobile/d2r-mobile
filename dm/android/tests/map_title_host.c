#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../host/dm_widget.h"
typedef struct {unsigned char *base;} module_t;
static _Alignas(8) unsigned char label[0x98];
static int hidden=1;
int dm_ui_map_is_hidden(void){return hidden;}
static uint64_t ui_panel(uint64_t root,const char *name){(void)root;assert(!strcmp(name,"HUDLevelNames"));return 1;}
static uint64_t ui_find(uint64_t root,const char *name,int depth){assert(root==1&&depth==0&&!strcmp(name,"NameText"));return (uint64_t)(uintptr_t)label;}
#include "map_title_actual.inc"
int main(void){
    module_t m={.base=(unsigned char *)(uintptr_t)0x10000000};
    uint64_t vt=(uint64_t)m.base+DM_VT_TEXT;memcpy(label,&vt,8);
    const char *name="Rogue Encampment";uint64_t length=strlen(name);
    memcpy(label+0x88,&name,8);memcpy(label+0x90,&length,8);
    char out[128];ui_map_title_pump(&m,1);dm_ui_map_title(out);
    assert(!strcmp(out,name)&&dm_ui_map_title_revision()==1);
    ui_map_title_pump(&m,1);assert(dm_ui_map_title_revision()==1);
    name="Cold Plains";length=strlen(name);memcpy(label+0x88,&name,8);memcpy(label+0x90,&length,8);
    ui_map_title_pump(&m,1);dm_ui_map_title(out);assert(!strcmp(out,name)&&dm_ui_map_title_revision()==2);
    memset(label,0,8);ui_map_title_pump(&m,1);assert(dm_ui_map_title_revision()==2);
    memcpy(label,&vt,8);length=128;memcpy(label+0x90,&length,8);
    ui_map_title_pump(&m,1);assert(dm_ui_map_title_revision()==2);
    puts("PASS map title: original localized text snapshot, stable/change revision, class and length guards");
}
