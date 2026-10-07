/* Choral (rewrite 2026-10-07): pitch tracking, level, consistency across sources, clicks,
 * bar-aware sync, switch-on, silence, labels. Runs the real pedal entry on host signals.
 * Not a listening test: it can't say whether the choir sounds good. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../src/custom/formant/formant.c"

#define SR 44100
static unsigned char *m; static unsigned int *ctx,*magic,*desc; static float *params,*fx,*dry; static unsigned char *arena;
static ChState *st;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); puts(""); fails++; } } while (0)

static void setup(void){
  if (!m) m = mmap(0, 1u<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  ctx=(unsigned int*)m; params=(float*)(m+256); fx=(float*)(m+512); dry=(float*)(m+1024); magic=(unsigned int*)(m+640); desc=(unsigned int*)(m+768); arena=m+4096;
  memset(arena,0xC9,65536);
  ctx[1]=(unsigned)(uintptr_t)params; ctx[3]=(unsigned)(uintptr_t)desc; ctx[4]=(unsigned)(uintptr_t)dry; ctx[5]=(unsigned)(uintptr_t)fx;
  magic[0]=0xABCD1234u; magic[2]=(unsigned)(uintptr_t)&magic[1]; ctx[11]=(unsigned)(uintptr_t)&magic[2]; ctx[12]=(unsigned)(uintptr_t)&magic[0];
  desc[0]=(unsigned)(uintptr_t)arena; desc[1]=(unsigned)(uintptr_t)(arena+65536); desc[2]=65536; params[0]=1.0f;
  st=(ChState*)(((uintptr_t)arena+3u)&~(uintptr_t)3u);
}
static void knobs(int choir,int size,int chord,int sing,int pace,int feel,int glide,int tempo,int mix){
  params[FORMANT_CHOIR_SLOT]=choir/100.f; params[FORMANT_SIZE_SLOT]=size/100.f; params[FORMANT_CHORD_SLOT]=chord/100.f;
  params[FORMANT_SING_SLOT]=sing/100.f; params[FORMANT_PACE_SLOT]=pace/100.f; params[FORMANT_FEEL_SLOT]=feel/100.f;
  params[FORMANT_GLIDE_SLOT]=glide/100.f; params[FORMANT_TEMPO_SLOT]=tempo/100.f; params[FORMANT_MIX_SLOT]=mix/100.f; }
static void defaults(void){ knobs(4,2,0,2,4,40,20,120,70); }

#define NMAX (SR*4)
static float in_[NMAX], out[NMAX], note_[NMAX/8];
/* sources: 0 saw, 1 square, 2 sine, 3 low-passed saw, 4 two detuned saws */
static double ph1, ph2, lp1, lp2;
static float src(int kind, double f){
  double x;
  ph1 += f / SR; ph1 -= floor(ph1); ph2 += f * 1.006 / SR; ph2 -= floor(ph2);
  if (kind == 0) x = 2*ph1-1;
  else if (kind == 1) x = ph1 < 0.5 ? 1 : -1;
  else if (kind == 2) x = sin(6.283185307*ph1);
  else if (kind == 3) { lp1 += 0.08*((2*ph1-1)-lp1); lp2 += 0.08*(lp1-lp2); x = 3*lp2; }
  else x = (2*ph1-1) + (2*ph2-1);
  return (float)(0.4*x);
}
typedef double (*FreqFn)(int t);
static double fconst; static double f_const(int t){ (void)t; return fconst; }
static void run(int n, int kind, FreqFn fr, double gate_on, double gate_off){
  ph1=ph2=lp1=lp2=0;
  for (int b=0;b<n/8;b++){
    for (int j=0;j<8;j++){ int t=b*8+j; double tt=(double)t/SR; float x = (tt>=gate_on && tt<gate_off) ? src(kind, fr(t)) : 0.0f; in_[t]=x; fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; }
    Fx_DLY_Formant(ctx);
    for (int j=0;j<8;j++) out[b*8+j]=fx[j];
    note_[b] = st->note;
  }
}
static double rms(const float *x,int a,int n){ double r=0; for(int t=a;t<a+n;t++) r+=x[t]*x[t]; return sqrt(r/n); }
static double db(double a){ return 20*log10(a+1e-12); }

