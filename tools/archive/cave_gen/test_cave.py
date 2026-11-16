import subprocess, os, sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
sys.path.insert(0, _ROOT)
from cavegen import build

cave = build()
body = cave[:-11] + b"\xc3"   # strip "call [slot]" + "jmp return", add ret

def cbytes(b):
    return ",".join(str(x) if x < 128 else str(x-256) for x in b)

src = r"""
#include <stdio.h>
#include <windows.h>
static char code[] = {%s};
static unsigned char pool[4*32];   /* record k: x float at base+0xc => esi = pool-12 */
static unsigned short idx[4] = {0,1,2,3};
static void setf(int rec, int off, float v){
  float* p = (float*)(pool + rec*32 + off);
  *p = v;
}
static float getf(int rec, int off){
  return *(float*)(pool + rec*32 + off);
}
int main(void){
  int i;
  /* quad: x=100,200,200,100 y=100,100,200,200  center=(150,150) */
  float xs[4]={100,200,200,100}, ys[4]={100,100,200,200};
  for(i=0;i<4;i++){ setf(i,0,xs[i]); setf(i,4,ys[i]); }
  printf("before: ");
  for(i=0;i<4;i++) printf("(%%.3f,%%.3f) ", getf(i,0), getf(i,4));
  printf("\n");
  VirtualProtect(code, sizeof(code), PAGE_EXECUTE_READWRITE, (DWORD*)&i);
  int n = 4;
  asm volatile(".intel_syntax noprefix;\n\t"
    "call %%3;\n\t"
    ".att_syntax prefix;"
    : : "S"(pool-12), "D"(idx), "c"(n), "r"(code) : "memory");
  printf("after:  ");
  for(i=0;i<4;i++) printf("(%%.3f,%%.3f) ", getf(i,0), getf(i,4));
  printf("\n");
  printf("expect: (112.500,112.500) (187.500,112.500) (187.500,187.500) (112.500,187.500)\n");
  return 0;
}
""" % cbytes(body)

open(os.path.join(paths.cache_dir('cave'), 'test_cave.c'),"w").write(src)
r = subprocess.run(["gcc","-m32","-O0",os.path.join(paths.cache_dir('cave'), 'test_cave.c'),"-o",os.path.join(paths.cache_dir('cave'), 'test_cave.exe')],capture_output=True)
print(r.stdout.decode(errors="ignore"), r.stderr.decode(errors="ignore"))
if r.returncode == 0:
    r2 = subprocess.run([os.path.join(paths.cache_dir('cave'), 'test_cave.exe')],capture_output=True)
    print(r2.stdout.decode(errors="ignore"), r2.stderr.decode(errors="ignore"), "exit", r2.returncode)
