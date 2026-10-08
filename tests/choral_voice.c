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
  if (!m) m = mmap(0, 2u<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);
  ctx=(unsigned int*)m; params=(float*)(m+256); fx=(float*)(m+512); dry=(float*)(m+1024); magic=(unsigned int*)(m+640); desc=(unsigned int*)(m+768); arena=m+4096;
  memset(arena,0xC9,1u<<20);
  ctx[1]=(unsigned)(uintptr_t)params; ctx[3]=(unsigned)(uintptr_t)desc; ctx[4]=(unsigned)(uintptr_t)dry; ctx[5]=(unsigned)(uintptr_t)fx;
  magic[0]=0xABCD1234u; magic[2]=(unsigned)(uintptr_t)&magic[1]; ctx[11]=(unsigned)(uintptr_t)&magic[2]; ctx[12]=(unsigned)(uintptr_t)&magic[0];
  desc[0]=(unsigned)(uintptr_t)arena; desc[1]=(unsigned)(uintptr_t)(arena+(1u<<20)); desc[2]=1u<<20; params[0]=1.0f;
  st=(ChState*)(((uintptr_t)arena+3u)&~(uintptr_t)3u);
}
static void knobs(int choir,int size,int chord,int sing,int pace,int feel,int glide,int tempo,int mix){
  params[FORMANT_CHOIR_SLOT]=choir/100.f; params[FORMANT_SIZE_SLOT]=size/100.f; params[FORMANT_CHORD_SLOT]=chord/100.f;
  params[FORMANT_SING_SLOT]=sing/100.f; params[FORMANT_PACE_SLOT]=pace/100.f; params[FORMANT_FEEL_SLOT]=feel/100.f;
  params[FORMANT_GLIDE_SLOT]=glide/100.f; params[FORMANT_TEMPO_SLOT]=tempo/100.f; params[FORMANT_MIX_SLOT]=mix/100.f; }
static void defaults(void){ knobs(4,2,0,6,4,40,20,120,70); }

#define NMAX (SR*4)
static float in_[NMAX], out[NMAX], note_[NMAX/8];
/* sources (one oscillator each, as Choral is meant for): 0 saw, 1 square, 2 sine,
 * 3 low-passed saw, 4 narrow pulse (25 %) */
