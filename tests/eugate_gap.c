#define EUGATE_HOST_TEST
#include <stdio.h>
#include "../src/custom/eugate/eugate.c"
int main(void){
  for(int gp=0;gp<=40;gp+=20){
    ChState s; ChParams P; float u[9]={15,15,0,40,0,gp,0,120,100}; /* 8 notes of 16, swing 40, hard */
    ch_prepare(&P,u); ch_init(&s); s.g=0; static float b[8]; float mn=9,mx=0; long smp=0; int dips=0; int prev=1;
    for(long k=0;k<44100*4;k+=8){for(int i=0;i<8;i++)b[i]=0.3f;ch_process(&s,&P,b,8);
      for(int i=0;i<8;i++){ if(!(b[i]==b[i])) {puts("NaN");return 1;} }
      if(k>20000){ int on=b[7]>0.15f; if(prev&&!on)dips++; prev=on; }}
    printf("gap %2d: gate falls %d times in ~3.5s\n",gp,dips);}
  return 0;}
