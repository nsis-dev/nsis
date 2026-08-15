#include "decompress.h"

#include <string.h> // for memset

#if _MSC_VER > 1200 // Hack to avoid extern "C" causing trouble with templates
#include <new>
#include <algorithm>
#include <iterator>
#include <memory>
#endif

#define EXEHEAD
#define NSIS_CONFIG_COMPRESSION_SUPPORT

#include "../Platform.h"

extern "C" {
#define NSIS_COMPRESS_USE_BZIP2
#include "../bzip2/bzlib.h"
#undef NSIS_COMPRESS_USE_BZIP2

#define NSIS_COMPRESS_USE_LZMA
#include "../7zip/LZMADecode.h"
#undef NSIS_COMPRESS_USE_LZMA

#define NSIS_COMPRESS_USE_ZLIB
#include "../zlib/ZLIB.H"
#undef NSIS_COMPRESS_USE_ZLIB

#define ZSTD_STATIC_LINKING_ONLY
#include "../zstd/lib/zstd.h"
#include "../zstd/lib/zstd_errors.h"
}

#define DECOMPRESSOR(name, type, initf, dec, u)   \
  name::name() {                                  \
    vs = new type;                                \
    memset(vs, 0, sizeof(type));                  \
  }                                               \
                                                  \
  name::~name() {                                 \
    delete (type *) vs;                           \
    vs = 0;                                       \
  }                                               \
                                                  \
  void name::setNextIn(void *buffer, int size) {  \
    type *s = (type *) vs;                        \
    s->next_in = (u *) buffer;                    \
    s->avail_in = size;                           \
  }                                               \
                                                  \
  void name::setNextOut(void *buffer, int size) { \
    type *s = (type *) vs;                        \
    s->next_out = (u *) buffer;                   \
    s->avail_out = size;                          \
  }                                               \
                                                  \
  int name::getAvailOut() {                       \
    type *s = (type *) vs;                        \
    return s->avail_out;                          \
  }                                               \
                                                  \
  void name::init() {                             \
    type *s = (type *) vs;                        \
    initf(s);                                     \
  }                                               \
                                                  \
  int name::decompress() {                        \
    type *s = (type *) vs;                        \
    return dec(s);                                \
  }

DECOMPRESSOR(lzmaDecompressor, lzma_stream, lzmaInit, lzmaDecode, unsigned char);
DECOMPRESSOR(bzip2Decompressor, DState, BZ2_bzDecompressInit, BZ2_bzDecompress, unsigned char);
DECOMPRESSOR(zlibDecompressor, z_stream, inflateReset, inflate, unsigned char);

zstdDecompressor::zstdDecompressor() { ctx = ZSTD_createDStream(); }

zstdDecompressor::~zstdDecompressor() { ZSTD_freeDStream(ctx); }

void zstdDecompressor::setNextIn(void *buffer, int size)
{
  input.src = buffer;
  input.size = size;
  input.pos = 0;
}

void zstdDecompressor::setNextOut(void *buffer, int size)
{
  output.dst = buffer;
  output.size = size;
  output.pos = 0;
}

int zstdDecompressor::getAvailOut() { return output.size - output.pos; }

void zstdDecompressor::init()
{ 
  ZSTD_DCtx_reset(ctx, ZSTD_reset_session_only);
  ZSTD_DCtx_setParameter(ctx, ZSTD_d_format, ZSTD_f_zstd1_magicless);
}

int zstdDecompressor::decompress()
{
  size_t ret = ZSTD_decompressStream(ctx, &output, &input);
  if (ret == 0) return 1;
  if (ZSTD_isError(ret)) return -ZSTD_getErrorCode(ret);
  return 0;
}
