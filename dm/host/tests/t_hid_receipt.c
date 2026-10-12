// 实际dm_input_set→GetNpadState返回收样；假时钟只固定timestamp，不替换输入/返回布局。
// The actual dm_input_set -> GetNpadState return takes the sample; the fake clock only fixes the timestamp and does not replace the input/return layout.
#ifndef HLE_HID_SOURCE
#define HLE_HID_SOURCE "../hle_hid.c"
#endif
#include HLE_HID_SOURCE
module_t g_mod[MAX_MOD]; int g_nmod;
#include <unistd.h>
#include <sys/wait.h>
static uint64_t clock_ns=1000000000ull;
uint64_t dm_guest_frame(void) { return 0; }
uint64_t dm_server_tick(void) { return UINT64_MAX; }
int dm_loadscreen_up(void) { return 0; }
void dm_hold_for_loadscreen(void) {}
uint64_t now_ns(void) { return clock_ns; }
uint64_t guest_ns(void) { return clock_ns; }
static int opmode=1;
int dm_opmode(void) { return opmode; }
int touch_fill(void *p, uint64_t n) { (void)p;(void)n;return 0; }
void dm_touch_set(const dm_touch *p,int n) { (void)p;(void)n; }
void uimsg_subscribe(void (*f)(const char *,const char *,const char *)) { (void)f; }
static int checks,fails;
#define CK(c) do { checks++; if (!(c)) { fails++;printf("FAIL line=%d %s\n",__LINE__,#c); } } while(0)
typedef struct { long long sampling; unsigned long long start,time,buttons,script; unsigned id,style,attrs; int lx,ly,rx,ry; } Row;
static int load(const char *path,Row *rows,int *limit) {
 FILE *f=fopen(path,"r");if(!f)return -1;char s[512];int n=0;*limit=0;
 while(fgets(s,sizeof s,f)) { if(strstr(s,"limit_reached")){(*limit)++;continue;} Row r={0};
  int k=sscanf(s,"{\"event\":\"npad_return\",\"read_start_ns\":%llu,\"time_ns\":%llu,\"sampling\":%lld,\"id\":%u,\"style\":%u,\"buttons\":%llu,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"attributes\":%u,\"script_mask\":%llu}",&r.start,&r.time,&r.sampling,&r.id,&r.style,&r.buttons,&r.lx,&r.ly,&r.rx,&r.ry,&r.attrs,&r.script);
  CK(k==12);CK(r.start<=r.time);if(n<1024)rows[n]=r;n++;
 }fclose(f);return n;
}
static void exercise(int which) {
 char path[256];snprintf(path,sizeof path,"%s/hid-receipt-%d.jsonl",getenv("TMPDIR")?getenv("TMPDIR"):"/tmp",getpid());unlink(path);
 unsetenv("DM_KEYS");unsetenv("DM_HID_RECEIPT_LOG");if(which)setenv("DM_HID_RECEIPT_LOG",path,1);if(which==2)setenv("DM_KEYS","A@0+10",1);
 NpadState s;uint32_t id=0;Row *rows=calloc(1024,sizeof *rows);int limit=0,n;
 if(which==4) {
  dm_input_set(0,0,0,0,0);h_hid_GetFullKey(&s,&id);h_hid_GetFullKey(&s,&id);
  clock_ns+=1000000000ull;h_hid_GetFullKey(&s,&id);
  n=load(path,rows,&limit);CK(n==2);if(n==2){CK(rows[0].sampling==1&&rows[1].sampling==3);CK(rows[1].time-rows[0].time==1000000000ull);CK(!rows[0].buttons&&!rows[1].buttons);}CK(limit==0);
 } else if(which==3) {
  for(int i=0;i<1030;i++){dm_input_set(0,(i&1)?32767:-32767,0,0,0);h_hid_GetFullKey(&s,&id);CK(s.lx==((i&1)?32767:-32767));}
  n=load(path,rows,&limit);CK(n==1024);CK(limit==1);
 } else if(which==2) {
  dm_input_set(DM_BTN_B,123,-456,7,-8);h_hid_GetFullKey(&s,&id);CK(s.buttons==(DM_BTN_A|DM_BTN_B));CK(s.lx==123&&s.ly==-456);
  n=load(path,rows,&limit);CK(n==1);if(n==1){CK(rows[0].buttons==s.buttons);CK(rows[0].script==DM_BTN_A);CK(rows[0].lx==123&&rows[0].ly==-456&&rows[0].rx==7&&rows[0].ry==-8);}CK(limit==0);
 } else {
  dm_input_set(0,0,0,0,0);h_hid_GetFullKey(&s,&id);CK(s.sampling==1&&s.attributes==1);
  dm_input_set(DM_BTN_B,32767,-32767,11,-12);h_hid_GetFullKey(&s,&id);CK(s.buttons==DM_BTN_B&&s.lx==32767&&s.ly==-32767&&s.rx==11&&s.ry==-12);
  h_hid_GetFullKey(&s,&id);CK(s.sampling==3);
  dm_input_set(0,0,0,0,0);h_hid_GetFullKey(&s,&id);CK(!s.buttons&&!s.lx&&!s.ly);
  opmode=0;id=0x20;dm_input_set(DM_BTN_Y,-32767,0,0,0);h_hid_GetHandheld(&s,&id);CK(s.attributes==1&&s.buttons==DM_BTN_Y&&s.lx==-32767);
  id=0;h_hid_GetFullKey(&s,&id);CK(s.attributes==0&&!s.buttons&&!s.lx&&s.sampling==6);
  n=load(path,rows,&limit);
  if(!which)CK(n==-1);
  else {CK(n==5);CK(limit==0);if(n==5){CK(rows[0].sampling==1&&rows[0].time==clock_ns&&rows[0].buttons==0);CK(rows[1].sampling==2&&rows[1].buttons==DM_BTN_B&&rows[1].lx==32767&&rows[1].ry==-12);CK(rows[2].sampling==4&&rows[2].buttons==0&&rows[2].lx==0);CK(rows[3].id==0x20&&rows[3].style==STYLE_HANDHELD&&rows[3].attrs==1&&rows[3].lx==-32767);CK(rows[4].id==0&&rows[4].style==STYLE_FULLKEY&&rows[4].attrs==0&&rows[4].sampling==6);}}
 }
 free(rows);unlink(path);printf("HID_RECEIPT case=%d checks=%d failures=%d\n",which,checks,fails);fflush(stdout);_exit(fails?1:0);
}
int main(void){int fail=0;for(int i=0;i<5;i++){pid_t p=fork();if(!p)exercise(i);int s;waitpid(p,&s,0);fail+=!WIFEXITED(s)||WEXITSTATUS(s)!=0;}printf("HID_RECEIPT %s cases=5 failures=%d\n",fail?"FAIL":"PASS",fail);return !!fail;}
