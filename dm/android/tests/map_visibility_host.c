#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include "../../host/dm_input.h"

typedef struct { unsigned char *base; } module_t;
static uint64_t panel;
static int ingame=1, pad, busy;
static uint64_t ui_panel(uint64_t root,const char *name){(void)root;assert(!strcmp(name,"AutoMap"));return panel;}
int dm_uiscreen_ingame(void){return ingame;}
int dm_padmode_current(void){return pad;}
int dm_padmode_busy(void){return busy;}
void dm_ui_key_pulse(uint64_t mask,int frames){assert(mask==DM_BTN_MINUS&&frames==9);*((uint8_t *)(uintptr_t)panel+0x50)^=1;}
#include "map_visibility_actual.inc"

int main(void){
    _Alignas(8) unsigned char first[0x15c4]={0}, second[0x15c4]={0};
    module_t m={.base=(unsigned char *)(uintptr_t)0x10000000};
    uint64_t vt=(uint64_t)m.base+0x20c58a0;
    float defaults[]={1,.75f,.5f,.25f};
    memcpy(first,&vt,8);memcpy(second,&vt,8);
    memcpy(first+0x15b4,defaults,sizeof defaults);memcpy(second+0x15b4,defaults,sizeof defaults);
    first[0x50]=second[0x50]=1;panel=(uint64_t)(uintptr_t)first;
    dm_ui_map_hidden(1);ui_map_visibility_pump(&m,1);
    assert(dm_ui_map_is_hidden()&&first[0x50]==0);
    assert(!memcmp(first+0x15b4,defaults,sizeof defaults));
    ui_map_visibility_pump(&m,1);
    panel=0;dm_ui_map_hidden(0);ui_map_visibility_pump(&m,1);
    panel=(uint64_t)(uintptr_t)first;
    dm_ui_map_hidden(0);ui_map_visibility_pump(&m,1);
    assert(!dm_ui_map_is_hidden()&&first[0x50]==1);
    dm_ui_map_hidden(1);ui_map_visibility_pump(&m,1);
    panel=(uint64_t)(uintptr_t)second;ui_map_visibility_pump(&m,1);
    assert(second[0x50]==0);
    pad=1;ui_map_visibility_pump(&m,1);
    assert(!dm_ui_map_is_hidden()&&second[0x50]==1);
    pad=0;dm_ui_map_hidden(1);ui_map_visibility_pump(&m,1);
    busy=1;ui_map_visibility_pump(&m,1);
    assert(!dm_ui_map_is_hidden()&&second[0x50]==1);
    busy=0;dm_ui_map_hidden(1);ui_map_visibility_pump(&m,1);
    ingame=0;ui_map_visibility_pump(&m,1);
    assert(!dm_ui_map_is_hidden()&&second[0x50]==0);
    ingame=1;second[0x50]=1;memset(second,0,8);dm_ui_map_hidden(1);ui_map_visibility_pump(&m,1);
    assert(second[0x50]==1&&!memcmp(second+0x15b4,defaults,sizeof defaults));
    panel=0;ui_map_visibility_pump(&m,1);
    puts("PASS map visibility: restore, replacement, class guard, game/controller lifecycle");
}
