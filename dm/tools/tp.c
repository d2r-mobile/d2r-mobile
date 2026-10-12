#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
static void *spin(void*a){volatile uint64_t x=0;for(;;)x++;return 0;}
static void *probe(void*a){
  uint64_t orig; __asm__ volatile("mrs %0, tpidr_el0":"=r"(orig));
  uint64_t mark=0x5A5A123400000000ull|(uint64_t)(uintptr_t)a;
  long changed=0,n=0;
  for(int i=0;i<20000;i++){
    __asm__ volatile("msr tpidr_el0, %0"::"r"(mark));
    for(volatile int k=0;k<20000;k++);
    if(i%50==0) usleep(100);
    uint64_t v; __asm__ volatile("mrs %0, tpidr_el0":"=r"(v));
    n++; if(v!=mark) changed++;
  }
  __asm__ volatile("msr tpidr_el0, %0"::"r"(orig));
  printf("thread %ld: orig=%llx changed %ld/%ld\n",(long)(uintptr_t)a,orig,changed,n);
  return 0;}
int main(){pthread_t t[40];for(int i=0;i<24;i++)pthread_create(&t[i],0,spin,0);
 pthread_t p[4];for(long i=0;i<4;i++)pthread_create(&p[i],0,probe,(void*)i);
 for(int i=0;i<4;i++)pthread_join(p[i],0);return 0;}
