#include "dm_touch_game_state.h"
#include "dm_touch_controls.h"
#include "../../host/dm_input.h"
#include <stdio.h>
#include <stdlib.h>
uint64_t dm_tg_portal_mask(const dm_tg_snapshot *,uint64_t);
#ifdef RED_BASELINE
/* v1旧策略的测试替身：valid永久复用，故意不检查时间。 | Test double for the old v1 policy: valid is reused forever, deliberately not checking time. */
uint64_t dm_tg_portal_mask(const dm_tg_snapshot *s,uint64_t n){(void)n;return s->valid?s->portal.buttons:0;}
#endif
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void press(dm_tc_state *c,int n,int t){dm_tc_begin(c,n,c->layout.target[t].x,c->layout.target[t].y); if(t>=DM_TC_MAIN && t<=DM_TC_SKILL4) dm_tc_tick(c,c->last_time_s+0.1);   /* 已过点按 / 拖动的判定时间 | The tap / drag decision time has passed */}
int main(void){
 dm_tg_snapshot s={.supported=1,.valid=1,.ready=1,.captured_ns=1000000000,.portal={.valid=1,.buttons=258}};
 CHECK(dm_tg_portal_mask(&s,1250000001)==0);
 CHECK(dm_tg_portal_mask(&s,1250000000)==258);CHECK(dm_tg_portal_mask(&s,1000000000)==258);
 CHECK(dm_tg_portal_mask(&s,999999999)==0);s.captured_ns=0;CHECK(dm_tg_portal_mask(&s,1)==0);
 s.captured_ns=1000000000;s.valid=0;CHECK(dm_tg_portal_mask(&s,1000000000)==0);
 s.valid=1;s.supported=0;CHECK(dm_tg_portal_mask(&s,1000000000)==0);
 s.supported=1;s.portal.valid=0;CHECK(dm_tg_portal_mask(&s,1000000000)==0);s.portal.valid=1;
 dm_tc_state c;dm_tc_init(&c,667,375);dm_tc_set_portal_button(&c,dm_tg_portal_mask(&s,1000000000));
 press(&c,0,DM_TC_PORTAL);press(&c,1,DM_TC_MAIN);dm_tc_begin(&c,2,90,275);dm_tc_move(&c,2,150,275);int32_t lx=c.lx;
 CHECK(c.buttons==257 && c.portal_pending && lx==32767);
 CHECK(dm_tc_portal_ack(&c,c.gesture_epoch,0,0));CHECK(c.buttons==259 && lx==32767);
 dm_tc_set_portal_button(&c,dm_tg_portal_mask(&s,1250000001));
 CHECK(!c.finger[0].active && c.finger[1].active && c.finger[2].active && c.buttons==1 && c.lx==lx);
 s.portal.buttons=257;dm_tc_set_portal_button(&c,dm_tg_portal_mask(&s,1000000000));
 CHECK(c.buttons==1 && c.lx==lx);press(&c,3,DM_TC_PORTAL);CHECK(c.buttons==257 && c.portal_pending);
 s.portal.buttons=264;dm_tc_set_portal_button(&c,dm_tg_portal_mask(&s,1000000000));CHECK(!c.finger[3].active && c.buttons==1 && c.lx==lx);
 printf("PASS synthetic host-clock freshness+held-map-change %d/%d checks\n",checks,checks);return 0;
}
