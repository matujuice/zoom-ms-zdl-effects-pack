#define EUGATE_HOST_TEST
#include <stdio.h>
#include "../src/custom/eugate/eugate.c"
/* Mix is a DJ-style crossfade: dry full up to 50, wet full from 50, both full at 50. */
int main(void){
  int bad=0; int mixes[5]={0,25,50,75,100}; float wantD[5]={1,1,1,0.5f,0}, wantW[5]={0,0.5f,1,1,1};
  for(int j=0;j<5;j++){
    ChState s; ChParams P; float u[9]={7,15,0,0,0,0,0,120,(float)mixes[j]}; /* 8 notes of 16, hard edges */
    ch_prepare(&P,u); ch_init(&s); s.g=0; static float b[8]; float mn=9,mx=0;
    for(long k=0;k<44100*2;k+=8){for(int i=0;i<8;i++)b[i]=0.3f;ch_process(&s,&P,b,8);
      if(k>20000) for(int i=0;i<8;i++){ if(b[i]<mn)mn=b[i]; if(b[i]>mx)mx=b[i]; }}
    printf("mix %3d: dry %.2f wet %.2f  out %.3f..%.3f (in 0.3)\n",mixes[j],P.dryG,P.wetG,mn,mx);
    if(P.dryG!=wantD[j]||P.wetG!=wantW[j]) bad=1;
    if(mn<0.3f*wantD[j]-1e-4f||mx>0.3f*(wantD[j]+wantW[j])+1e-4f) bad=1;
  }
  return bad;}
