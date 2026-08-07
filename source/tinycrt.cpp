// Minimal no-CRT memory helpers used by the release build.
// These are linked into the DLL because /NODEFAULTLIB is used intentionally.
typedef __SIZE_TYPE__ usize;
extern "C" __declspec(noinline) void* memcpy(void* dst, const void* src, usize n) {
    volatile unsigned char* d=(volatile unsigned char*)dst;
    const volatile unsigned char* s=(const volatile unsigned char*)src;
    for (usize i=0;i<n;++i) d[i]=s[i];
    return dst;
}
extern "C" __declspec(noinline) void* memmove(void* dst, const void* src, usize n) {
    volatile unsigned char* d=(volatile unsigned char*)dst;
    const volatile unsigned char* s=(const volatile unsigned char*)src;
    if (d < s) { for (usize i=0;i<n;++i) d[i]=s[i]; }
    else if (d > s) { for (usize i=n;i!=0;--i) d[i-1]=s[i-1]; }
    return dst;
}
extern "C" __declspec(noinline) void* memset(void* dst, int c, usize n) {
    volatile unsigned char* d=(volatile unsigned char*)dst;
    unsigned char v=(unsigned char)c;
    for (usize i=0;i<n;++i) d[i]=v;
    return dst;
}
