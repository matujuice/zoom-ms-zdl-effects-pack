/* DirtBox: wet level vs input for every model, Drive and input level; EQ flat and
 * at the extremes; labels; no NaN, no blow-up. */
#define DIRTBOX_HOST_TEST
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/custom/dirtbox/dirtbox.c"
#define N 88200
static double run(int m,float d,float t,float amp,float lo,float mi,float hi,float *pk,int *bad){
  static float x[N]; AbState s; AbParams P; float k[9]={m/6.0f,d,t,lo,mi,hi,0.0f,0.5f,1.0f};
  double ai=0,ao=0; int i; *pk=0;
  ab_prepare(&P,k); ab_init(&s,&P);
  for(i=0;i<N;i++){ float ph=110.0f*i/44100.0f; ph-=(float)(int)ph; x[i]=amp*(2.0f*ph-1.0f); }   /* 303-ish saw */
  for(i=0;i<N;i+=8)ab_process(&s,&P,x+i,8);
  for(i=N/2;i<N;i++){float ph=110.0f*i/44100.0f; ph-=(float)(int)ph; float y=amp*(2.0f*ph-1.0f); ai+=y*y; ao+=x[i]*x[i];
    if(!(fabsf(x[i])<4.0f))*bad=1; if(fabsf(x[i])>*pk)*pk=fabsf(x[i]);}
  if(ao<=0||ao!=ao)*bad=1;
  return 10*log10(ao/ai);
}
int main(void){
  const char *nm[7]={"TS9","DIST+","DS-1","RAT2","MUFF","SFUZZ","MT-2"};
  float ds[]={0.0f,0.5f,1.0f}, amps[]={0.05f,0.2f,0.6f}, ts[]={0.0f,0.8f,1.0f};
  int m,d,a,t,bad=0; float pk; char lab[8];
  for(m=0;m<7;m++){
    for(t=0;t<3;t++)for(d=0;d<3;d++)for(a=0;a<3;a++){
      double g=run(m,ds[d],ts[t],amps[a],0.5f,0.5f,0.5f,&pk,&bad);
      if(t==1&&a==1) printf("%-5s tone %.1f drive %.1f amp %.2f -> %+6.1f dB  peak %.2f\n",nm[m],ts[t],ds[d],amps[a],g,pk);
    }
    printf("%-5s drive .5: tone 0 %+.1f, tone 1 %+.1f, EQ all +12 %+.1f, all -12 %+.1f dB\n",nm[m],
      run(m,0.5f,0.0f,0.2f,0.5f,0.5f,0.5f,&pk,&bad),run(m,0.5f,1.0f,0.2f,0.5f,0.5f,0.5f,&pk,&bad),
      run(m,0.5f,0.8f,0.2f,1,1,1,&pk,&bad),run(m,0.5f,0.8f,0.2f,0,0,0,&pk,&bad));
  }
  ZDL_GetLabel_0(4u,lab); printf("labels: %s ",lab); ZDL_GetLabel_3(0u,lab); printf("%s ",lab);
  ZDL_GetLabel_4(12u,lab); printf("%s ",lab); ZDL_GetLabel_5(24u,lab); printf("%s\n",lab);
  if(strcmp(lab,"+12"))bad=1;
  printf(bad?"FAIL: NaN, silence or blow-up\n":"ok: finite and bounded\n");
  return bad;}
