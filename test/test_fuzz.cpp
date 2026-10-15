// Host test for ReflectINI: Random parser input (run with AddressSanitizer / UndefinedBehaviorSanitizer).
// Build and run: see test/Makefile.
#include "ReflectINI.h"
#include <cstdlib>
#include <cstdio>
struct W { char ssid[8]=""; int ip[2]={0,0}; REFLECT(MEMBER(ssid), MEMBER(ip)) };
struct C { int a=0; float f=0; char s[6]=""; bool b=false; W w; W arr[2]; const char* p=nullptr; REFLECT(MEMBER(a),MEMBER(f),MEMBER(s),MEMBER(b),MEMBER(w),MEMBER(arr),MEMBER(p)) };
int main(){
  const char alpha[] = "ab=[]., ;#\"'\\\n\r\t01x-e.wsipfarr";
  srand(1);
  char buf[300];
  long ok=0;
  for(int it=0; it<300000; it++){
    int n = rand()%sizeof(buf);
    for(int i=0;i<n;i++) buf[i] = (rand()%8==0) ? char(rand()%256) : alpha[rand()%(sizeof(alpha)-1)];
    buf[n]=0;
    INIreader r(16,4, it%2 ? IniConfig::strict() : IniConfig{});
    if(r.parse(buf)){ C c; if(r.read(c)) ok++; int x; r.read(x,"a"); W w; r.read(w,"w"); }
    // roundtrip check on the writer
    C c2; INIwriter w(512); w.write(c2);
  }
  printf("fuzz done, %ld successful reads\n", ok);
}
