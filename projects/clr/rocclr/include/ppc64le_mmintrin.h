#if defined(__powerpc__)
// Define _mm_pause here
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_pause(void) {
  unsigned long __PPR;

  __asm__ volatile("    mfppr   %0;"
                   "   or 31,31,31;"
                   "   isync;"
                   "   lwsync;"
                   "   isync;"
                   "   mtppr    %0;"
                   : "=r"(__PPR)
                   :
                   : "memory");
}

// end of _mm_pause
// Define _mm_sfence here
extern __inline void
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mm_sfence(void) {
  unsigned long __PPR;

  __asm__ volatile(" lwsync;"
                   : "=r"(__PPR)
                   :
                   : "memory");
}

static inline void _mm_mfence() {
    asm volatile("sync" ::: "memory");
}


static inline void _mm_clflush(const void* addr) {
    asm volatile("dcbf 0, %0" : : "r"(addr));
    asm volatile("sync");
}

#endif
