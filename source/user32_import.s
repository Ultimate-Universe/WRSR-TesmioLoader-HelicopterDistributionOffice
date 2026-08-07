// Minimal conventional PE import descriptor for USER32!GetAsyncKeyState.
// Kept in source so the no-CRT DLL build is reproducible without a Windows SDK
// import library on the build host. The resulting PE has a normal USER32 import.
        .section .idata$2,"dr"
        .p2align 2
        .globl __IMPORT_DESCRIPTOR_USER32
__IMPORT_DESCRIPTOR_USER32:
        .long .Lilt@IMGREL
        .long 0
        .long 0
        .long .Ldllname@IMGREL
        .long .Liat@IMGREL

        .section .idata$3,"dr"
        .p2align 2
        .zero 20

        .section .idata$4,"dr"
        .p2align 3
.Lilt:
        .long .Lhint@IMGREL
        .long 0
        .quad 0

        .section .idata$5,"drw"
        .p2align 3
        .globl __imp_GetAsyncKeyState
.Liat:
__imp_GetAsyncKeyState:
        .long .Lhint@IMGREL
        .long 0
        .quad 0

        .section .idata$6,"dr"
        .p2align 1
.Lhint:
        .short 0
        .asciz "GetAsyncKeyState"
.Ldllname:
        .asciz "USER32.dll"
