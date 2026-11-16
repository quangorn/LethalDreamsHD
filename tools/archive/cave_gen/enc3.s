.intel_syntax noprefix
.text
f32a:  fild   DWORD PTR [esp+228]
f32b:  fild   DWORD PTR [esp+224]
f32c:  fild   DWORD PTR [esp+12]
f32d:  fild   DWORD PTR [esp]
f32e:  fistp  DWORD PTR [esp+228]
f32f:  fistp  DWORD PTR [esp+224]
f32g:  fistp  DWORD PTR [esp+12]
f32h:  fistp  DWORD PTR [esp]
fm64:  fmul   QWORD PTR ds:0x51c098
fdv:   fdivp  st(1), st(0)
fs64:  fstp   QWORD PTR ds:0x51c088
m40esi: mov    DWORD PTR [esi+0x40], edx
m6ceax: mov    DWORD PTR [eax+0x6c], ecx
lea12:  lea    ecx, [esp+0xc]
movsp:  mov    edx, DWORD PTR [esp]
sub8d:  sub    esp, 8
add8d:  add    esp, 8
subd0:  sub    esp, 0xd0
addd0:  add    esp, 0xd0
movb58: mov    BYTE PTR [esi+0x58], 0