static double ph1, lp1, lp2;
static float src(int kind, double f){
  double x;
  ph1 += f / SR; ph1 -= floor(ph1);
  if (kind == 0) x = 2*ph1-1;
  else if (kind == 1) x = ph1 < 0.5 ? 1 : -1;
  else if (kind == 2) x = sin(6.283185307*ph1);
  else if (kind == 3) { lp1 += 0.08*((2*ph1-1)-lp1); lp2 += 0.08*(lp1-lp2); x = 3*lp2; }
  else x = ph1 < 0.25 ? 1.5 : -0.5;
  return (float)(0.4*x);
}
typedef double (*FreqFn)(int t);
static double fconst; static double f_const(int t){ (void)t; return fconst; }
static void run(int n, int kind, FreqFn fr, double gate_on, double gate_off){
  ph1=lp1=lp2=0;
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
  const char *kn[5]={"saw","square","sine","lp saw","pulse 25%"};
  double notes[]={41.2,55,82.4,110,164.8,220,329.6,440,659.3,987.8};
  setup(); defaults();

  /* 1. pitch: the accepted note within 10 cents on every source, 41 Hz .. 1 kHz, checked on
   *    every block of the second half second */
  { double worst=0, wf=0; int miss=0;
    for (int k=0;k<5;k++) for (int i=0;i<10;i++){ setup(); defaults(); fconst=notes[i]; run(SR, k, f_const, 0, 9);
      int bad=0, nb=0; double w=0, lo=1e9, hi=-1e9;
      for (int b=SR/16;b<SR/8;b++){ double c = 1200*(note_[b] - log2(notes[i])); nb++; if (c<lo) lo=c; if (c>hi) hi=c; if (fabs(c)>10) bad++; else if (fabs(c)>fabs(w)) w=c; }
      if (bad) printf("  %s %.1f Hz: %d of %d blocks off by more than 10 cents (%.0f .. %.0f)\n", kn[k], notes[i], bad, nb, lo, hi);
      if (st->hold<=0) miss++;
      if (fabs(w)>fabs(worst)) worst=w;
      if ((double)bad/nb > wf) wf=(double)bad/nb;
      if (k<4 && bad) miss++; }
    printf("pitch: worst error %.1f cents over 5 sources x 10 notes (41 Hz .. 1 kHz); off-note time at most %.1f%%\n", worst, 100*wf);
    CHECK(fabs(worst) < 10 && miss==0 && wf < 0.02, "pitch tracking"); }

  /* 2. note changes, legato (no new attack) and as an arp (each note re-attacked after a
   *    20 ms gap): time to the new note (within 15 cents), and nothing more than 25 cents
   *    off after it (the first readings of a new note may wobble a few cents) */
  { int pairs[][2]={{110,165},{220,330},{55,82},{440,220},{330,660},{82,330},{660,110},{110,220},{220,110}}; double worst=0, worsta=0; int wrong=0;
    for (int arp=0;arp<2;arp++) for (int p=0;p<9;p++){ setup(); defaults(); fconst=pairs[p][0]; run(SR/2, 0, f_const, 0, 9);
      fconst=pairs[p][1]; ph1=0; int lock=-1, gap = arp ? (int)(0.02*SR/8) : 0;
      for (int b=0;b<(SR/2)/8;b++){ for(int j=0;j<8;j++){ float x = b<gap ? 0.0f : src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx);
        double c=1200*(st->note-log2(fconst)); if (fabs(c)<15){ if(lock<0) lock=b; } else if (lock>=0 && fabs(c)>25) wrong++; }
      double ms=(lock-gap)*8000.0/SR; if (lock<0) ms=999;
      printf("  %s %d -> %d Hz: new note after %.1f ms\n", arp ? "arp   " : "legato", pairs[p][0], pairs[p][1], ms);
      if (arp) { if (ms>worsta) worsta=ms; } else if (ms>worst) worst=ms; }
    printf("note change: worst %.1f ms legato, %.1f ms re-attacked; wrong blocks after the change %d\n", worst, worsta, wrong);
    CHECK(worst < 70 && worsta < 70 && wrong == 0, "note changes"); }

  /* 2b. legato octave jumps on every single-oscillator source (saw, square, sine, low-passed
   *     saw, whose level drops an octave up): taken as fast as any other note */
  { double worst=0; int f0s[]={82,110,220,330};
    for (int k=0;k<4;k++) for (int i=0;i<4;i++) for (int dn=0;dn<2;dn++){ setup(); defaults(); fconst=f0s[i]; run(SR/2, k, f_const, 0, 9);
      fconst = dn ? f0s[i]/2.0 : f0s[i]*2.0; int lock=-1;
      for (int b=0;b<(SR/2)/8;b++){ for(int j=0;j<8;j++){ float x=src(k,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx);
        if (fabs(1200*(st->note-log2(fconst)))<15){ if(lock<0) lock=b; } else lock=-1; }
      double ms = lock<0 ? 999 : lock*8000.0/SR; if (getenv("CHV")) printf("  oct k%d %d %s %.1f ms\n", k, f0s[i], dn?"dn":"up", ms); if (ms>worst) worst=ms; }
    printf("legato octave up/down, 4 sources x 4 notes: new note after at most %.1f ms\n", worst);
    CHECK(worst < 100, "legato octave jumps"); }

  /* 2f. decay tails: a 0.3 s note, then a long release with the filter closing (plain and
   *     resonant), over input noise and 50 Hz hum (-50 dB): the voice never moves to another
   *     octave or onto the hum while it can be heard */
  { int wrong=0, nb=0;
    for (int pr=0;pr<4;pr++) for (int m=36;m<=72;m+=6){ double f=440*pow(2,(m-69)/12.0); setup(); knobs(4,2,0,0,4,40,20,120,100);
      double sp=0,l1=0,l2=0,hp=0; unsigned r=1;
      for (int b=0;b<(int)(3.0*SR/8);b++){ for(int j=0;j<8;j++){ double t=(b*8+j)/(double)SR, a = t<0.3 ? 1 : exp(-(t-0.3)*3.5);
          double fc = (pr&1) ? 150+3000*exp(-t*3) : 2000*a+100, dmp = (pr&2) ? 0.08 : 0.6, g=2*sin(3.14159*fc/SR);
          sp+=f/SR; sp-=floor(sp); l1+=g*((2*sp-1)-l1-dmp*l2); l2+=g*l1; r=r*1664525u+1013904223u; hp+=50.0/SR; if(hp>1) hp-=1;
          float y=(float)(0.3*a*l2 + 0.003*((double)(r>>8)/16777216.0-0.5) + 0.003*sin(6.2831853*hp)); fx[j]=y; fx[j+8]=y; dry[j]=y; dry[j+8]=0; }
        Fx_DLY_Formant(ctx); nb++;
        if (b*8.0/SR>0.1 && st->vamp>1e-4 && fabs(1200*(st->note-log2(f)))>300) wrong++; } }
    printf("decay tails over noise and hum, 4 synth sounds x 7 notes: %.1f ms sung on a wrong note\n", wrong*8000.0/SR);
    CHECK(wrong==0, "decay tails"); }

  /* 2g. a soft note right after a loud one (-20 dB, re-attacked) is still found */
  { setup(); defaults(); fconst=220; run(SR/2, 0, f_const, 0, 0.4); int lock=-1; ph1=0;
    for (int b=0;b<(SR/4)/8;b++){ for(int j=0;j<8;j++){ float x=0.1f*src(0,330); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx);
      if (lock<0 && st->hold>0 && fabs(1200*(st->note-log2(330)))<15) lock=b; }
    printf("soft note 0.1 s after a loud one: found after %.1f ms\n", lock<0 ? 999 : lock*8000.0/SR); CHECK(lock>=0 && lock*8000.0/SR < 50, "soft note after a loud one"); }

  /* 2d. fast arp, 160 BPM 16ths (94 ms), 50 % gate, C3 up: every note sung */
  { int miss=0; double lat=0; int pat[8]={0,7,12,3,15,10,19,5}; setup(); defaults(); int stepb=(int)(0.09375*SR/8);
    for (int n8=0;n8<40;n8++){ double f=440*pow(2,(48+pat[n8%8]-69)/12.0); int first=-1; ph1=0;
      for (int b=0;b<stepb;b++){ for(int j=0;j<8;j++){ float x = b<stepb/2 ? src(0,f) : 0.0f; fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; }
        Fx_DLY_Formant(ctx); if (first<0 && st->hold>0 && st->vamp>0 && fabs(1200*(st->note-log2(f)))<40) first=b; }
      if (n8<8) continue; if (first<0) miss++; else if (first*8000.0/SR>lat) lat=first*8000.0/SR; }
    printf("160 BPM 16th arp from C3, 50%% gate: %d of 32 notes missed, each found within %.1f ms\n", miss, lat);
    CHECK(miss==0 && lat<30, "fast arp"); }

  /* 2e. Glide 0 starts every legato note fresh (the old note hushes), Glide above 0 slides
   *     to it with the voice held: lowest voice level in the 60 ms after the change */
  { double dip[2]; for (int g=0; g<2; g++){ setup(); knobs(4,2,0,6,4,40,g?20:0,120,70); fconst=110; run(SR/2, 0, f_const, 0, 9);
      double v0=st->vamp, lo=1e9; fconst=165;
      for (int b=0;b<(int)(0.06*SR/8);b++){ for(int j=0;j<8;j++){ float x=src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx); if (st->vamp<lo) lo=st->vamp; }
      dip[g]=db(lo/v0); }
    printf("legato 110 -> 165 Hz, voice level dip: Glide 0 %.1f dB, Glide 20 %.1f dB\n", dip[0], dip[1]);
    CHECK(dip[0] < -12 && dip[1] > -3, "Glide 0 re-attack, Glide 20 legato"); }

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
    printf("consistency: choir level across saw/square/sine/lp saw/pulse differs by at most %.1f dB\n", worst);
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
  { setup(); knobs(4,2,0,6,2,40,20,120,100); fconst=220; double worst_ph=0; int flips=0;
    int bar = (int)(4*60.0*SR/120/8);  /* blocks per bar at 120 */
    ph1=0; double maxd=0, maxd_ref=0;
    for (int b=0;b<bar*9;b++){
      if (b>0 && b%bar==0){ params[FORMANT_TEMPO_SLOT] = ((b/bar)&1) ? 3.21f : 1.20f; flips++; }
      for(int j=0;j<8;j++){ float x=src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; }
      float before = st->wsm; Fx_DLY_Formant(ctx);
      double dw=fabs(st->wsm-before); if (b%bar==0 && b>bar) { double want = (((b/bar)-1)&1) ? 0.5 : 0.0; double e=fabs(st->lfo_ph-want); if (e>0.5) e=1-e; if (e>worst_ph) worst_ph=e; if (dw>maxd) maxd=dw; } else if (b>0 && dw>maxd_ref) maxd_ref=dw; }
    printf("bar flips, Pace 2bar: cycle off the bar by at most %.4f, vowel step at a flip %.4f (elsewhere %.4f)\n", worst_ph, maxd, maxd_ref);
    CHECK(worst_ph < 0.01 && maxd <= maxd_ref*1.5+1e-4, "bar-aware restart"); }

  /* 6b. Chord: every singer on its interval (Choir MEN, SEXT, 110 Hz): singer pitch read from
   *     its phase step per block, to the nearest semitone (detune and vibrato are < 50 cents) */
  { int bad=0; int ch[11][6]={{0,4,7,12,16,19},{0,3,7,12,15,19},{0,2,7,12,14,19},{0,5,7,12,17,19},{0,3,6,12,15,18},
      {0,4,8,12,16,20},{0,4,7,11,12,16},{0,3,7,10,12,15},{0,4,7,10,12,16},{0,4,7,14,12,19},{0,7,16,12,19,24}};
    for (int c=0;c<=24;c++){ setup(); knobs(0,5,c,0,4,40,20,120,100); fconst=110; run(SR/2, 0, f_const, 0, 9);
      float p0[6]; for(int i=0;i<6;i++) p0[i]=st->sph[i];
      for(int j=0;j<8;j++){ float x=src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx);
      for (int i=0;i<6;i++){ double d=st->sph[i]-p0[i]; if (d<0) d+=1; double semi=12*log2(d*SR/8/110.0);
        int want = c<=12 ? ((i&1)?c:0) : c<24 ? ch[c-13][i] : 0; if ((int)lround(semi)!=want){ bad++; printf("  chord %d singer %d: %.2f semitones, want %d\n", c, i, semi, want); } } }
    printf("chords: every singer of all 25 Chord settings on its interval: %d wrong\n", bad); CHECK(bad==0, "chord intervals"); }

  /* 6c. DRONE holds the phrase's first note on every second singer; CANON: the second section
   *     sings what was played one Pace cycle (1/4 at 120 = 0.5 s) later */
  { double f0s[2]; setup(); knobs(0,1,24,0,4,40,20,120,100); fconst=110; run(SR/2, 0, f_const, 0, 9); fconst=165; run(SR/2, 0, f_const, 0, 9);
    { float a=st->sph[0], b2=st->sph[1]; for(int j=0;j<8;j++){ float x=src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } Fx_DLY_Formant(ctx);
      double d0=st->sph[0]-a, d1=st->sph[1]-b2; if(d0<0)d0+=1; if(d1<0)d1+=1; f0s[0]=d0*SR/8; f0s[1]=d1*SR/8; }
    printf("DRONE, 110 then 165 Hz legato: singer 0 at %.1f Hz, singer 1 holds %.1f Hz\n", f0s[0], f0s[1]);
    CHECK(fabs(1200*log2(f0s[0]/165))<50 && fabs(1200*log2(f0s[1]/110))<50, "drone");
    double fc[3]; int cyc=(int)(0.5*SR/8); setup(); knobs(4,1,0,11,8,40,20,120,100); fconst=110; ph1=0;
    for (int b=0;b<cyc*4;b++){ if (b==cyc*2) fconst=165;
      for(int j=0;j<8;j++){ float x=src(0,fconst); fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; } float a=st->sph[1]; Fx_DLY_Formant(ctx);
      double d=st->sph[1]-a; if(d<0)d+=1; if (b==cyc*2+cyc/2) fc[0]=d*SR/8/2; if (b==cyc*3+cyc/2) fc[1]=d*SR/8/2; if (b==cyc/2) fc[2]=st->amp[1]; }
    printf("CANON M+W, Pace 1/4: women (an octave up) 0.25 s after the change sing %.1f Hz, 0.75 s after %.1f Hz; in the first cycle level %.2g\n", fc[0], fc[1], fc[2]);
    CHECK(fabs(1200*log2(fc[0]/110))<50 && fabs(1200*log2(fc[1]/165))<50 && fc[2]<1e-3, "canon"); }

  /* 6d. syllables (LA DOO): a new one on every note, re-attacked or legato */
  { int pat[8]={0,7,12,3,15,10,19,5}, worst=99; int stepb=(int)(0.09375*SR/8);
    for (int sg=8; sg<=9; sg++) for (int gate=0; gate<2; gate++){ setup(); knobs(4,2,0,sg,4,40,gate?20:0,120,100); int resets=0, last=0;
      for (int n8=0;n8<24;n8++){ double f=440*pow(2,(48+pat[n8%8]-69)/12.0); ph1=0;
        for (int b=0;b<stepb;b++){ for(int j=0;j<8;j++){ float x = (gate || b<stepb/2) ? src(0,f) : 0.0f; fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; }
          Fx_DLY_Formant(ctx); if (st->syl < last && st->syl < 5 && last > 100) resets++; last=st->syl; } }
      printf("  sing %d, %s: %d syllables on 23 note changes\n", sg, gate ? "legato" : "50%% gate", resets); if (resets<worst) worst=resets; }
    CHECK(worst>=23, "a syllable on every note"); }
  { double u[2]; for (int k=0;k<2;k++){ setup(); knobs(0,0,0,8,k?4:8,40,20,120,100); fconst=220; run(SR/4, 0, f_const, 0, 9); u[k]=st->wsm; }
    printf("LA opens over one Pace: 0.25 s into a note, l -> ah %.2f with Pace 1/4 (0.5 s), %.2f with 1bar (2 s)\n", u[0], u[1]);
    CHECK(u[0] > 0.35 && u[0] < 0.6 && u[1] > 0.05 && u[1] < 0.2, "LA opening time"); }

  /* 6f. level of the steady settings (vowels, OOAH, VOWL) on every Choir */
  { double lo=99, hi=-99; int sgs[8]={0,1,2,3,4,5,6,7}; int wl=0, wh=0;
    for (int c=0;c<=16;c++) for (int z=0;z<=5;z+=5) for (int k=0;k<8;k++) for (int i=1;i<9;i+=2){
      setup(); knobs(c,z,0,sgs[k],4,40,20,120,100); fconst=notes[i]; run(SR, 0, f_const, 0, 9);
      double e = db(rms(out,SR/2,SR/2)/rms(in_,SR/2,SR/2)); if (e<lo){lo=e; wl=sgs[k]*1000+c*10+i;} if (e>hi){hi=e; wh=sgs[k]*1000+c*10+i;} }
    printf("level, every Choir x Sing (vowels, OOAH, VOWL): %.1f .. %.1f dB (lowest sing/choir/note %d, highest %d)\n", lo, hi, wl, wh);
    CHECK(lo > -6 && hi < 6, "level range, all settings"); }

  /* 6g. every Chord x Sing on ALL SEXT, loud: no NaN, ceiling held; envelope modes click no
   *     more than AAH on the same arp */
  { double pk=0; int nan=0; double step[12];
    for (int c=0;c<=24;c+=3) for (int sg=0;sg<=11;sg++){ setup(); knobs(14+(c%3),5,c,sg,10,100,20,120,100); fconst=notes[(c+sg)%10]; run(SR/2, 4, f_const, 0, 9);
      for (int t=0;t<SR/2;t++){ if (!(out[t]==out[t])) nan=1; if (fabs(out[t])>pk) pk=fabs(out[t]); } }
    int pat[8]={0,7,12,3,15,10,19,5}; int stepb=(int)(0.1875*SR/8);
    for (int sg=0;sg<=11;sg++){ setup(); knobs(4,2,0,sg,8,40,0,120,100); double mx=0, prev=0;
      for (int n8=0;n8<16;n8++){ double f=440*pow(2,(48+pat[n8%8]-69)/12.0); ph1=0;
        for (int b=0;b<stepb;b++){ for(int j=0;j<8;j++){ float x = b<stepb*3/4 ? src(2,f) : 0.0f; fx[j]=x; fx[j+8]=x; dry[j]=x; dry[j+8]=0; }
          Fx_DLY_Formant(ctx); for(int j=0;j<8;j++){ double d=fabs(fx[j]-prev); prev=fx[j]; if (n8>0 && d>mx) mx=d; } } }
      step[sg]=mx; }
    double wr=0; for (int sg=1;sg<=11;sg++) if (step[sg]/step[0]>wr) wr=step[sg]/step[0];
    if (getenv("CHV")) for (int sg=0;sg<=11;sg++) printf("  sing %d step %.4f\n", sg, step[sg]);
    printf("all Chord x Sing, ALL SEXT: peak %.2f, NaN %d; largest sample step on a sine arp vs AAH: x%.2f\n", pk, nan, wr);
    CHECK(!nan && pk<=1.0001 && wr < 2.0, "chord x sing peak, NaN or clicks"); }

  /* 7. switch-on: no burst of old sound */
  { setup(); defaults(); fconst=330; run(SR, 0, f_const, 0, 9);
    params[0]=0; fconst=110; run(SR/4, 0, f_const, 0, 9); params[0]=1; knobs(4,2,0,6,4,40,20,120,100);
    for (int b=0;b<40;b++){ for(int j=0;j<8;j++){ fx[j]=0; fx[j+8]=0; } Fx_DLY_Formant(ctx); for(int j=0;j<8;j++) out[b*8+j]=fx[j]; }
    double pk=0; for (int t=0;t<320;t++) if (fabs(out[t])>pk) pk=fabs(out[t]);
    printf("switch-on into silence: peak %.2g\n", pk); CHECK(pk < 1e-4, "switch-on burst"); }

  /* 8. Mix 0 = dry */
  { setup(); knobs(4,2,0,6,4,40,20,120,0); fconst=220; run(SR/2, 0, f_const, 0, 9); double d=0; for(int t=0;t<(SR/2/8)*8;t++) d=fmax(d,fabs(out[t]-in_[t])); printf("Mix 0: largest difference from dry %.2g (dryG/wetG raw %g)\n", d, params[FORMANT_MIX_SLOT]); CHECK(d<1e-6, "Mix 0 not dry"); }

  { char b[8]; unsigned i; printf("labels:");
    for (i=0;i<=16;i++){ ZDL_GetLabel_0(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=5;i++){ ZDL_GetLabel_1(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=24;i++){ ZDL_GetLabel_2(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=11;i++){ ZDL_GetLabel_3(i,b); printf(" %s",b); } printf(" |");
    for (i=0;i<=16;i+=4){ ZDL_GetLabel_4(i,b); printf(" %s",b); } printf("\n"); }
  printf("%d failed checks\n", fails);
  return fails ? 1 : 0;
}
