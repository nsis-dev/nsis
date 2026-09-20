/*
 * LZMADecode.c
 * 
 * This file is a part of LZMA compression module for NSIS.
 * 
 * Original LZMA SDK Copyright (C) 1999-2006 Igor Pavlov
 * Modifications Copyright (C) 2003-2026 Amir Szekely <kichik@netvision.net.il>
 * 
 * Licensed under the Common Public License version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * 
 * Licence details can be found in the file COPYING.
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty.
 *
 * Reviewed for Unicode support by Jim Park -- 08/24/2007
 */

#ifndef __LZMADECODE_H
#define __LZMADECODE_H

#include "../Platform.h"
#include "LzmaSDK/LzmaDec.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LZMACALL
# define LZMACALL
#endif

#define LZMA_STREAM_END 1
#define LZMA_OK 0
#define LZMA_DATA_ERROR -1
#define LZMA_NOT_ENOUGH_MEM -1

typedef struct
{
  Byte *next_in;
  UInt32 avail_in;
  Byte *next_out;
  UInt32 avail_out;
  UInt32 totalOut;

  CLzmaDec decoder;
  ISzAlloc allocator;
  Byte properties[LZMA_PROPS_SIZE];
  unsigned propertiesSize;
  int allocated;
  int initialized;
  int finished;
} lzma_stream;

void LZMACALL lzmaInit(lzma_stream *stream);
int LZMACALL lzmaDecode(lzma_stream *stream);

#ifdef __cplusplus
}
#endif

#endif
