// Real owned mappings exercise the production write preparation, generations and BUSY protocol.
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();} } while(0)
static int observed_mprotect(void *,size_t,int);
#define mprotect observed_mprotect
#include "../../gfx/nvn/nvn_dirty.c"
#undef mprotect

void dm_fault_hook_register(int (*fn)(void *,int,void *)){(void)fn;}
void dm_guest_write_prep_register(void (*fn)(void *,size_t)){(void)fn;}
static uint8_t *owned;
static size_t owned_bytes;
static int fail_rw;
static _Atomic int armed,entered,released,fault_entered,fault_done;
static uint64_t before_write;
static int observed_mprotect(void *address,size_t bytes,int protection){
    CHECK((uintptr_t)address>=(uintptr_t)owned&&bytes<=owned_bytes&&
           (uintptr_t)address-(uintptr_t)owned<=owned_bytes-bytes);
    if(protection==(PROT_READ|PROT_WRITE)){
        if(fail_rw){errno=EACCES;return -1;}
        if(atomic_exchange(&armed,0)){
            range_t *range=find((uintptr_t)owned);
            for(unsigned i=0;i<8;i++){
                CHECK(atomic_load(&range->state[i])==PG_BUSY);
                CHECK(atomic_load(&range->gen[i])>before_write);
            }
            atomic_store(&entered,1);
            while(!atomic_load(&released))sched_yield();
        }
    }
    return mprotect(address,bytes,protection);
}
static void protect_all(void){nvn_dirty_protect(owned,owned_bytes);}
static uint64_t calls(void){uint64_t calls,pages,ns;nvn_dirty_mp_stats(&calls,&pages,&ns);return calls;}
static void *prepare_thread(void *unused){(void)unused;prep_write(owned,owned_bytes);return NULL;}
static void *fault_thread(void *unused){
    (void)unused;atomic_store(&fault_entered,1);
    CHECK(on_fault(owned+owned_bytes-getpagesize(),1,NULL)==1);atomic_store(&fault_done,1);return NULL;
}
static void wait_for(_Atomic int *flag){
    for(unsigned n=0;n<2000&&!atomic_load(flag);n++)usleep(1000);
    CHECK(atomic_load(flag));
}
int main(void){
    size_t page=getpagesize();owned_bytes=8*page;
    uint8_t *mapping=mmap(NULL,owned_bytes+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(mapping!=MAP_FAILED);owned=mapping+page;
    CHECK(!mprotect(owned,owned_bytes,PROT_READ|PROT_WRITE));nvn_dirty_add_range(owned,owned_bytes);
    range_t *range=find((uintptr_t)owned);CHECK(range);
    protect_all();uint64_t old_stamp=nvn_dirty_now(),count=calls();
    prep_write(owned+1,owned_bytes-2);
    CHECK(calls()-count==1);
    for(unsigned i=0;i<8;i++){
        CHECK(atomic_load(&range->state[i])==PG_NONE&&atomic_load(&range->gen[i])>old_stamp);
        owned[i*page]=(uint8_t)i;
    }
    protect_all();old_stamp=nvn_dirty_now();count=calls();
    prep_write(owned+page+1,4*page-2);CHECK(calls()-count==1);
    for(unsigned i=0;i<8;i++)CHECK(atomic_load(&range->state[i])==(i>=1&&i<=4?PG_NONE:PG_PROT));
    CHECK(atomic_load(&range->gen[0])<=old_stamp&&atomic_load(&range->gen[7])<=old_stamp);
    protect_all();prep_write(owned+2*page,page);prep_write(owned+5*page,page);
    uint64_t hole2=atomic_load(&range->gen[2]),hole5=atomic_load(&range->gen[5]);count=calls();
    prep_write(owned,owned_bytes);CHECK(calls()-count==3);
    CHECK(atomic_load(&range->gen[2])==hole2&&atomic_load(&range->gen[5])==hole5);
    for(unsigned i=0;i<8;i++)CHECK(atomic_load(&range->state[i])==PG_NONE);

    // A failed protection change must stop before certifying writable pages; isolate the injected failure in a child.
    protect_all();pid_t child=fork();CHECK(child>=0);
    if(!child){fail_rw=1;prep_write(owned,owned_bytes);_exit(0);}
    int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);

    protect_all();before_write=nvn_dirty_now();atomic_store(&armed,1);
    pthread_t writer,fault;CHECK(!pthread_create(&writer,NULL,prepare_thread,NULL));wait_for(&entered);
    CHECK(!pthread_create(&fault,NULL,fault_thread,NULL));wait_for(&fault_entered);usleep(1000);
    CHECK(!atomic_load(&fault_done));atomic_store(&released,1);
    CHECK(!pthread_join(writer,NULL)&&!pthread_join(fault,NULL));CHECK(atomic_load(&fault_done));
    for(unsigned i=0;i<8;i++){CHECK(atomic_load(&range->state[i])==PG_NONE);owned[i*page]=0x5a;}
    nvn_dirty_remove_range(owned);CHECK(!munmap(mapping,owned_bytes+2*page));
    puts("PASS dirty write runs: bounded/partial/holes/generations/failure/BUSY fault ordering");return 0;
}
