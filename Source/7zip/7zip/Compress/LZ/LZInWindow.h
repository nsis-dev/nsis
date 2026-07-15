/*
 * LZInWindow.h
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
 */

#ifndef __LZ_IN_WINDOW_H
#define __LZ_IN_WINDOW_H

#include "../../IStream.h"

class CLZInWindow
{
  Byte *_bufferBase; // pointer to buffer with data
  ISequentialInStream *_stream;
  UInt32 _posLimit;  // offset (from _buffer) when new block reading must be done
  bool _streamEndWasReached; // if (true) then _streamPos shows real end of stream
  const Byte *_pointerToLastSafePosition;
protected:
  Byte  *_buffer;   // Pointer to virtual Buffer begin
  UInt32 _blockSize;  // Size of Allocated memory block
  UInt32 _pos;             // offset (from _buffer) of current byte
  UInt32 _keepSizeBefore;  // how many BYTEs must be kept in buffer before _pos
  UInt32 _keepSizeAfter;   // how many BYTEs must be kept buffer after _pos
  UInt32 _streamPos;   // offset (from _buffer) of first not read byte from Stream

  void MoveBlock();
  HRESULT ReadBlock();
  void Free();
public:
  CLZInWindow(): _bufferBase(0) {}
  virtual ~CLZInWindow() { Free(); }

  // keepSizeBefore + keepSizeAfter + keepSizeReserv < 4G)
  bool Create(UInt32 keepSizeBefore, UInt32 keepSizeAfter, UInt32 keepSizeReserv = (1<<17));

  void SetStream(ISequentialInStream *stream);
  HRESULT Init();
  // void ReleaseStream();

  Byte *GetBuffer() const { return _buffer; }

  const Byte *GetPointerToCurrentPos() const { return _buffer + _pos; }

  HRESULT MovePos()
  {
    _pos++;
    if (_pos > _posLimit)
    {
      const Byte *pointerToPosition = _buffer + _pos;
      if(pointerToPosition > _pointerToLastSafePosition)
        MoveBlock();
      return ReadBlock();
    }
    else
      return S_OK;
  }
  Byte GetIndexByte(Int32 index) const  {  return _buffer[(size_t)_pos + index]; }

  // index + limit have not to exceed _keepSizeAfter;
  // -2G <= index < 2G
  UInt32 GetMatchLen(Int32 index, UInt32 distance, UInt32 limit) const
  {  
    if(_streamEndWasReached)
      if ((_pos + index) + limit > _streamPos)
        limit = _streamPos - (_pos + index);
    distance++;
    const Byte *pby = _buffer + (size_t)_pos + index;
    UInt32 i;
    for(i = 0; i < limit && pby[i] == pby[(size_t)i - distance]; i++);
    return i;
  }

  UInt32 GetNumAvailableBytes() const { return _streamPos - _pos; }

  void ReduceOffsets(Int32 subValue)
  {
    _buffer += subValue;
    _posLimit -= subValue;
    _pos -= subValue;
    _streamPos -= subValue;
  }

  bool NeedMove(size_t numCheckBytes)
  {
    size_t reserv = _pointerToLastSafePosition - (_buffer + _pos);
    return (reserv <= numCheckBytes);
  }
};

#endif
