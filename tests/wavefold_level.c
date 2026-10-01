#define WAVEFOLD_HOST_TEST
#include <stdio.h>
#include <math.h>
#include "../src/custom/wavefold/wavefold.c"
int main(void){
  float ds[]={0.05f,0.3f,0.6f,1.0f}; float amps[]={0.02f,0.1f,0.4f}; int d,a,i;
  for(d=0;d<4;d++)for(a=0;a<3;a++){
    WfState s; WfParams P; float k[5]={ds[d],0.5f,0.6f,0.5f,1.0f}; static float x[48000*3]; double ai=0,ao=0;
    wf_prepare(&P,k); wf_init(&s,&P);
    for(i=0;i<144000;i++)x[i]=amps[a]*sinf(6.2831853f*196.0f*i/48000.0f);
    for(i=0;i<144000;i+=64)wf_process(&s,&P,x+i,64);
    {static float y[48000*3]; for(i=0;i<144000;i++)y[i]=amps[a]*sinf(6.2831853f*196.0f*i/48000.0f);
     for(i=96000;i<144000;i++){ai+=y[i]*y[i];ao+=x[i]*x[i];}}
    printf("drive %.2f amp %.2f -> %+.2f dB\n",ds[d],amps[a],10*log10(ao/ai));}
  return 0;}
