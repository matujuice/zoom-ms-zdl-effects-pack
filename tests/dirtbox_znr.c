/* DirtBox ZNR: kicks (55 Hz, 1.2 s exponential tail) over -70 dBFS noise, then 2 s of
 * noise only. Checks the noise between kicks is cut and the kick tails are kept. */
#define DIRTBOX_HOST_TEST
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../src/custom/dirtbox/dirtbox.c"
#define SR 44100
#define KICK (SR*3/8)            /* 160 BPM quarter notes */
#define NK 64
#define N (KICK*NK + 2*SR)
static float noise(void){ return 0.0003f*((float)rand()/RAND_MAX*2.0f-1.0f)*1.732f; }  /* ~ -70 dBFS rms */
static void run(float *x, int m, float znr){
  AbState s; AbParams P; float k[9]={m/6.0f,0.8f,0.8f,0.5f,0.5f,0.5f,znr,0.5f,1.0f}; int i;
  srand(1);
  for(i=0;i<N;i++){ float y=noise();
    if(i<KICK*NK){ int p=i%KICK; float tt=(float)p/SR; y+=0.7f*expf(-tt*5.75f)*sinf(6.2831853f*(55.0f*tt+40.0f*(1.0f-expf(-tt*30.0f))/30.0f)); }
    x[i]=y; }
  ab_prepare(&P,k); ab_init(&s,&P);
  for(i=0;i<N;i+=8)ab_process(&s,&P,x+i,8);
}
static double rms(float *x,int a,int b){ double e=0; int i; for(i=a;i<b;i++)e+=x[i]*x[i]; return sqrt(e/(b-a)+1e-30); }
int main(void){
  static float off[N], on[N]; const char *nm[7]={"TS9","DIST+","DS-1","RAT2","MUFF","SFUZZ","MT-2"}; int m, bad=0;
  for(m=0;m<7;m++){
    double tail_off=0,tail_on=0,body_off=0,body_on=0; int j;
    run(off,m,0.0f); run(on,m,0.5f);
    for(j=NK/2;j<NK;j++){ int b=j*KICK;
      body_off+=rms(off,b,b+SR/20); body_on+=rms(on,b,b+SR/20);                    /* first 50 ms  */
      tail_off+=rms(off,b+KICK*3/4,b+KICK); tail_on+=rms(on,b+KICK*3/4,b+KICK); }  /* last quarter */
    { double nOff=rms(off,KICK*NK+SR,N), nOn=rms(on,KICK*NK+SR,N);
      double dt=20*log10(tail_on/tail_off), db=20*log10(body_on/body_off), dn=20*log10(nOn/nOff);
      printf("%-5s body %+5.2f dB  tail end %+5.2f dB  noise-only %+6.1f dB (from %.1f dBFS)\n",
             nm[m],db,dt,dn,20*log10(nOff));
      if(fabs(db)>0.1||dt<-1.0||dn>-20.0)bad=1; }
  }
  /* one long kick on its own: gain ZNR 50 vs off along the tail (input level in brackets) */
  for(m=2;m<7;m+=3){
    AbState s1,s2; AbParams P1,P2; float k1[9]={m/6.0f,0.8f,0.8f,0.5f,0.5f,0.5f,0.0f,0.5f,1.0f}, k2[9]={m/6.0f,0.8f,0.8f,0.5f,0.5f,0.5f,0.5f,0.5f,1.0f};
    int i,w; srand(2);
    for(i=0;i<3*SR;i++){ float tt=(float)i/SR; off[i]=noise()+0.7f*expf(-tt*5.75f)*sinf(6.2831853f*55.0f*tt); on[i]=off[i]; }
    ab_prepare(&P1,k1); ab_init(&s1,&P1); ab_prepare(&P2,k2); ab_init(&s2,&P2);
    for(i=0;i<3*SR;i+=8){ ab_process(&s1,&P1,off+i,8); ab_process(&s2,&P2,on+i,8); }
    printf("%-5s single kick:", nm[m]);
    for(w=1;w<=12;w++){ int b=w*SR/5; float in=0.7f*expf(-(float)b/SR*5.75f);
      double g=20*log10(rms(on,b,b+SR/50)/rms(off,b,b+SR/50));
      printf(" %.1fs %+.1f(%.0f)", (float)b/SR, g, 20*log10(in));
      if(in>0.0056f && g<-1.0) bad=1; }       /* above -45 dBFS: kept within 1 dB */
    printf("\n"); }
  printf(bad?"FAIL\n":"ok: tails kept, noise cut\n");
  return bad;}
