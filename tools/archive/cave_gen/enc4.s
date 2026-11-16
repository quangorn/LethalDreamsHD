.intel_syntax noprefix
.text
c1: sub    esp, 8
c2: mov    DWORD PTR [esp], edx
c3: mov    DWORD PTR [esp+4], eax
c4: mov    ecx, DWORD PTR [esp+20]
c5: imul   ecx, DWORD PTR ds:0x51c080
c6: mov    eax, ecx
c7: xor    edx, edx
c8: mov    ecx, DWORD PTR ds:0x51c088
c9: idiv   ecx
c10: mov   ecx, eax
c11: mov   eax, DWORD PTR [esp+4]
c12: mov   DWORD PTR [eax+108], ecx
c13: add   esp, 8
c14: lea   edx, [esp+12]
c15: imul  edx, DWORD PTR ds:0x51c080
c16: mov   DWORD PTR [esi+64], edx
c17: mov   DWORD PTR ds:0x51c088, eax
c18: mov   eax, DWORD PTR [esp+228]
c19: add   eax, eax
c20: lea   eax, [eax+eax*2]
c21: imul  eax, eax, 4
c22: sub   esp, 0xd0
c23: add   esp, 0xd0
