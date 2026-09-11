/*
 * crt_replacements.c
 * 
 * This file is a part of NSIS.
 * 
 * Copyright (C) 1999-2026 Nullsoft and Contributors
 * 
 * Licensed under the zlib/libpng license (the "License");
 * you may not use this file except in compliance with the License.
 * 
 * Licence details can be found in the file COPYING.
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty.
 *
 * Minimal CRT replacements for exehead, which is compiled with /nodefaultlib.
 * These are needed by the zstd decompression library.
 */

#include "../Platform.h"
#include <stddef.h>

#if defined(_MSC_VER)
  #pragma function(memcpy)
  #pragma function(memset)
  #pragma function(memmove)
#endif

void *memcpy(void *dest, const void *src, size_t count)
{
  char *d = (char *)dest;
  const char *s = (const char *)src;
  /* Copy word-at-a-time when both sides are aligned; zstd decompression
   * is memcpy-heavy so the old byte loop cost real extraction speed. */
  if (count >= 4 * sizeof(size_t)
      && (((size_t)d & (sizeof(size_t) - 1)) == 0)
      && (((size_t)s & (sizeof(size_t) - 1)) == 0))
  {
    size_t *dw = (size_t *)d;
    const size_t *sw = (const size_t *)s;
    size_t n = count / sizeof(size_t);
    while (n--) *dw++ = *sw++;
    d = (char *)dw;
    s = (const char *)sw;
    count %= sizeof(size_t);
  }
  while (count--) *d++ = *s++;
  return dest;
}

void *memset(void *mem, int c, size_t len)
{
  char *p = (char *)mem;
  while (len--) *p++ = (char)c;
  return mem;
}

void *memmove(void *dest, const void *src, size_t n)
{
  char *pcDstn = (char *)dest;
  const char *pcSource = (const char *)src;
  if ((pcSource < pcDstn) && (pcDstn < pcSource + n))
    for (pcDstn += n, pcSource += n; n--;) *--pcDstn = *--pcSource;
  else
    while (n--) *pcDstn++ = *pcSource++;
  return dest;
}

void *malloc(size_t size)
{
  if (size == 0) size = 1;
  return GlobalAlloc(GPTR, size);
}

void *calloc(size_t num, size_t size)
{
  void *mem;
  if (num != 0 && size > (size_t)-1 / num) return NULL; /* overflow */
  mem = malloc(num * size);
  if (!mem) return NULL;
  return memset(mem, 0, num * size);
}

void free(void *ptr)
{
  if (ptr) GlobalFree(ptr);
}

void *realloc(void *ptr, size_t size)
{
  void *newmem;
  size_t oldsize, copysize;
  if (!ptr) return malloc(size);
  if (size == 0) { free(ptr); return NULL; }
  newmem = malloc(size);
  if (!newmem) return NULL; /* keep original block on failure */
  oldsize = (size_t)GlobalSize(ptr);
  copysize = oldsize < size ? oldsize : size;
  if (copysize) memcpy(newmem, ptr, copysize);
  free(ptr);
  return newmem;
}

#if defined(_MSC_VER)

/*
 * MSVC 32-bit /nodefaultlib: the compiler generates calls to runtime helper
 * functions (__rotl, __rotl64, __allmul, __allshl, __byteswap_ulong,
 * __byteswap_uint64) that are normally provided by the CRT. Defining these
 * by their exact names fails (C2169: intrinsic function, cannot be defined)
 * because the compiler treats them as built-ins. Instead we define them under
 * different names (nsis_*) and use /alternatename to tell the linker to resolve
 * the unresolved __* symbols to our _nsis_* implementations.
 *
 * On x86, C __cdecl functions get a leading underscore in the object file,
 * so nsis_rotl becomes _nsis_rotl at link time.
 */
#pragma comment(linker, "/alternatename:__rotl=_nsis_rotl")
#pragma comment(linker, "/alternatename:__rotl64=_nsis_rotl64")
#pragma comment(linker, "/alternatename:__byteswap_ulong=_nsis_byteswap_ulong")
#pragma comment(linker, "/alternatename:__byteswap_uint64=_nsis_byteswap_uint64")
#pragma comment(linker, "/alternatename:__allmul=_nsis_allmul")
#pragma comment(linker, "/alternatename:__allshl=_nsis_allshl")

unsigned int nsis_rotl(unsigned int val, int shift) {
  shift &= 31;
  if (shift == 0) return val;
  return (val << shift) | (val >> (32 - shift));
}

unsigned __int64 nsis_rotl64(unsigned __int64 val, int shift) {
  unsigned int lo = (unsigned int)(val);
  unsigned int hi = (unsigned int)(val >> 32);
  shift &= 63;
  if (shift == 0) return val;
  if (shift >= 32) {
    /* Rotate by 32 is a half swap; fold it in then rotate the rest. */
    unsigned int t = lo; lo = hi; hi = t;
    shift -= 32;
    if (shift == 0) return ((unsigned __int64)hi << 32) | lo;
  }
  {
    unsigned int t = lo >> (32 - shift);
    lo = (lo << shift) | (hi >> (32 - shift));
    hi = (hi << shift) | t;
  }
  return ((unsigned __int64)hi << 32) | lo;
}

unsigned int nsis_byteswap_ulong(unsigned int val) {
  return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
         ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF);
}

unsigned __int64 nsis_byteswap_uint64(unsigned __int64 val) {
  unsigned int lo = nsis_byteswap_ulong((unsigned int)(val));
  unsigned int hi = nsis_byteswap_ulong((unsigned int)(val >> 32));
  return ((unsigned __int64)lo << 32) | hi;
}

unsigned __int64 nsis_allmul(unsigned __int64 a, unsigned __int64 b) {
  /* 64-bit multiply without using a 64-bit multiply (which would recurse
   * into __allmul on 32-bit MSVC). 16-bit schoolbook multiply. */
  unsigned int al0 = (unsigned int)(a) & 0xFFFF;
  unsigned int al1 = (unsigned int)(a) >> 16;
  unsigned int bl0 = (unsigned int)(b) & 0xFFFF;
  unsigned int bl1 = (unsigned int)(b) >> 16;
  unsigned int lo, mid, m1, m2, rl, rh, t;
  unsigned int carry_mid, carry_t;
  lo = al0 * bl0;
  m1 = al0 * bl1;
  m2 = al1 * bl0;
  mid = m1 + m2;
  carry_mid = (mid < m1) ? 1u : 0u;
  /* rl = lo + (mid_low << 16) */
  t = (lo >> 16) + (mid & 0xFFFF);
  carry_t = t >> 16;
  rl = (lo & 0xFFFF) | ((t & 0xFFFF) << 16);
  /* high 32 bits: sub-product high + cross terms (a_lo*b_hi + a_hi*b_lo) */
  rh = al1 * bl1 + ((mid >> 16) & 0xFFFF) + (carry_mid << 16) + carry_t
     + (unsigned int)(a) * (unsigned int)(b >> 32)
     + (unsigned int)(a >> 32) * (unsigned int)(b);
  return ((unsigned __int64)rh << 32) | rl;
}

unsigned __int64 nsis_allshl(unsigned __int64 val, int shift) {
  unsigned int lo = (unsigned int)(val);
  unsigned int hi = (unsigned int)(val >> 32);
  shift &= 63;
  if (shift == 0) return val;
  if (shift >= 32) {
    hi = lo << (shift - 32);
    lo = 0;
  } else {
    hi = (hi << shift) | (lo >> (32 - shift));
    lo = lo << shift;
  }
  return ((unsigned __int64)hi << 32) | lo;
}

#endif /* _MSC_VER */
