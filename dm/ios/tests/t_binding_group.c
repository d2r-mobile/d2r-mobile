#include "dm_touch_game_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RED_BASELINE
int dm_tg_group_observe(dm_tg_group *g,const dm_tg_snapshot *s,int game,uint64_t scene,uint64_t caller,uint64_t hud,int scope){(void)game;(void)scene;(void)caller;(void)hud;(void)scope;g->completed=*s;g->completed.ready=1;return 1;}
#endif
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static int feed(dm_tg_group *g,dm_tg_snapshot *s,int arg,int game,int scope,uint64_t scene,uint64_t caller,uint64_t hud){s->getter_input_slot=arg;s->observed_index=s->selected_bank+arg;s->captured_ns++;return dm_tg_group_observe(g,s,game,scene,caller,hud,scope);}
int main(void){
 dm_tg_snapshot s={.supported=1,.valid=1,.captured_ns=100,.portal={.valid=1,.buttons=258}};dm_tg_group g={0};
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88));CHECK(!g.completed.ready && g.seen==1);
 for(int i=7;i<11;i++)CHECK(!feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88));
 CHECK(feed(&g,&s,11,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88));CHECK(g.completed.ready && g.completed.secondary_group==1 && g.completed.group_seen==63 && g.completed.group_started_ns==101);
 uint64_t identity=g.completed.binding_identity;CHECK(identity!=0);
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_UNCONFIRMED,1,77,88));CHECK(!g.completed.ready);
 for(int i=0;i<6;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88);CHECK(g.completed.ready && !g.completed.secondary_group);
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88));CHECK(g.completed.ready && !g.completed.secondary_group); // 旧完整normal暂留，不能单alt伪ACK | the old complete normal is kept for now; a lone alt must not fake an ACK
 for(int i=0;i<6;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88);
 CHECK(!g.completed.secondary_group); // normal/alt不混群 | normal/alt are not mixed into one group
 s.selected_bank=16;CHECK(!feed(&g,&s,7,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88));CHECK(!g.completed.ready);
 s.selected_bank=0;for(int i=6;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88);CHECK(g.completed.ready);
 s.raw_skill_id[3]++;CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88));CHECK(!g.completed.ready);
 for(int i=7;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,1,77,88);CHECK(g.completed.ready && g.completed.binding_identity!=identity);
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,2,77,88));CHECK(!g.completed.ready);
 for(int i=7;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,2,77,88);CHECK(g.completed.ready && g.completed.scene_epoch==2);
 CHECK(!feed(&g,&s,6,0,DM_TG_IDENTITY_SYNTHETIC,2,77,88));CHECK(!g.completed.ready);
 for(int i=6;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,2,77,88);CHECK(g.completed.ready);
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,2,99,88));CHECK(!g.completed.ready);
 for(int i=7;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,2,99,88);CHECK(g.completed.ready);
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,2,99,0));CHECK(!g.completed.ready);
 for(int i=6;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,2,99,88);CHECK(g.completed.ready);
 uint64_t world_identity=g.completed.binding_identity;s.world_epoch=1;
 CHECK(!feed(&g,&s,6,1,DM_TG_IDENTITY_SYNTHETIC,2,99,88));CHECK(!g.completed.ready);
 for(int i=7;i<12;i++)feed(&g,&s,i,1,DM_TG_IDENTITY_SYNTHETIC,2,99,88);CHECK(g.completed.ready && g.completed.binding_identity!=world_identity);
 printf("PASS six-facet group+scope/scene/bank/map-ID identity %d/%d checks\n",checks,checks);return 0;
}
