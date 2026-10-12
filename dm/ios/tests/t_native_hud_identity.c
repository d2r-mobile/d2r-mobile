#include "dm_touch_game_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RED_BASELINE
int dm_tg_hud_identity(dm_tg_hud_cohort *c,const uint64_t *r,uint64_t b,int g,uint64_t w,dm_tg_read_fn f,uint64_t *l,uint64_t *h){(void)c;(void)r;(void)b;(void)g;(void)w;(void)f;*l=*h=0;return 0;}
void dm_tg_hud_reset(dm_tg_hud_cohort *c,uint64_t w){memset(c,0,sizeof(*c));c->world_epoch=w;}
int dm_tg_vm_read(uint64_t a,void *o,size_t n){memcpy(o,(const void *)a,n);return 1;}
#endif
static int checks,readonly_calls;
static int observed(dm_tg_hud_cohort *c,const uint64_t *r,uint64_t b,int game,uint64_t world,dm_tg_read_fn read_fn,uint64_t *caller,uint64_t *hud) {
 uint64_t before[31];memcpy(before,r,sizeof before);
 int result=dm_tg_hud_identity(c,r,b,game,world,read_fn,caller,hud);
 if(memcmp(before,r,sizeof before)){fprintf(stderr,"FAIL native resolver changed regs\n");exit(1);}++readonly_calls;return result;
}
#define dm_tg_hud_identity observed
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);exit(1);}}while(0)
static int failed_read(uint64_t a,void *o,size_t n){(void)a;(void)o;(void)n;return 0;}
static void put32(unsigned char *p,uint32_t n){memcpy(p,&n,4);}
int main(void){
 const uint64_t base=0x123000000;unsigned char widget[7][0xc98]={{0}};uint64_t frame[8]={0},regs[31]={0},caller=0,hud=0;
 regs[29]=(uint64_t)&frame[2];frame[3]=base+0x3e2f74;dm_tg_hud_cohort c;dm_tg_hud_reset(&c,1);
 for(int i=0;i<6;i++) {put32(widget[i]+0xb18,(uint32_t)i);frame[1]=(uint64_t)widget[i];regs[19]=(uint64_t)i;CHECK(dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud)==0);}
 CHECK(c.confirmed && c.seen==63 && c.hud_identity!=0);uint64_t identity=c.hud_identity;
 for(int i=0;i<6;i++){frame[1]=(uint64_t)widget[i];regs[19]=(uint64_t)(i+6);CHECK(dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud)==DM_TG_IDENTITY_NATIVE);CHECK(caller==base+0x3e2f74 && hud==identity);}
 unsigned char original[sizeof widget];memcpy(original,widget,sizeof widget);
 frame[3]=base+0x2fa1d0;for(int i=0;i<12;i++){regs[19]=(uint64_t)i;CHECK(dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud)==0);}CHECK(c.hud_identity==identity && c.confirmed);
 frame[3]=base+0x3e2f74;regs[19]=16;CHECK(!dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud));
 regs[19]=0;frame[1]=(uint64_t)widget[5];CHECK(!dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud)); // widget enum5 versus arg0
 regs[19]=11;widget[5][0xc90]=1;CHECK(!dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud));widget[5][0xc90]=0;
 CHECK(!dm_tg_hud_identity(&c,regs,base,1,1,failed_read,&caller,&hud));
 CHECK(!dm_tg_hud_identity(&c,regs,base,0,1,dm_tg_vm_read,&caller,&hud));CHECK(c.hud_identity==identity && c.confirmed);
 put32(widget[6]+0xb18,5);frame[1]=(uint64_t)widget[6];CHECK(!dm_tg_hud_identity(&c,regs,base,1,1,dm_tg_vm_read,&caller,&hud));CHECK(c.widget[5]==(uint64_t)widget[5] && c.hud_identity==identity);
 dm_tg_hud_reset(&c,2);CHECK(!c.confirmed && !c.seen);
 for(int i=0;i<6;i++){frame[1]=(uint64_t)widget[i];regs[19]=(uint64_t)i;CHECK(!dm_tg_hud_identity(&c,regs,base,0,2,dm_tg_vm_read,&caller,&hud));}CHECK(!c.seen && !c.confirmed);
 for(int i=0;i<6;i++){frame[1]=(uint64_t)widget[i];regs[19]=(uint64_t)i;CHECK(!dm_tg_hud_identity(&c,regs,base,1,2,dm_tg_vm_read,&caller,&hud));}CHECK(c.confirmed && c.hud_identity!=identity);
 frame[1]=(uint64_t)widget[5];regs[19]=5;CHECK(dm_tg_hud_identity(&c,regs,base,1,2,dm_tg_vm_read,&caller,&hud)==1);
 frame[1]=1;regs[19]=5;CHECK(!dm_tg_hud_identity(&c,regs,base,1,2,dm_tg_vm_read,&caller,&hud)); // 真Mach读取坏堆指针。 | a real Mach read of a bad heap pointer.
 dm_tg_hud_reset(&c,3);frame[1]=(uint64_t)widget[0];regs[19]=0;CHECK(!dm_tg_hud_identity(&c,regs,base,1,3,dm_tg_vm_read,&caller,&hud));
 put32(widget[0]+0xb18,1);regs[19]=1;CHECK(!dm_tg_hud_identity(&c,regs,base,1,3,dm_tg_vm_read,&caller,&hud));CHECK(c.seen==1 && !c.widget[1]);put32(widget[0]+0xb18,0);
 CHECK(!dm_tg_vm_read(1,&caller,8));CHECK(!dm_tg_vm_read(UINT64_MAX,&caller,8));
 CHECK(!memcmp(widget,original,6*sizeof widget[0])); // 原six widget无写入；第七是测试替身。 | no writes to the original six widgets; the seventh is a test double.
 printf("PASS native Mach FramePointer/HUD cohort/cache/world negative %d/%d checks; %d/%d calls regs read-only\n",checks,checks,readonly_calls,readonly_calls);return 0;
}
