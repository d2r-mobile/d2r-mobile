// Compiles the actual NativeActivity frontend hooks against owned host boundaries.
#include "live_touch.h"
#include "live_touch_items.h"
#include "live_touch_provider.h"
#include "dm_cursor.h"
#include "dm_actions.h"
#include "dm_world.h"
#include "dm_input.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
typedef struct {
    dm_live_touch *live_touch;
    dm_live_touch_items *live_items;
    int live_provider_available;
    dm_live_touch_scene_observation live_observation;
    dm_live_touch_geometry live_geometry;
    unsigned map_world_seq,tap_move_seen,world_tap_seen;
    uint32_t selected_point;
    double add_point_at,stat_next,tip_until,labels_at;
    int tip_pointer_was,tip_dragged,ui_drag_active;
    dm_live_touch_item_snapshot tip_last;
    uint64_t frontend_scene,frontend_world,frontend_ui;
    int fe_pointer_active,fe_pointer_id,fe_drag;
    float fe_origin_x,fe_origin_y;
} game_app;
static uint64_t clock_ns;
static uint64_t now_ns(void){return clock_ns;}
static int hit,bind,ready=1,stat_adds,skill_adds,full,map_calls,world_result,ingame;
static int drag_begins,drag_moves,drag_ends,last_drag,touches,fe_ok=1;
static int world_taps;
void dm_ui_world_tap(float ux,float uy,float wx,float wy){assert(ux==wx&&uy==wy);assert(wx>=0&&wx<=1&&wy>=0&&wy<=1);world_taps++;}
static uint32_t hit_id;
static dm_points_info points;
int dm_live_touch_items_pointer_ready(const dm_live_touch_items *i){(void)i;return ready;}
unsigned dm_world_tap(float x,float y){assert(x>=0&&x<=1&&y>=0&&y<=1);return 1;}
int dm_world_tap_result(unsigned s){(void)s;return world_result;}
void dm_ui_map_full(int on){full=on;map_calls++;}
int dm_ui_map_is_full(void){return full;}
int dm_uiscreen_ingame(void){return ingame;}
void dm_ui_drag_lift(float y){assert(y>=0&&y<1);}
void dm_ui_drag_begin(int kind,float x,float y,float tol){(void)x;(void)y;(void)tol;drag_begins++;last_drag=kind;}
void dm_ui_drag_move(float x,float y){(void)x;(void)y;drag_moves++;}
void dm_ui_drag_end(void){drag_ends++;}
int dm_ui_bind_active(void){return bind;}
int dm_ui_bind_skill_hit(float *x,float *y){*x=.4f;*y=.3f;return hit;}
uint32_t dm_ui_hit_id(void){return hit_id;}
void dm_stat_add(int s,int n){assert(s>=0&&s<4&&n==1);stat_adds++;}
void dm_points_get(dm_points_info *p){*p=points;}
void dm_skill_add_selected(void){skill_adds++;}
int dm_cursor_pending(void){return 0;}
int dm_padmode_current(void){return 0;}
int dm_ui_free_cursor(void){return 0;}
int dm_ui_touch_active(void){return 0;}
int dm_ui_fe_drag_ok(void){return fe_ok;}
void dm_ui_touch(int phase,float x,float y){(void)phase;(void)x;(void)y;touches++;}
void dm_ui_key_pulse_alone(uint64_t button,int frames){assert(button==DM_BTN_L&&frames==9);}
static _Atomic uint64_t ghost_contact,ghost_contact_seq;
static _Atomic int32_t ghost_pointer_id=-1;
int dm_ui_shelf_grid(float grid[4]){(void)grid;return 0;}
int dm_ui_stack_left(void){return 0;}
void mtl_ui_ghost(int on,float px,float py,const float grid[4],float cx,float cy){(void)on;(void)px;(void)py;(void)grid;(void)cx;(void)cy;}
void dm_vk_ui_ghost_request(int on,float px,float py,const float grid[4],float cx,float cy,int dim,const _Atomic uint64_t *live,uint64_t token){(void)on;(void)px;(void)py;(void)grid;(void)cx;(void)cy;(void)dim;(void)live;(void)token;}
#include "frontend_actual.inc"
static void tick(game_app *a,uint64_t ns){clock_ns=ns;live_touch_frontend_tick(a);}
static void event(game_app *a,int action,float x,float y,uint64_t ns){
    int32_t id=1;
    if(action==DM_LIVE_ACTION_UP||action==DM_LIVE_ACTION_CANCEL)
        shop_ghost_contact_event(a,action,id);
    assert(dm_live_touch_event(a->live_touch,action,0,1,&id,&x,&y,667,375,0,0,667,375,ns));
    if(action==DM_LIVE_ACTION_DOWN)shop_ghost_contact_event(a,action,id);
}
int main(void){
    game_app a={0};a.live_touch=dm_live_touch_create(667,375);assert(a.live_touch);
    a.live_provider_available=1;
    a.live_geometry.logical_width=667;a.live_geometry.logical_height=375;
    a.live_observation.scene_epoch=a.live_observation.world_epoch=a.live_observation.ui_identity=1;
    dm_live_touch_set_context(a.live_touch,1,0,1,1,1,0);
    full=1;tick(&a,900000000);assert(!map_calls);full=0;ingame=1;
    event(&a,DM_LIVE_ACTION_DOWN,260,110,1000000000);
    uint64_t owner_token=atomic_load(&ghost_contact);
    assert(owner_token&&ghost_pointer_id==1);
    int32_t ids[2]={1,2};float xs[2]={260,640},ys[2]={110,350};
    assert(dm_live_touch_event(a.live_touch,DM_LIVE_ACTION_POINTER_DOWN,1,2,ids,xs,ys,667,375,0,0,667,375,1010000000));
    shop_ghost_contact_event(&a,DM_LIVE_ACTION_POINTER_DOWN,2);
    shop_ghost_contact_event(&a,DM_LIVE_ACTION_POINTER_UP,2);
    assert(dm_live_touch_event(a.live_touch,DM_LIVE_ACTION_POINTER_UP,1,2,ids,xs,ys,667,375,0,0,667,375,1020000000));
    assert(atomic_load(&ghost_contact)==owner_token&&ghost_pointer_id==1);
    hit=2;hit_id=1;
    tick(&a,1490000000);assert(!stat_adds);
    ready=0;tick(&a,1510000000);assert(!stat_adds);
    ready=1;tick(&a,1520000000);assert(stat_adds==1);
    tick(&a,1600000000);assert(stat_adds==1);
    tick(&a,1680000000);assert(stat_adds==2);
    event(&a,DM_LIVE_ACTION_UP,260,110,1690000000);
    tick(&a,1800000000);assert(stat_adds==2);
    // A selected stat requires a second tap; a new target only selects.
    dm_tc_state *c=dm_live_touch_controls(a.live_touch);
    a.selected_point=0;c->tap_move_seq++;tick(&a,1900000000);assert(stat_adds==2);
    c->tap_move_seq++;tick(&a,2000000000);assert(stat_adds==3);
    // Skill long press opens the game's Y binding path once, without adding points.
    event(&a,DM_LIVE_ACTION_DOWN,260,110,3000000000);hit=1;hit_id=0x80000001;
    tick(&a,3510000000);assert(c->pulse_button==DM_BTN_Y);assert(!skill_adds);
    event(&a,DM_LIVE_ACTION_CANCEL,260,110,3520000000);
    dm_live_touch_set_context(a.live_touch,1,0,1,1,1,0);
    c=dm_live_touch_controls(a.live_touch);a.selected_point=0;c->tap_move_seq++;
    tick(&a,4000000000);c->tap_move_seq++;tick(&a,4100000000);
    points.tree_open=1;points.sel_skill=4;points.sel_block=0;
    tick(&a,4410000000);assert(skill_adds==1);
    tick(&a,4500000000);assert(skill_adds==1);
    // Real core scroll/slider state reaches the host and always ends.
    event(&a,DM_LIVE_ACTION_DOWN,260,110,5000000000);
    dm_live_touch_apply_item_hit(a.live_touch,c->pointer_slot,11);
    event(&a,DM_LIVE_ACTION_MOVE,260,150,5100000000);tick(&a,5110000000);
    assert(drag_begins==1&&last_drag==1&&drag_moves==1);
    event(&a,DM_LIVE_ACTION_UP,260,150,5200000000);tick(&a,5210000000);assert(drag_ends==1);
    live_frontend_touch(&a,0,7,.4f,.4f);live_frontend_touch(&a,1,7,.4f,.6f);
    assert(a.fe_drag&&last_drag==1);int prior=touches;
    live_frontend_touch(&a,2,8,.4f,.6f);assert(a.fe_drag&&touches==prior);
    live_frontend_touch(&a,2,7,.4f,.6f);assert(!a.fe_drag&&!a.fe_pointer_active);
    // Map-title tap queries the world before switching; NPC/item wins over the map.
    dm_live_touch_set_context(a.live_touch,0,(1u<<DM_TC_COUNT)-1,1,1,1,0);
    float rect[]={.65f,.02f,.9f,.35f};dm_live_touch_set_minimap(a.live_touch,rect,0);
    event(&a,DM_LIVE_ACTION_DOWN,460,15,6000000000);event(&a,DM_LIVE_ACTION_UP,460,15,6100000000);
    world_result=2;tick(&a,6110000000);assert(!map_calls);
    event(&a,DM_LIVE_ACTION_DOWN,460,15,7000000000);event(&a,DM_LIVE_ACTION_UP,460,15,7100000000);
    world_result=0;tick(&a,7110000000);assert(full&&map_calls==1);
    c->world_tap_x=200;c->world_tap_y=100;c->world_tap_seq++;
    tick(&a,7200000000);assert(world_taps==1);
    tick(&a,7300000000);assert(world_taps==1);
    dm_live_touch_set_context(a.live_touch,1,0,1,1,1,0);
    c->world_tap_seq++;tick(&a,7400000000);assert(world_taps==1);
    dm_live_touch_destroy(a.live_touch);
    puts("PASS actual frontend: stat hold/repeat, skill select/add/bind, scroll lifecycle, map world priority");
}