int main(void){
  const char *kn[5]={"saw","square","sine","lp saw","2 saws"};
  double notes[]={41.2,55,82.4,110,164.8,220,329.6,440,659.3,987.8};
  setup(); defaults();

  /* 1. pitch: the accepted note within 10 cents on every source, 41 Hz .. 1 kHz, checked on
   *    every block of the second half second (two detuned saws beat: at most 2% off) */
  { double worst=0, wf=0; int miss=0;
    for (int k=0;k<5;k++) for (int i=0;i<10;i++){ setup(); defaults(); fconst=notes[i]; run(SR, k, f_const, 0, 9);
      int bad=0, nb=0; double w=0, lo=1e9, hi=-1e9;
      for (int b=SR/16;b<SR/8;b++){ double c = 1200*(note_[b] - log2(notes[i])); if (k==4) c -= 1200*log2(1.003); nb++; if (c<lo) lo=c; if (c>hi) hi=c; if (fabs(c)>10) bad++; else if (fabs(c)>fabs(w)) w=c; }
      if (bad) printf("  %s %.1f Hz: %d of %d blocks off by more than 10 cents (%.0f .. %.0f)\n", kn[k], notes[i], bad, nb, lo, hi);
      if (st->hold<=0) miss++;
      if (fabs(w)>fabs(worst)) worst=w;
      if ((double)bad/nb > wf) wf=(double)bad/nb;
      if (k<4 && bad) miss++; }
    printf("pitch: worst error %.1f cents over 5 sources x 10 notes (41 Hz .. 1 kHz); off-note time at most %.1f%%\n", worst, 100*wf);
    CHECK(fabs(worst) < 10 && miss==0 && wf < 0.02, "pitch tracking"); }

  /* 2. note changes, legato (no new attack) and as an arp (each note re-attacked after a
   *    20 ms gap): time to the new note, and no wrong notes after it */
  { int pairs[][2]={{110,165},{220,330},{55,82},{440,220},{330,660},{82,330},{660,110},{110,220},{220,110}}; double worst=0, worsta=0; int wrong=0;
    for (int arp=0;arp<2;arp++) for (int p=0;p<9;p++){ setup(); defaults(); fconst=pairs[p][0]; run(SR/2, 0, f_const, 0, 9);
      fconst=pairs[p][1]; ph1=0; int lock=-1, gap = arp ? (int)(0.02*SR/8) : 0;
      for (int b=0;b<(SR/2)/8;b++){ for(int j=0;j<8;j++){ float x = b<gap ? 0.0f : src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx);
        double c=1200*(st->note-log2(fconst)); if (fabs(c)<15){ if(lock<0) lock=b; } else if (lock>=0) wrong++; }
      double ms=(lock-gap)*8000.0/SR; if (lock<0) ms=999;
      printf("  %s %d -> %d Hz: new note after %.1f ms\n", arp ? "arp   " : "legato", pairs[p][0], pairs[p][1], ms);
      if (arp) { if (ms>worsta) worsta=ms; } else if (ms>worst) worst=ms; }
    printf("note change: worst %.1f ms legato (an octave up waits 0.5 s), %.1f ms re-attacked; wrong blocks after the change %d\n", worst, worsta, wrong);
    CHECK(worst < 520 && worsta < 70 && wrong == 0, "note changes"); }

  /* 3. level: choir (Mix 100) vs input, every Choir x Size x Sing, notes 55..660 Hz, saw */
  { double lo=99, hi=-99; int wlo[4]={0}, whi[4]={0};
    for (int c=0;c<=8;c++) for (int z=0;z<=5;z++) for (int sg=0;sg<=2;sg++) for (int i=1;i<9;i++){
      setup(); knobs(c,z,0,sg,4,40,20,120,100); fconst=notes[i]; run(SR, 0, f_const, 0, 9);
      double e = db(rms(out,SR/2,SR/2)/rms(in_,SR/2,SR/2));
      if (e<lo){lo=e; wlo[0]=c;wlo[1]=z;wlo[2]=sg;wlo[3]=(int)notes[i];} if (e>hi){hi=e; whi[0]=c;whi[1]=z;whi[2]=sg;whi[3]=(int)notes[i];} }
    printf("level, choir vs input: %.1f .. %.1f dB (lowest choir %d size %d sing %d %d Hz; highest choir %d size %d sing %d %d Hz)\n",
           lo,hi,wlo[0],wlo[1],wlo[2],wlo[3],whi[0],whi[1],whi[2],whi[3]);
    CHECK(lo > -6 && hi < 6, "level range"); }

  /* 4. consistency: the same note on five different sources gives the same choir level */
  { double worst=0;   /* spread: loudest minus quietest source on the same note */
    for (int i=2;i<9;i++){ double l[5], mn=99, mx=-99;
      for (int k=0;k<5;k++){ setup(); knobs(4,2,0,0,4,40,20,120,100); fconst=notes[i]; run(SR, k, f_const, 0, 9); l[k]=db(rms(out,SR/2,SR/2)/rms(in_,SR/2,SR/2)); }
      for (int k=0;k<5;k++){ if (l[k]<mn) mn=l[k]; if (l[k]>mx) mx=l[k]; } if (mx-mn>worst) worst=mx-mn;
      printf("  %.0f Hz: %.1f %.1f %.1f %.1f %.1f dB\n", notes[i], l[0],l[1],l[2],l[3],l[4]); }
    printf("consistency: choir level across saw/square/sine/lp saw/2 saws differs by at most %.1f dB\n", worst);
    CHECK(worst < 2.5, "consistency across sources"); }

  /* 5. ceiling, NaN, silence */
  { double pk=0; int nan=0;
    for (int c=0;c<=8;c+=2) for (int k=0;k<5;k++){ setup(); knobs(c,5,1,2,10,100,0,120,100); fconst=notes[(c+k)%10]; run(SR, k, f_const, 0, 9);
      for (int t=0;t<SR;t++){ if (!(out[t]==out[t])) nan=1; if (fabs(out[t])>pk) pk=fabs(out[t]); } }
    printf("loud settings: peak %.2f, NaN %d\n", pk, nan); CHECK(!nan && pk<=1.0001, "peak or NaN");
    setup(); defaults(); fconst=220; run(SR*2, 0, f_const, 0, 0.5);
    double tail = rms(out, SR+SR/2, SR/2);
    printf("silence after a note: choir rms %.2g (%.0f dB)\n", tail, db(tail)); CHECK(tail < 1e-4, "choir keeps sounding in silence"); }

  /* 6. clicks: largest step between output samples vs a steady note, with Pace 2bar and a
   *    Tempo flip every bar (bar-aware: the cycle must go 0, 0.5, 0, 0.5 ...) */
  { setup(); knobs(4,2,0,2,2,40,20,120,100); fconst=220; double worst_ph=0; int flips=0;
    int bar = (int)(4*60.0*SR/120/8);  /* blocks per bar at 120 */
    ph1=0; double maxd=0, maxd_ref=0;
    for (int b=0;b<bar*9;b++){
      if (b>0 && b%bar==0){ params[FORMANT_TEMPO_SLOT] = ((b/bar)&1) ? 3.21f : 1.20f; flips++; }
      for(int j=0;j<8;j++){ float x=src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; }
      float before = st->wsm; Fx_DLY_Formant(ctx);
      double dw=fabs(st->wsm-before); if (b%bar==0 && b>bar) { double want = (((b/bar)-1)&1) ? 0.5 : 0.0; double e=fabs(st->lfo_ph-want); if (e>0.5) e=1-e; if (e>worst_ph) worst_ph=e; if (dw>maxd) maxd=dw; } else if (b>0 && dw>maxd_ref) maxd_ref=dw; }
    printf("bar flips, Pace 2bar: cycle off the bar by at most %.4f, vowel step at a flip %.4f (elsewhere %.4f)\n", worst_ph, maxd, maxd_ref);
    CHECK(worst_ph < 0.01 && maxd <= maxd_ref*1.5+1e-4, "bar-aware restart"); }

  /* 7. switch-on: no burst of old sound */
  { setup(); defaults(); fconst=330; run(SR, 0, f_const, 0, 9);
    params[0]=0; fconst=110; run(SR/4, 0, f_const, 0, 9); params[0]=1; knobs(4,2,0,2,4,40,20,120,100);
    for (int b=0;b<40;b++){ for(int j=0;j<8;j++){ fx[j]=0; fx[j+8]=0; } Fx_DLY_Formant(ctx); for(int j=0;j<8;j++) out[b*8+j]=fx[j]; }
    double pk=0; for (int t=0;t<320;t++) if (fabs(out[t])>pk) pk=fabs(out[t]);
    printf("switch-on into silence: peak %.2g\n", pk); CHECK(pk < 1e-4, "switch-on burst"); }

  /* 8. Mix 0 = dry */
  { setup(); knobs(4,2,0,2,4,40,20,120,0); fconst=220; run(SR/2, 0, f_const, 0, 9); double d=0; for(int t=0;t<(SR/2/8)*8;t++) d=fmax(d,fabs(out[t]-in_[t])); printf("Mix 0: largest difference from dry %.2g (dryG/wetG raw %g)\n", d, params[FORMANT_MIX_SLOT]); CHECK(d<1e-6, "Mix 0 not dry"); }

  { char b[8]; unsigned i; printf("labels:");
    for (i=0;i<=8;i++){ ZDL_GetLabel_0(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=5;i++){ ZDL_GetLabel_1(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=1;i++){ ZDL_GetLabel_2(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=2;i++){ ZDL_GetLabel_3(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=16;i+=4){ ZDL_GetLabel_4(i,b); printf(" %s",b); } printf("\n"); }
  printf("%d failed checks\n", fails);
  return fails ? 1 : 0;
}
