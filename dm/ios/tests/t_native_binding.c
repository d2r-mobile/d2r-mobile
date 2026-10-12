#include "dm_touch_game_state.h"
#include "dm_touch_controls.h"
#include "../../host/dm_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x); exit(1); } } while(0)
#ifdef RED_BASELINE
/* 测试专用缺失功能基线，不进入Sources。 | Test-only missing-feature baseline, does not go into Sources. */
int dm_tg_parse(dm_tg_snapshot *o,const void *m,size_t ml,const void *b,size_t bl,int k,int input,int i,uint32_t id,uint64_t n) {(void)m;(void)ml;(void)b;(void)bl;(void)k;(void)input;(void)i;(void)id;(void)n;memset(o,0,sizeof(*o));return 0;}
void dm_tc_set_portal_button(dm_tc_state *s,uint64_t b) {(void)s;(void)b;}
#endif
void dm_tc_set_portal_button(dm_tc_state *,uint64_t);
static void put(unsigned char *p,uint32_t v) { for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8)); }
static void press(dm_tc_state *s,int n,int t) { dm_tc_begin(s,n,s->layout.target[t].x,s->layout.target[t].y); }
int main(int argc,char **argv) {
    CHECK(argc==3); unsigned char map[114],table[896];
    FILE *f=fopen(argv[1],"rb"); CHECK(f!=NULL);CHECK(fread(map,1,sizeof map,f)==sizeof map);fclose(f);
    f=fopen(argv[2],"rb");CHECK(f!=NULL);CHECK(fread(table,1,sizeof table,f)==sizeof table);fclose(f);
    dm_tg_snapshot s;
    CHECK(dm_tg_parse(&s,map,sizeof map,table,12*28,0,(7-0),7,359,99));
    CHECK(s.valid && s.source==DM_TG_SOURCE_NATIVE_GETTER && s.selected_bank==0 && s.captured_ns==99);
    CHECK(s.mapping[0].logical==0x1000 && s.mapping[0].normal==1 && s.mapping[0].alt==7);
    CHECK(s.portal.valid && s.portal.slot==7 && s.portal.raw_skill_id==359 && s.portal.buttons==(DM_BTN_ZL|DM_BTN_B));
    unsigned char changed[896]; memcpy(changed,table,sizeof changed);
    put(changed+7*28,UINT32_MAX);put(changed+6*28,220);
    CHECK(dm_tg_parse(&s,map,72,changed,336,0,(6-0),6,220,100));CHECK(s.portal.buttons==(DM_BTN_ZL|DM_BTN_A));
    put(changed+6*28,UINT32_MAX);put(changed+5*28,219);
    CHECK(dm_tg_parse(&s,map,72,changed,336,0,(5-0),5,219,101));CHECK(s.portal.buttons==DM_BTN_ZR);
    put(changed+5*28,UINT32_MAX);
    CHECK(dm_tg_parse(&s,map,72,changed,336,0,(0-0),0,0,102));CHECK(s.valid && !s.portal.valid && s.portal.buttons==0);
    CHECK(dm_tg_parse(&s,map,72,table+16*28,336,16,(16-16),16,UINT32_MAX,103));CHECK(s.selected_bank==16 && !s.portal.valid);
    CHECK(!dm_tg_parse(&s,map,71,table,336,0,(7-0),7,359,1));CHECK(!s.valid && !s.portal.buttons);
    CHECK(!dm_tg_parse(&s,map,72,table,335,0,(7-0),7,359,1));
    CHECK(!dm_tg_parse(&s,map,72,table,336,0,(12-0),12,359,1));
    CHECK(!dm_tg_parse(&s,map,72,table,336,16,(7-16),7,359,1));
    CHECK(!dm_tg_parse(&s,map,72,table,336,0,(7-0),7,220,1));
    unsigned char badmap[72];memcpy(badmap,map,72);put(badmap+12,0x1000);
    CHECK(!dm_tg_parse(&s,badmap,72,table,336,0,(7-0),7,359,1));
    memcpy(badmap,map,72);put(badmap+16,1);CHECK(!dm_tg_parse(&s,badmap,72,table,336,0,(7-0),7,359,1));
    memcpy(badmap,map,72);put(badmap+20,7);CHECK(!dm_tg_parse(&s,badmap,72,table,336,0,(7-0),7,359,1));
    memcpy(badmap,map,72);put(badmap,0xDEAD);CHECK(!dm_tg_parse(&s,badmap,72,table,336,0,(7-0),7,359,1));
    const uint64_t facets[6]={DM_BTN_B,DM_BTN_A,DM_BTN_Y,DM_BTN_X,DM_BTN_R,DM_BTN_ZR};
    for(int alt=0;alt<2;alt++)for(int facet=0;facet<6;facet++) {
        memset(changed,255,sizeof changed);int slot=map[facet*12+(alt?8:4)];put(changed+slot*28,359);
        CHECK(dm_tg_parse(&s,map,72,changed,336,0,(slot-0),slot,359,104));
        CHECK(s.portal.buttons==(facets[facet]|(alt?DM_BTN_ZL:0)) && s.portal.slot==slot);
    }
    memset(changed,255,sizeof changed);put(changed,219);put(changed+7*28,359);
    CHECK(dm_tg_parse(&s,map,72,changed,336,0,(0-0),0,219,105));CHECK(s.portal.slot==7);
    put(changed+7*28,360);put(changed,UINT32_MAX);
    CHECK(dm_tg_parse(&s,map,72,changed,336,0,(7-0),7,360,106));CHECK(!s.portal.valid && !s.portal.buttons);
    dm_tc_state c;dm_tc_init(&c,667,375);dm_tc_set_portal_button(&c,DM_BTN_ZL|DM_BTN_B);
    press(&c,0,DM_TC_PORTAL);press(&c,1,DM_TC_MAIN);dm_tc_begin(&c,2,100,200);dm_tc_move(&c,2,150,200);
    CHECK(c.buttons==(DM_BTN_ZL|DM_BTN_A) && c.portal_pending);
    CHECK(dm_tc_portal_ack(&c,c.gesture_epoch,0,0));
    CHECK(c.buttons==(DM_BTN_ZL|DM_BTN_B|DM_BTN_A));int32_t lx=c.lx;
    dm_tc_set_portal_button(&c,DM_BTN_ZL|DM_BTN_B);CHECK(c.finger[0].active);
    dm_tc_set_portal_button(&c,DM_BTN_ZL|DM_BTN_A);
    CHECK(!c.finger[0].active && c.finger[1].active && c.finger[2].active && c.lx==lx && c.buttons==DM_BTN_A);
    dm_tc_resize(&c,812,375);CHECK(c.layout.target[DM_TC_PORTAL].button==(DM_BTN_ZL|DM_BTN_A));
    dm_tc_set_portal_button(&c,0);press(&c,0,DM_TC_PORTAL);CHECK(c.buttons==0 && c.joy_slot==-1);
    printf("PASS native binding fixture+negative+ownership %d/%d checks\n",checks,checks);return 0;
}
