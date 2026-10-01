#define WAVEFOLD_HOST_TEST
#include <stdio.h>
#include <math.h>
#include "../src/custom/wavefold/wavefold.c"
int main(void){
  WfState s; WfParams P; float k[5]={0.5f,0.5f,0.6f,0.5f,1.0f}; int sr=48000,N=sr*2,i,b;
  static float x[96000];
  wf_prepare(&P,k); wf_init(&s,&P);
  for(i=0;i<N;i++){double t=i/(double)sr;double e=(t<1.0?exp(-t*3):0)+(t>=1.2?exp(-(t-1.2)*3):0);
    e*=1-exp(-t*500); if(t>=1.2) e*=1; x[i]=0.4*e*sin(6.2831853*220*t);}
  static float in[96000]; for(i=0;i<N;i++)in[i]=x[i];
  for(i=0;i<N;i+=64) wf_process(&s,&P,x+i,64);
  double ts[]={0,.01,.03,.06,.1,.2,.5,1.2,1.21,1.23,1.26,1.3,1.4,1.7};
  for(b=0;b<14;b++){int i0=(int)(ts[b]*sr);double a=0,o=0;for(i=0;i<480;i++){a+=in[i0+i]*in[i0+i];o+=x[i0+i]*x[i0+i];}
    printf("%.2f in %.4f out %.4f ratio %.2f\n",ts[b],sqrt(a/480),sqrt(o/480),sqrt(o/(a+1e-20)));}
  return 0;}
