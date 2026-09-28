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

#include <stdlib.h>
#include "LZMADecode.h"

static void *lzmaAlloc(ISzAllocPtr allocator, size_t size)
{
  (void) allocator;
#ifdef _WIN32
  return GlobalAlloc(GPTR, size);
#else
  return malloc(size);
#endif
}

static void lzmaFree(ISzAllocPtr allocator, void *address)
{
  (void) allocator;
#ifdef _WIN32
  GlobalFree(address);
#else
  free(address);
#endif
}

void LZMACALL lzmaInit(lzma_stream *stream)
{
  if (!stream->allocated)
  {
    LzmaDec_Construct(&stream->decoder);
    stream->allocator.Alloc = lzmaAlloc;
    stream->allocator.Free = lzmaFree;
  }

  stream->next_in = NULL;
  stream->avail_in = 0;
  stream->next_out = NULL;
  stream->avail_out = 0;
  stream->totalOut = 0;
  stream->propertiesSize = 0;
  stream->initialized = 0;
  stream->finished = 0;
}

int LZMACALL lzmaDecode(lzma_stream *stream)
{
  SizeT inputSize;
  SizeT outputSize;
  SRes result;
  ELzmaStatus status;

  if (stream->finished)
    return LZMA_STREAM_END;

  while (stream->propertiesSize < LZMA_PROPS_SIZE && stream->avail_in)
  {
    stream->properties[stream->propertiesSize++] = *stream->next_in++;
    --stream->avail_in;
  }

  if (stream->propertiesSize != LZMA_PROPS_SIZE)
    return LZMA_OK;

  if (!stream->initialized)
  {
    result = LzmaDec_Allocate(&stream->decoder, stream->properties,
                              LZMA_PROPS_SIZE, &stream->allocator);
    if (result != SZ_OK)
      return LZMA_NOT_ENOUGH_MEM;
    stream->allocated = 1;
    stream->initialized = 1;
    LzmaDec_Init(&stream->decoder);
  }

  inputSize = stream->avail_in;
  outputSize = stream->avail_out;
  result = LzmaDec_DecodeToBuf(&stream->decoder, stream->next_out, &outputSize,
                               stream->next_in, &inputSize, LZMA_FINISH_ANY,
                               &status);
  stream->next_in += inputSize;
  stream->avail_in -= (UInt32) inputSize;
  stream->next_out += outputSize;
  stream->avail_out -= (UInt32) outputSize;
  stream->totalOut += (UInt32) outputSize;

  if (result != SZ_OK)
    return LZMA_DATA_ERROR;
  if (status == LZMA_STATUS_FINISHED_WITH_MARK)
  {
    stream->finished = 1;
    return LZMA_STREAM_END;
  }
  return LZMA_OK;
}
