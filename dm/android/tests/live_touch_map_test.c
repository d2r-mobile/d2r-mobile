#include "live_touch.h"
#include <assert.h>
#include <stdio.h>

static void event(dm_live_touch *t,int action,int id,float x,float y,int64_t ns){
    int32_t p=id;
    assert(dm_live_touch_event(t,action,0,1,&p,&x,&y,667,375,0,0,667,375,ns));
}
int main(void){
    dm_live_touch *t=dm_live_touch_create(667,375);assert(t);
    dm_live_touch_set_context(t,0,(1u<<DM_TC_COUNT)-1,1,1,1,0);
    float rect[]={.65f,.02f,.9f,.35f},x,y;
    dm_live_touch_set_minimap(t,rect,0);
    event(t,DM_LIVE_ACTION_DOWN,10,460,15,1000000000);
    event(t,DM_LIVE_ACTION_MOVE,10,461,15,1010000000);
    assert(dm_live_touch_active(t));
    event(t,DM_LIVE_ACTION_UP,10,461,15,1100000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==1);
    assert(x>.68f&&x<.70f&&y<.05f);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    assert(!dm_live_touch_active(t));
    event(t,DM_LIVE_ACTION_DOWN,11,460,15,2000000000);
    event(t,DM_LIVE_ACTION_UP,11,500,15,2100000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    event(t,DM_LIVE_ACTION_DOWN,12,460,15,3000000000);
    event(t,DM_LIVE_ACTION_CANCEL,12,460,15,3100000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    dm_live_touch_set_context(t,0,(1u<<DM_TC_COUNT)-1,1,1,1,0);
    dm_live_touch_set_minimap(t,rect,1);
    event(t,DM_LIVE_ACTION_DOWN,13,400,180,4000000000);
    event(t,DM_LIVE_ACTION_UP,13,400,180,4100000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==2);
    // Main attack remains an attack with the full map visible.
    dm_tc_state *s=dm_live_touch_controls(t);
    event(t,DM_LIVE_ACTION_DOWN,14,s->layout.target[0].x,s->layout.target[0].y,5000000000);
    assert(dm_live_touch_buttons(t));
    event(t,DM_LIVE_ACTION_UP,14,s->layout.target[0].x,s->layout.target[0].y,5100000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    event(t,DM_LIVE_ACTION_DOWN,15,460,15,6000000000);
    dm_live_touch_set_context(t,1,0,1,1,2,0);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    dm_live_touch_set_context(t,0,((1u<<DM_TC_COUNT)-1)&~(1u<<DM_TC_MAP),2,2,2,0);
    float edge_rect[]={.65f,0,1,.35f};
    dm_live_touch_set_minimap(t,edge_rect,0);
    dm_tc_state *edge=dm_live_touch_controls(t);
    float mx=edge->layout.target[DM_TC_MAP].x,my=edge->layout.target[DM_TC_MAP].y;
    event(t,DM_LIVE_ACTION_DOWN,16,mx,my,8000000000);
    event(t,DM_LIVE_ACTION_UP,16,mx,my,8100000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==1);
    event(t,DM_LIVE_ACTION_DOWN,17,mx,my,9000000000);
    event(t,DM_LIVE_ACTION_UP,17,mx,my,9700000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==3);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    event(t,DM_LIVE_ACTION_DOWN,18,mx,my,10000000000);
    event(t,DM_LIVE_ACTION_MOVE,18,mx-30,my,10100000000);
    event(t,DM_LIVE_ACTION_MOVE,18,mx,my,10200000000);
    event(t,DM_LIVE_ACTION_UP,18,mx,my,10700000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    event(t,DM_LIVE_ACTION_DOWN,19,mx,my,11000000000);
    event(t,DM_LIVE_ACTION_CANCEL,19,mx,my,11700000000);
    assert(!dm_live_touch_take_map_request(t,&x,&y));
    dm_live_touch_set_context(t,0,((1u<<DM_TC_COUNT)-1)&~(1u<<DM_TC_MAP),2,2,2,0);
    dm_live_touch_set_minimap(t,edge_rect,1);
    event(t,DM_LIVE_ACTION_DOWN,20,400,180,12000000000);
    event(t,DM_LIVE_ACTION_UP,20,400,180,12100000000);
    assert(dm_live_touch_take_map_request(t,&x,&y)==2);
    dm_live_touch_destroy(t);
    puts("PASS map: title tap/move, full-map return, skill priority, cancel and context");
}
