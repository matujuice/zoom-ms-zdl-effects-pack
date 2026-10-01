#define EUGATE_HOST_TEST
#include <stdio.h>
#include <math.h>
#include "../src/custom/eugate/eugate.c"
static int ref(int j,int hits,int steps){return ((j*hits)%steps)<hits;}
int main(void){
  int bad=0,steps,hits,sh;
  /* 1: pattern words equal the textbook formula, with rotation */
  for(steps=1;steps<=64;steps++)for(hits=1;hits<=64;hits++){
    ChParams P; float u[9]={hits-1,steps-1,0,0,0,0,50,120,100}; int j,h=hits>steps?steps:hits;
    ch_prepare(&P,u);
    for(j=0;j<steps;j++){int b=j<32?(P.lo>>j)&1:(P.hi>>(j-32))&1; if(b!=ref(j,h,steps)){bad++;}}
    for(j=steps;j<64;j++){int b=j<32?(P.lo>>j)&1:(P.hi>>(j-32))&1; if(b)bad++;}
  }
  printf("pattern mismatches vs formula: %d\n",bad);
  /* 2: run audio, record step sequence for steps=12/5 notes and steps=5/3, shift 0 and 3, swing 0 */
  int cfg[3][3]={{5,12,0},{3,5,0},{5,12,3}};
  for(int c=0;c<3;c++){
    ChState s; ChParams P; float u[9]={cfg[c][0]-1,cfg[c][1]-1,cfg[c][2],0,0,0,0,120,100};
    ch_prepare(&P,u); ch_init(&s); s.g=0;
    /* 120 bpm: a step = 0.125 s = 5512.5 samples. sample the gate mid-step for 30 steps */
    static float buf[8]; char line[80]; int n=0; long smp=0;
    for(int st=0;st<30;st++){
      long target=(long)((st+0.5)*5512.5);
      while(smp<target){for(int i=0;i<8;i++)buf[i]=0.3f;ch_process(&s,&P,buf,8);smp+=8;}
      line[n++]=buf[7]>0.15f?'X':'.';
    } line[n]=0; printf("steps %2d notes %d shift %d: %s\n",cfg[c][1],cfg[c][0],cfg[c][2],line);
  }
  return 0;}
