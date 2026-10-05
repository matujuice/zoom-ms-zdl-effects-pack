/* DirtBox: wet level vs input for every model, Drive and input level; no NaN, no blow-up. */
#define DIRTBOX_HOST_TEST
#include <stdio.h>
#include <math.h>
#include "../src/custom/dirtbox/dirtbox.c"
#define N 88200
int main(void){
  const char *nm[3]={"DS-1","RAT","METAL"};
  float ds[]={0.0f,0.5f,1.0f}, amps[]={0.05f,0.2f,0.6f}, ts[]={0.0f,0.5f,1.0f};
  static float x[N]; int m,d,a,t,i,bad=0;
  for(m=0;m<3;m++)for(t=0;t<3;t++)for(d=0;d<3;d++)for(a=0;a<3;a++){
    DbState s; DbParams P; float k[6]={m*0.5f,ds[d],ts[t],0.0f,0.5f,1.0f};
    double ai=0,ao=0; float pk=0;
    db_prepare(&P,k); db_init(&s,&P);
    for(i=0;i<N;i++)x[i]=amps[a]*sinf(6.2831853f*110.0f*i/44100.0f);
    for(i=0;i<N;i+=8)db_process(&s,&P,x+i,8);
    for(i=N/2;i<N;i++){float y=amps[a]*sinf(6.2831853f*110.0f*i/44100.0f);ai+=y*y;ao+=x[i]*x[i];
      if(!(fabsf(x[i])<4.0f))bad=1; if(fabsf(x[i])>pk)pk=fabsf(x[i]);}
    if(t==1||(d==2&&a==1))
      printf("%-5s tone %.1f drive %.1f amp %.2f -> %+6.1f dB  peak %.2f\n",nm[m],ts[t],ds[d],amps[a],10*log10(ao/ai),pk);
    if(ao<=0||ao!=ao)bad=1;
  }
  printf(bad?"FAIL: NaN, silence or blow-up\n":"ok: finite and bounded\n");
  return bad;}
