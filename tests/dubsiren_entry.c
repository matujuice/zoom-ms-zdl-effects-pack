#define MODEK 0.3333333f   /* Mode knob 1 = Fast, as a 0..1 knob value */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../src/custom/dubsiren/dubsiren.c"
static unsigned char *m; static unsigned int *ctx,*magic,*desc; static float *params,*fx; static unsigned char *arena;
static void setup(void){
  m = mmap(0, 1u<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  ctx=(unsigned int*)m; params=(float*)(m+256); fx=(float*)(m+512); magic=(unsigned int*)(m+640); desc=(unsigned int*)(m+768); arena=m+4096;
  memset(arena,0xC9,1u<<19);
  ctx[1]=(unsigned)(uintptr_t)params; ctx[3]=(unsigned)(uintptr_t)desc; ctx[5]=(unsigned)(uintptr_t)fx;
  magic[0]=0xABCD1234u; magic[2]=(unsigned)(uintptr_t)&magic[1]; ctx[11]=(unsigned)(uintptr_t)&magic[2]; ctx[12]=(unsigned)(uintptr_t)&magic[0];
  desc[0]=(unsigned)(uintptr_t)arena; desc[1]=(unsigned)(uintptr_t)(arena+(1u<<19)); desc[2]=1u<<19; params[0]=1.0f;
}
static void setk(int trig,int mode,int pitch,int rate,int depth,int vol,int time,int fb,int last){
 params[DUBSIREN_TRIG_SLOT]=trig/100.f; params[DUBSIREN_MODE_SLOT]=mode/100.f; params[DUBSIREN_PITCH_SLOT]=pitch/100.f; params[DUBSIREN_RATE_SLOT]=rate/100.f;
 params[DUBSIREN_DEPTH_SLOT]=depth/100.f; params[DUBSIREN_VOL_SLOT]=vol/100.f; params[DUBSIREN_TIME_SLOT]=time/100.f; params[DUBSIREN_FDBK_SLOT]=fb/100.f;
#ifdef OLD
 params[DUBSIREN_ECHO_SLOT]=last/100.f;
#else
 params[DUBSIREN_TEMPO_SLOT]=last/100.f;
#endif
}
#define N 132300
static float out[N];
static void run(int foot_on_at, int foot_off_at){ for(int b=0;b<N/8;b++){ int t=b*8; params[0]=(t>=foot_on_at && t<foot_off_at)?1.0f:0.0f; for(int j=0;j<8;j++){fx[j]=0; fx[j+8]=0;} Fx_DLY_DubSiren(ctx); for(int j=0;j<8;j++) out[t+j]=fx[j]; } }
int main(int argc,char**argv){
#ifdef OLD
  setup(); setk(1,1,48,67,34,50,40,55,60); run(40000,200000); FILE*f=fopen("/dev/null","wb"); fwrite(out,4,N,f); fclose(f); return 0;
#else
  FILE*f; setup(); setk(1,1,48,67,34,50,40,55,120); run(40000,200000); f=fopen("/dev/null","wb"); fwrite(out,4,N,f); fclose(f);
  /* synced LFO: check the increment directly */
  { SirenState *s=(SirenState*)(((uintptr_t)arena+3)&~3); (void)s; SirenParams P; float k[9]; SirenState *st=(SirenState*)calloc(1,sizeof(SirenState)); sr_init(st);
    int rates[5]={107,106,102,111,112}; int bpms[5]={120,120,90,200,60};
    for(int q=0;q<5;q++){ k[0]=0.3333333f;k[1]=MODEK;k[2]=.48f;k[3]=rates[q]*0.008928571f;k[4]=.34f;k[5]=.5f;k[6]=.4f;k[7]=.55f*1.0f/0.8f*0.8f;k[8]=bpms[q]*0.0022675737f; sr_prepare(st,&P,k,0);
      printf("Rate %d @%d BPM: LFO cycle = %.1f samples = %.4f beats\n",rates[q],bpms[q],1.0/P.lfo_inc,(1.0/P.lfo_inc)/(2646000.0/bpms[q])); } }
  { char b[8]; unsigned v[]={0,1,67,100,101,104,106,107,109,111,112}; for(int i=0;i<11;i++){ZDL_GetLabel_3(v[i],b); printf("R%u=%s ",v[i],b);} printf("\n");
    unsigned w[]={0,40,100}; for(int i=0;i<3;i++){ZDL_GetLabel_6(w[i],b); printf("T%u=%s ",w[i],b);} printf("\n");
    unsigned x[]={0,1,55,100,125}; for(int i=0;i<5;i++){ZDL_GetLabel_8(x[i],b); printf("F%u=%s ",x[i],b);} printf("\n"); }
  /* sweep: no NaN / limits for all rate+time values */
  { int bad=0; float pk=0; for(int r=0;r<=112;r+=7) for(int tm=0;tm<=100;tm+=9){ setup(); setk(0,r%4,48,r,34,100,tm,125,40+r); run(10000,60000); for(int t=0;t<N;t++){ if(!(out[t]==out[t])) bad++; if(fabsf(out[t])>pk) pk=fabsf(out[t]); } } printf("sweep: nan=%d peak=%.2f\n",bad,pk); }
  return 0;
#endif
}
