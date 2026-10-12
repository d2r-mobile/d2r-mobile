#include "dm_touch_controls.h"
#include "../../host/dm_input.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef RED_BASELINE
/* v2测试替身：旧直接组合与缺失ACK。 | v2 test double: old direct combination and missing ACK. */
static int dm_tc_portal_ack(dm_tc_state *s,uint64_t e,uint64_t b,uint64_t c){(void)s;(void)e;(void)b;(void)c;return 0;}
static void dm_tc_set_portal_binding(dm_tc_state *s,uint64_t mask,uint64_t b,uint64_t c){(void)b;(void)c;dm_tc_set_portal_button(s,mask);}
#define EPOCH(s) 0
#else
#define EPOCH(s) ((s).gesture_epoch)
#endif
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void press(dm_tc_state *s,int n,int target){dm_tc_begin(s,n,s->layout.target[target].x,s->layout.target[target].y); if(target>=DM_TC_MAIN && target<=DM_TC_SKILL4) dm_tc_tick(s,s->last_time_s+0.1);   /* 主钮 / 技能钮按下后有判定时间（点按还是拖动），这里的「按下」= 已过判定时间 | After the main button / a skill button is pressed there is a decision time (tap or drag); here "pressed" = the decision time has passed */}
int main(void){
 dm_tc_state s;dm_tc_init(&s,667,375);dm_tc_set_portal_binding(&s,258,77,3);press(&s,0,DM_TC_PORTAL);
 CHECK(s.buttons==256);uint64_t epoch=EPOCH(s);
 press(&s,1,DM_TC_MAIN);dm_tc_begin(&s,2,90,275);dm_tc_move(&s,2,150,275);CHECK(s.buttons==257 && s.lx==32767);
 CHECK(!dm_tc_portal_ack(&s,epoch+1,77,3));CHECK(!dm_tc_portal_ack(&s,epoch,78,3));CHECK(!dm_tc_portal_ack(&s,epoch,77,4));CHECK(s.buttons==257);
 press(&s,3,DM_TC_PORTAL);CHECK(EPOCH(s)==epoch && s.buttons==257);
 CHECK(dm_tc_portal_ack(&s,epoch,77,3));CHECK(s.buttons==259 && s.lx==32767);CHECK(!dm_tc_portal_ack(&s,epoch,77,3));
 press(&s,4,DM_TC_PORTAL);CHECK(EPOCH(s)==epoch && s.buttons==259);
 dm_tc_end(&s,0);CHECK(s.buttons==259 && s.lx==32767 && s.finger[3].active && s.finger[4].active && EPOCH(s)==epoch);
 dm_tc_end(&s,3);CHECK(s.buttons==259 && EPOCH(s)==epoch);dm_tc_end(&s,4);CHECK(s.buttons==1 && s.lx==32767);CHECK(!dm_tc_portal_ack(&s,epoch,77,3));
 press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);CHECK(s.buttons==257);dm_tc_set_portal_binding(&s,258,78,3);CHECK(s.buttons==1 && s.lx==32767);CHECK(!dm_tc_portal_ack(&s,epoch,77,3));
 press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);dm_tc_set_portal_binding(&s,258,78,4);CHECK(s.buttons==1);CHECK(!dm_tc_portal_ack(&s,epoch,78,3));
 press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);dm_tc_set_portal_button(&s,0);CHECK(s.buttons==1 && s.lx==32767);CHECK(!dm_tc_portal_ack(&s,epoch,78,4));
 dm_tc_set_portal_binding(&s,258,79,4);press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);dm_tc_end(&s,0);CHECK(s.buttons==1);CHECK(!dm_tc_portal_ack(&s,epoch,79,4));
 press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);dm_tc_cancel_all(&s);CHECK(s.buttons==0 && s.lx==0);CHECK(!dm_tc_portal_ack(&s,epoch,79,4));
 press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);dm_tc_resize(&s,812,375);CHECK(s.buttons==0);CHECK(!dm_tc_portal_ack(&s,epoch,79,4));
 dm_tc_set_portal_binding(&s,512,80,5);press(&s,0,DM_TC_PORTAL);CHECK(s.buttons==512);dm_tc_end(&s,0);CHECK(s.buttons==0);
 dm_tc_set_portal_binding(&s,258,81,6);press(&s,0,DM_TC_PORTAL);epoch=EPOCH(s);press(&s,1,DM_TC_PORTAL);
 CHECK(s.buttons==256 && EPOCH(s)==epoch);dm_tc_end(&s,0);CHECK(s.buttons==256 && EPOCH(s)==epoch);
 CHECK(dm_tc_portal_ack(&s,epoch,81,6));CHECK(s.buttons==258);dm_tc_end(&s,1);CHECK(s.buttons==0);CHECK(!dm_tc_portal_ack(&s,epoch,81,6));
 printf("PASS portal two-stage+epoch+end/cancel/identity/scene %d/%d checks\n",checks,checks);return 0;
}
