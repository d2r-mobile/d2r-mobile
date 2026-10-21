#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../native/pending_ranges.h"
static uint64_t seed=0xa0b19c23;
static uint64_t random_value(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
typedef struct {uintptr_t address;uint64_t size,order;dm_pending_kind kind;} record;
int main(void){
    dm_pending_ranges ranges={0};record records[128];unsigned rejected=0;
    for(unsigned round=0;round<32;round++){
        assert(!dm_pending_ranges_may_overlap(&ranges,0,UINTPTR_MAX));
        for(unsigned i=0;i<128;i++){
            record *r=&records[i];*r=(record){(uintptr_t)(4096+random_value()%8192),1+random_value()%256,i+1,i%2};
            assert(dm_pending_ranges_insert(&ranges,r->kind,r->address,r->size,r->order)==1);
            for(unsigned q=0;q<32;q++){
                uintptr_t address=random_value()%65536;uint64_t size=random_value()%512,before=random_value()%(i+3);
                if(q%2)before=DM_PENDING_ORDER_ALL;
                for(dm_pending_kind kind=DM_PENDING_UNKNOWN;kind<DM_PENDING_KIND_COUNT;kind++){
                    int reference=0;
                    for(unsigned j=0;j<=i;j++){const record *p=&records[j];if(size&&p->kind==kind&&p->order<before&&address<p->address+p->size&&p->address<address+size)reference=1;}
                    int exact=dm_pending_ranges_overlap(&ranges,kind,address,size,before,NULL);
                    assert(exact==reference);
                    if(!dm_pending_ranges_may_overlap(&ranges,address,size)){assert(!reference);rejected++;}
                }
            }
        }
        assert(!dm_pending_ranges_may_overlap(&ranges,0,4096));
        assert(!dm_pending_ranges_may_overlap(&ranges,16384,100));
        assert(dm_pending_ranges_may_overlap(&ranges,UINTPTR_MAX,2));
        assert(dm_pending_ranges_overlap(&ranges,DM_PENDING_UNKNOWN,UINTPTR_MAX,2,DM_PENDING_ORDER_ALL,NULL)==-1&&errno==EOVERFLOW);
        dm_pending_ranges_reset(&ranges);
    }
    assert(dm_pending_ranges_insert(&ranges,DM_PENDING_HOST_EFFECT,UINTPTR_MAX-16,16,1)==1);
    assert(dm_pending_ranges_may_overlap(&ranges,UINTPTR_MAX-1,1));
    assert(dm_pending_ranges_overlap(&ranges,DM_PENDING_HOST_EFFECT,UINTPTR_MAX-1,1,DM_PENDING_ORDER_ALL,NULL)==1);
    assert(rejected>100000);dm_pending_ranges_destroy(&ranges);
    puts("pending bounds: 262144 ordered brute-force comparisons, no false negatives, reset/overflow/host effects PASS");
}
