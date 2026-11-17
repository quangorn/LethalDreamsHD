.intel_syntax noprefix
# Cave для exepatch_video43.py: роликиDirectShow в 4:3 по центру.
# Сайт: FUN_0049a1a0, вызов IVideoWindow::SetWindowPosition (vtable+0x9c), VA 0x49a322.
# На входе esp тот же, что на сайте; rc = [esp+8] (left@8, top@0xc, right@0x10, bottom@0x14).
# Аргументы вызова: this, left, top, width, height; порядок push: height, width, top, left, this.
# 4:3 считается как width = 4*height/3 (все целевые высоты делятся на 3),
# left = (right - width)/2. При 4:3-окне (right == 4h/3) left = 0 и width = right — identity.
# Последняя jmp требует relocation (exepatch_video43.py ставит rel32 на 0x49a33e).
cave43:
    mov  ecx, DWORD PTR [esp+0x14]
    mov  eax, ecx
    imul eax, eax, 4
    cdq
    mov  edx, 3
    idiv edx
    mov  edx, DWORD PTR [esp+0x10]
    sub  edx, eax
    shr  edx, 1
    push ecx
    push eax
    push 0
    push edx
    mov  eax, DWORD PTR [0x51d5f4]
    push eax
    mov  edx, DWORD PTR [eax]
    call DWORD PTR [edx+0x9c]
    jmp  0
