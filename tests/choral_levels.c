#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../src/custom/formant/formant.c"
static unsigned char *m; static unsigned int *ctx,*magic,*desc; static float *params,*fx; static unsigned char *arena;
static void setup(void){
  m = mmap(0, 1u<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  ctx=(unsigned int*)m; params=(float*)(m+256); fx=(float*)(m+512); magic=(unsigned int*)(m+640); desc=(unsigned int*)(m+768); arena=m+4096;
  memset(arena,0xC9,65536);
  ctx[1]=(unsigned)(uintptr_t)params; ctx[3]=(unsigned)(uintptr_t)desc; ctx[5]=(unsigned)(uintptr_t)fx;
  magic[0]=0xABCD1234u; magic[2]=(unsigned)(uintptr_t)&magic[1]; ctx[11]=(unsigned)(uintptr_t)&magic[2]; ctx[12]=(unsigned)(uintptr_t)&magic[0];
  desc[0]=(unsigned)(uintptr_t)arena; desc[1]=(unsigned)(uintptr_t)(arena+65536); desc[2]=65536; params[0]=1.0f;
}
static int DET=0, SHP=0;
static void K9(int vowel,int tempo,int div,int depth,int reso,int mix,int sec,int stag,int size){
 params[FORMANT_VOWEL_SLOT]=vowel/100.f; params[FORMANT_TEMPO_SLOT]=tempo/100.f; params[FORMANT_DIV_SLOT]=div/100.f; params[FORMANT_DEPTH_SLOT]=depth/100.f; params[FORMANT_RESO_SLOT]=reso/100.f; params[FORMANT_MIX_SLOT]=mix/100.f;
 params[FORMANT_CHORD_SLOT]=sec/100.f; params[FORMANT_PARAM_SLOT]=stag/100.f; params[FORMANT_SHAPE_SLOT]=SHP/100.f; (void)size; }
static void K(int vowel,int tempo,int div,int depth,int reso,int mix){ K9(vowel,tempo,div,depth,reso,mix,0,0,0); }
#define N 88200
static float in_[N], out[N];
static unsigned rs=1; static float noise(void){ rs=rs*1664525u+1013904223u; return ((rs>>8)/8388608.0f-1.0f)*0.3f; }
static float pink(void){ static float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0; float w=noise()*3.0f; b0=0.99886f*b0+w*0.0555179f; b1=0.99332f*b1+w*0.0750759f; b2=0.96900f*b2+w*0.1538520f; b3=0.86650f*b3+w*0.3104856f; b4=0.55000f*b4+w*0.5329522f; b5=-0.7616f*b5-w*0.0168980f; float o=(b0+b1+b2+b3+b4+b5+b6+w*0.5362f)*0.11f; b6=w*0.115926f; return o; }
static float saw(int t){ double ph=fmod(t*110.0/44100.0,1.0); return 0.4f*(float)(2*ph-1); }
static void run(int n, int kind){ for(int b=0;b<n/8;b++){ for(int j=0;j<8;j++){int t=b*8+j; float x= kind==0? noise(): kind==1? saw(t): kind==3? pink(): kind==4? 0.3f*(float)sin(2*M_PI*220*t/44100.0): kind==5? 0.4f*(float)(2*fmod(t*55.0/44100.0,1.0)-1): kind==6? 0.3f*(float)sin(2*M_PI*880*t/44100.0): 0; in_[t]=x; fx[j]=x; fx[j+8]=0;}
  Fx_DLY_Formant(ctx); for(int j=0;j<8;j++) out[b*8+j]=fx[j]; } }
static double mag(float *x,int n,double f){ double re=0,im=0; for(int t=0;t<n;t++){ double w=0.5-0.5*cos(6.2831853*t/(n-1)); re+=w*x[t]*cos(6.2831853*f*t/44100); im+=w*x[t]*sin(6.2831853*f*t/44100);} return sqrt(re*re+im*im)/n*4; }
static double rms(float*x,int a,int n){double r=0;for(int t=a;t<a+n;t++)r+=x[t]*x[t];return sqrt(r/n);}
static int bad=0;


int main(void){
 int bad=0; double P=0; int nan=0;
 for(int ci=0;ci<=83;ci++){ for(int kind=0;kind<3;kind++){ int kk[3]={1,0,4}; setup(); K9(10*(ci%5),120,2,50,60+ci%40,100,ci,50,0); run(N,kk[kind]); for(int t=0;t<N;t++){ if(!(out[t]==out[t])||fabs(out[t])>50) nan=1; if(fabs(out[t])>P) P=fabs(out[t]); } } }
 printf("all 84 Chord settings x 3 sources: peak %.3f nan=%d\n",P,nan); if(nan||P>1.001) bad=1;
 /* level match across vowels / reso / chords for saw and noise, mix 100 */
 double worst=0; for(int v=0;v<5;v++) for(int r=0;r<=100;r+=25) for(int ci=0;ci<=83;ci+=9) for(int kind=0;kind<2;kind++){ int kk[2]={1,0}; setup(); K9(10*v,120,2,0,r,100,ci,30,0); run(N,kk[kind]); double e=20*log10(rms(out,40000,40000)/rms(in_,40000,40000)); if(fabs(e)>fabs(worst)) worst=e; }
 printf("worst level error, saw/noise, all vowels, Reso 0..100: %.2f dB\n",worst); if(fabs(worst)>1.5) bad=1;
 /* mix 0 = dry */
 setup(); K9(0,120,2,0,80,0,50,30,0); run(N,1); double d=0; for(int t=0;t<N;t++) d=fmax(d,fabs(out[t]-in_[t])); printf("mix 0 diff %.2g\n",d); if(d>1e-6) bad=1;
 { char b[8]; int vv[10]={0,1,42,43,44,48,49,50,51,83}; printf("labels:"); for(int i=0;i<10;i++){ ZDL_GetLabel_2(vv[i],b); printf(" %d=%s",vv[i],b);} printf("\n"); }
 printf(bad?"FAILED\n":"ALL OK\n"); return bad; }
