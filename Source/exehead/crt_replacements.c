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

#if defined(_MSC_VER)
  #pragma function(memcpy)
#endif
void *memcpy(void *dest, const void *src, size_t count)
{
  char *dest8 = (char *)dest;
  const char *src8 = (const char *)src;
  while (count--) *dest8++ = *src8++;
  return dest;
}

#ifndef _MSC_VER
void *memset(void *mem, int c, size_t len)
{
  char *p = (char *)mem;
  while (len-- > 0) *p++ = (char)c;
  return mem;
}
#endif

#if defined(_MSC_VER)
  #pragma function(memmove)
#endif
void *memmove(void *dest, const void *src, unsigned int n)
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
  return GlobalAlloc(GPTR, size);
}

void *calloc(size_t num, size_t size)
{
  void *mem = malloc(num * size);
  return memset(mem, 0, num * size);
}

void free(void *ptr)
{
  GlobalFree(ptr);
}

void *realloc(void *ptr, size_t size)
{
  void *newmem = malloc(size);
  if (newmem && ptr)
  {
    memcpy(newmem, ptr, size);
    free(ptr);
  }
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
  return (val << shift) | (val >> (32 - shift));
}

unsigned __int64 nsis_rotl64(unsigned __int64 val, int shift) {
  unsigned int p[2];
  p[0] = (unsigned int)(val);
  p[1] = (unsigned int)(val >> 32);
  shift &= 63;
  if (shift > 0 && shift < 32) {
    unsigned int t = p[0] >> (32 - shift);
    p[0] = (p[0] << shift) | (p[1] >> (32 - shift));
    p[1] = (p[1] << shift) | t;
  } else if (shift >= 32) {
    p[1] = p[0] << (shift - 32);
    p[0] = 0;
  }
  return ((unsigned __int64)p[1] << 32) | p[0];
}

unsigned int nsis_byteswap_ulong(unsigned int val) {
  return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
         ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF);
}

unsigned __int64 nsis_byteswap_uint64(unsigned __int64 val) {
  unsigned int v[2];
  v[0] = nsis_byteswap_ulong((unsigned int)(val));
  v[1] = nsis_byteswap_ulong((unsigned int)(val >> 32));
  return ((unsigned __int64)v[1] << 32) | v[0];
}

unsigned __int64 nsis_allmul(unsigned __int64 a, unsigned __int64 b) {
  unsigned int al = (unsigned int)(a), ah = (unsigned int)(a >> 32);
  unsigned int bl = (unsigned int)(b), bh = (unsigned int)(b >> 32);
  unsigned int rl = al * bl;
  unsigned int rh = ah * bl + al * bh;
  return ((unsigned __int64)rh << 32) | rl;
}

unsigned __int64 nsis_allshl(unsigned __int64 val, int shift) {
  unsigned int p[2];
  p[0] = (unsigned int)(val);
  p[1] = (unsigned int)(val >> 32);
  shift &= 63;
  if (shift > 0) {
    if (shift < 32) {
      unsigned int t = p[0] >> (32 - shift);
      p[0] = (p[0] << shift) | (p[1] >> (32 - shift));
      p[1] = (p[1] << shift) | t;
    } else {
      p[1] = p[0] << (shift - 32);
      p[0] = 0;
    }
  }
  return ((unsigned __int64)p[1] << 32) | p[0];
}

#endif /* _MSC_VER */
