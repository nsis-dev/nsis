/*
 * utf.cpp
 * 
 * This file is a part of NSIS.
 * 
 * Copyright (C) 2011-2026 Anders Kjersem
 * 
 * Licensed under the zlib/libpng license (the "License");
 * you may not use this file except in compliance with the License.
 * 
 * Licence details can be found in the file COPYING.
 * 
 * This software is provided 'as-is', without any express or implied
 * warranty.
 *
 */

#include "utf.h"
#include "util.h"

#define FIX_ENDIAN_INT16LETOHOST_INPLACE FIX_ENDIAN_INT16_INPLACE

UINT StrLenUTF16(const void*str)
{
  return sizeof(wchar_t) == 2 ? (UINT)wcslen((wchar_t*)str) : InlineStrLenUTF16(str);
}

bool StrSetUTF16LE(tstring&dest, const void*src)
{
#ifndef _WIN32
  CharEncConv cec;
  if (!cec.Initialize(-1,NStreamEncoding::UTF16LE)) return false;
  src = (const void*) cec.Convert(src);
  if (!src) return false;
#endif
#ifdef C_ASSERT
  C_ASSERT(sizeof(tstring::value_type) >= sizeof(wchar_t));
#endif
  try { dest = (wchar_t*) src; } catch(...) { return false; }
  return true;
}

void UTF16InplaceEndianSwap(void*Buffer, UINT cch)
{
  unsigned short *p = (unsigned short *) Buffer;
  while(cch--) p[cch] = SWAP_ENDIAN_INT16(p[cch]);
}

inline UINT UTF8ToWC_Convert(LPCSTR StrU8,UINT cbU8,wchar_t*Buffer,UINT cchBuf)
{
#ifndef MB_ERR_INVALID_CHARS
  const UINT MB_ERR_INVALID_CHARS = 8; // MSDN says this flag is OK for CP_UTF8
#endif
  return (UINT) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,StrU8,cbU8,Buffer,cchBuf);
}
inline UINT UTF8ToWC_Prepare(LPCSTR StrU8,UINT cbU8)
{
  return UTF8ToWC_Convert(StrU8,cbU8,0,0);
}

UINT WCFromCodePoint(wchar_t*Dest,UINT cchDest,UINT32 CodPt)
{
  // Don't allow half surrogate pairs
  if (CodPt >= 0xd800 && CodPt <= 0xdfff) CodPt = UNICODE_REPLACEMENT_CHARACTER;
#ifdef _WIN32
  if (CodPt <= 0xffff && cchDest)
  {
    *Dest = (wchar_t) CodPt;
    return 1;
  }
  else if (cchDest >= 2)
  {
    const UINT32 lead_offset = 0xd800 - (0x10000 >> 10);
    UINT16 lead = lead_offset + (CodPt >> 10), trail = 0xdc00 + (CodPt & 0x3ff);
    Dest[0] = lead, Dest[1] = trail;
    return 2;
  }
  return 0;
#else
  iconvdescriptor iconvd;
  if (!iconvd.Open(nsis_iconv_get_wchar_t_code(),iconvdescriptor::GetHostEndianUCS4Code())) return 0;
  size_t inleft = 4;
  UINT cchW = iconvd.Convert(&CodPt,&inleft,Dest,cchDest*sizeof(wchar_t)) / sizeof(wchar_t);
  return !inleft ? cchW : 0;
#endif
}

wchar_t* DupWCFromBytes(void*Buffer,UINT cbBuffer,UINT32 SrcCP)
{
  /*\
  Converts a buffer encoded with SrcCP to a \0 terminated wchar_t malloc'ed buffer.
  Returns 0 on failure.
  \*/
  CharEncConv cec;
  cec.SetAllowOptimizedReturn(!!(SrcCP&DWCFBF_ALLOWOPTIMIZEDRETURN));
  if (!cec.Initialize(-1, SrcCP&=~DWCFBF_ALLOWOPTIMIZEDRETURN)) return 0;
  wchar_t *pWC = (wchar_t*) cec.Convert(Buffer, cbBuffer);
  return pWC ? (wchar_t*) cec.Detach() : 0;
}

BOOL CharEncConv::IsValidCodePage(UINT cp)
{
#ifdef _WIN32
  if (cp <= 1 || NStreamEncoding::IsUnicodeCodepage(cp)) return true; // Allow ACP/OEM/UTF*
#endif
  return cp < NStreamEncoding::CPCOUNT && ::IsValidCodePage(cp);
}
bool CharEncConv::Initialize(UINT32 ToEnc, UINT32 FromEnc)
{
  /*
  ** Initialize() with a Windows codepage or -1 for wchar_t
  */
  const WORD UTF32LE = NStreamEncoding::UTF32LE;
#ifdef _WIN32
  if (NStreamEncoding::UTF16LE == ToEnc) ToEnc = -1;
  if (NStreamEncoding::UTF16LE == FromEnc) FromEnc = -1;
#endif
  m_TE = (WORD) ToEnc, m_FE = (WORD) FromEnc;
  if ((UTF32LE|1) == (m_FE|1) || (UTF32LE|1) == (m_TE|1)) return false; // UTF-32 is a pain to deal with on Windows
#ifdef _WIN32
  return (IsWE(m_FE) || IsValidCodePage(FromEnc)) && (IsWE(m_TE) || IsValidCodePage(ToEnc));
#else
  const char* wchar_iconv_enc = nsis_iconv_get_wchar_t_code();
  char f[50], t[COUNTOF(f)];
  if (IsWE(m_FE)) strcpy(f, wchar_iconv_enc); else create_code_page_string(f, COUNTOF(f), m_FE);
  if (IsWE(m_TE)) strcpy(t, wchar_iconv_enc); else create_code_page_string(t, COUNTOF(t), m_TE);
  return m_TE == m_FE || m_iconvd.Open(t, f);
#endif
}
size_t CharEncConv::GuessOutputSize(size_t cbConverted)
{
  UINT cus = IsWE(m_TE) ? sizeof(wchar_t) : NStreamEncoding::GetCodeUnitSize(m_TE);
  size_t zt = 1, cch = cbConverted / cus;
  if (!cch) return 0;
  switch(cus)
  {
  case 1: zt = !((char*)m_Result)[--cch]; break;
  case 2: zt = !((WORD*)m_Result)[--cch]; break;
  case 4: zt = !((UINT32*)m_Result)[--cch]; break;
  }
  return (cch + (zt ? 0 : 1)) * cus;
}
void* CharEncConv::Convert(const void*Src, size_t cbSrc, size_t*cbOut)
{
  /*
  ** Convert() mallocs a buffer and converts Src (as m_FE) to m_TE.
  ** If cbSrc is -1 the size is calculated. cbOut can be NULL.
  ** Returns a pointer to the buffer on success or 0 on error.
  ** The buffer is valid until you call Close() or Convert().
  */
#ifdef _WIN32
  m_OptimizedReturn = false;
#endif
  if ((size_t)-1 == cbSrc)
  {
    UINT cus = IsWE(m_FE) ? sizeof(wchar_t) : NStreamEncoding::GetCodeUnitSize(m_FE);
    switch(cus)
    {
    case 1: cbSrc = strlen((char*)Src); break;
    case 2: cbSrc = StrLenUTF16(Src); break;
    //case 4: // No UTF-32 support...
    default:
      if (sizeof(wchar_t) > 2 && sizeof(wchar_t) == cus)
      {
        cbSrc = wcslen((wchar_t*)Src);
        break;
      }
      assert(0);
      return 0;
    }
    cbSrc = (cbSrc + 1) * cus;
  }
  if (m_FE == m_TE)
  {
#ifdef _WIN32
    if (m_AllowOptimizedReturn && IsWE(m_FE))
    {
      if (cbOut)
      {
        cbSrc /= sizeof(wchar_t);
        if (cbSrc && ((WORD*)Src)[--cbSrc]) ++cbSrc;
        *cbOut = cbSrc * sizeof(wchar_t);
      }
      m_OptimizedReturn = true;
      return (void*) (m_Result = (char*) Src);
    }
#endif
    char *p = (char*) realloc(m_Result, cbSrc + sizeof(UINT32));
    if (p) m_Result = p; else return 0;
    memcpy(p, Src, cbSrc);
    *((UINT32*)(p+cbSrc)) = 0;
    if (cbOut) *cbOut = GuessOutputSize(cbSrc);
    return m_Result;
  }
#ifdef _WIN32
  if (!IsWE(m_FE) && !IsWE(m_TE) && NStreamEncoding::UTF16BE != m_TE)
  {
    // We need a middle step: Src -> wchar_t -> Target
    CharEncConv cec;
    if (!cec.Initialize(-1, m_FE)) return 0;
    size_t cbConv;
    char *pWC = (char*) cec.Convert(Src, cbSrc, &cbConv);
    if (!pWC) return 0;
    this->m_FE = -1;
    return this->Convert(pWC, cbConv, cbOut);
  }
  if (IsWE(m_FE))
  {
    if (NStreamEncoding::UTF16BE == m_TE) goto l_swapUTF16;
    cbSrc /= sizeof(wchar_t);
    UINT cbDest = WideCharToMultiByte(m_TE, 0, (wchar_t*)Src, (int)cbSrc, 0, 0, 0, 0);
    char *p = (char*) realloc(m_Result, (cbDest + 1) * sizeof(char));
    if (p) m_Result = p; else return 0;
    if (!(cbDest = WideCharToMultiByte(m_TE, 0, (wchar_t*)Src, (int)cbSrc, p, (int)cbDest, 0, 0))) return 0;
    if (p[--cbDest]) p[++cbDest] = '\0'; // Always \0 terminate
    if (cbOut) *cbOut = cbDest; // cbOut never includes the \0 terminator
  }
  else
  {
    UINT cchDest;
    if (NStreamEncoding::UTF16BE == m_FE) // UTF16BE -> UTF16LE/wchar_t
    {
l_swapUTF16:
      char *p = (char*) realloc(m_Result, cbSrc + sizeof(wchar_t));
      if (p) m_Result = p; else return 0;
      memcpy(p, Src, cbSrc);
      cchDest = (UINT) (cbSrc / sizeof(wchar_t));
      UTF16InplaceEndianSwap(p, cchDest);
      if (!cchDest) *((WORD*)p) = 0, ++cchDest; // For "--cchDest" during \0 termination
    }
    else
    {
      cchDest = MultiByteToWideChar(m_FE, 0, (char*)Src, (int)cbSrc, 0, 0);
      char *p = (char*) realloc(m_Result, (cchDest + 1) * sizeof(wchar_t));
      if (p) m_Result = p; else return 0;
      if (!(cchDest = MultiByteToWideChar(m_FE, 0, (char*)Src, (int)cbSrc, (LPWSTR)p, (int)cchDest))) return 0;
      if (NStreamEncoding::UTF16BE == m_TE) UTF16InplaceEndianSwap(p, cchDest);
    }
    if (((WORD*)m_Result)[--cchDest]) ((WORD*)m_Result)[++cchDest] = '\0';
    if (cbOut) *cbOut = cchDest * sizeof(wchar_t);
  }
#else
  char *in = (char*) Src;
  size_t cbConv;
  if (!nsis_iconv_reallociconv(m_iconvd, &in, &cbSrc, &m_Result, cbConv)) return 0;
  if (cbOut) *cbOut = GuessOutputSize(cbConv);
#endif
  return m_Result;
}

#if !defined(_WIN32) || !defined(_UNICODE)
bool WCToUTF16LEHlpr::Create(const TCHAR*in, unsigned int codepage)
{
  CharEncConv cec;
  if (!cec.Initialize(NStreamEncoding::UTF16LE, -1)) return false;
  if (!cec.Convert(in)) return false;
  m_s = (unsigned short*) cec.Detach();
  return true;
}
#endif

UINT DetectUTFBOM(void*Buffer, UINT cb)
{
  unsigned char *b = (unsigned char*) Buffer;
  if (cb >= 3 && 0xef == b[0] && 0xbb == b[1] && 0xbf == b[2])
    return NStreamEncoding::UTF8;
  if (cb >= 2)
  {
    if (cb >= 4 && !b[0] && !b[1] && 0xfe == b[2] && 0xff == b[3])
      return NStreamEncoding::UTF32BE;
    if (0xff == b[0] && 0xfe == b[1])
      return (cb >= 4 && !b[2] && !b[3]) ? NStreamEncoding::UTF32LE : NStreamEncoding::UTF16LE;
    if (0xfe == b[0] && 0xff == b[1])
      return NStreamEncoding::UTF16BE;
  }
  return 0;
}

UINT DetectUTFBOM(FILE*strm)
{
  /*\
  Tries to detect a BOM at the current position in a stream.
  If a BOM is found it is eaten.
  NOTE: ungetc is only guaranteed to support 1 pushback, 
  lets hope no MBCS file starts with parts of a BOM.
  \*/
  const int b1 = fgetc(strm);
  if (EOF == b1) return 0;
  if (0xef == b1)
  {
    const int b2 = fgetc(strm);
    if (0xbb == b2)
    {
      const int b3 = fgetc(strm);
      if (0xbf == b3) return NStreamEncoding::UTF8;
      ungetc(b3,strm);
    }
    ungetc(b2,strm);
  }
  if (0xfe == b1 || 0xff == b1 || 0x00 == b1)
  {
    const int b2 = fgetc(strm), b3 = fgetc(strm);
    if (b1 && (b1^b2) == (0xfe^0xff))
    {
      if (0xff == b1 && 0 == b3)
      {
        const int b4 = fgetc(strm);
        if (0 == b4) return NStreamEncoding::UTF32LE;
        ungetc(b4,strm);
      }
      ungetc(b3,strm);
      return 0xff == b1 ? NStreamEncoding::UTF16LE : NStreamEncoding::UTF16BE;
    }
    if (0 == b1 && 0 == b2)
    {
      if (0xfe == b3)
      {
        const int b4 = fgetc(strm);
        if (0xff == b4) return NStreamEncoding::UTF32BE;
        ungetc(b4,strm);
      }
    }
    ungetc(b3,strm);
    ungetc(b2,strm);
  }
  ungetc(b1,strm);
  return 0;
}

static bool IsUTFEncodingString(const TCHAR*s, const TCHAR*suff)
{
  if (S7ChLwr(s[0]) == 'u' && S7ChLwr(s[1]) == 't' && S7ChLwr(s[2]) == 'f')
  {
    s += 3 + (s[3] == '-' || s[3] == '_');
    return !_tcsicmp(s, suff);
  }
  return false;
}

WORD GetEncodingFromString(const TCHAR*s, bool&BOM)
{
  BOM = false;
  if (!_tcsicmp(s,_T("ACP")))
    return NStreamEncoding::ACP;
  if (!_tcsicmp(s,_T("OEM")))
    return NStreamEncoding::OEMCP;
  if (IsUTFEncodingString(s, _T("8")))
    return NStreamEncoding::UTF8;
  if (IsUTFEncodingString(s, _T("8SIG")) || IsUTFEncodingString(s, _T("8BOM")))
    return (BOM = true, NStreamEncoding::UTF8);
  if (IsUTFEncodingString(s, _T("16LE")) || (IsUTFEncodingString(s, _T("16LEBOM")) && (BOM = true)))
    return NStreamEncoding::UTF16LE;
  if (IsUTFEncodingString(s, _T("16BE")) || (IsUTFEncodingString(s, _T("16BEBOM")) && (BOM = true)))
    return NStreamEncoding::UTF16LE;
  if (S7IsChEqualI('C', *s++) && S7IsChEqualI('P', *s++))
  {
    int cp = _tstoi(s);
    if (cp > 0 && cp < NStreamEncoding::CPCOUNT) return (WORD) cp;
  }
  return NStreamEncoding::UNKNOWN;
}

WORD GetEncodingFromString(const TCHAR*s)
{
  bool bom;
  return GetEncodingFromString(s, bom);
}

void NStreamEncoding::GetCPDisplayName(WORD CP, TCHAR*Buf)
{
  TCHAR mybuf[10];
  const TCHAR *p = mybuf;
  switch(CP)
  {
  case ACP: p = _T("ACP"); break;
  case OEMCP: p = _T("OEM"); break;
  case UTF16LE: p = _T("UTF16LE"); break;
  case UTF16BE: p = _T("UTF16BE"); break;
  case UTF32LE: p = _T("UTF32LE"); break;
  case UTF32BE: p = _T("UTF32BE"); break;
  case UTF8: p = _T("UTF8"); break;
  case BINARY: p = _T("BIN"); break;
  default: 
    _stprintf(mybuf,_T("CP%u"), CP);
    if (CP >= NStreamEncoding::CPCOUNT) p = _T("?");
  }
  _tcscpy(Buf, p);
}

bool NBaseStream::Attach(FILE*hFile, WORD enc, bool Seek /*= true*/)
{
  Close();
  m_hFile = hFile;
  if (!m_hFile) return false;
  if (!NStream::SetBinaryMode(m_hFile) && m_hFile != stdin) return false;
  WORD cp = 0;
  if (enc != NStreamEncoding::BINARY)
  {
    fpos_t pos;
    if (Seek && !fgetpos(m_hFile, &pos)) rewind(m_hFile); else Seek = false;
    cp = DetectUTFBOM(m_hFile);
    if (Seek)
    {
      fsetpos(m_hFile, &pos);
      if (cp) 
      {
        DetectUTFBOM(m_hFile); // parseScript() etc does not like the BOM, make sure we skip past it
        fseek(m_hFile, 0, SEEK_CUR); // Force intervening call to a file positioning function so writes works for files with BOM opened with "a+"
      }
    }
  }
  if (!cp) cp = enc;
  m_Enc.SafeSetCodepage(cp);
  return true;
}

bool NOStream::WriteString(const wchar_t*Str, size_t cch /*= -1*/)
{
  CharEncConv cec;
  if (!cec.Initialize(m_Enc.GetCodepage(), -1)) return false;
  cec.SetAllowOptimizedReturn(true);
  if ((size_t)-1 != cch) cch *= sizeof(wchar_t); // cec.Convert wants byte count
  size_t cbConv;
  char *p = (char*) cec.Convert(Str, cch, &cbConv);
  return p && WriteOctets(p, cbConv);
}
bool NOStream::WritePlatformNLString(const wchar_t*Str, size_t cch /*= -1*/)
{
#ifdef _WIN32
  size_t cch2 = 0, nlcount = 0;
  for(; cch2 < cch && Str[cch2]; ++cch2) if (L'\n' == Str[cch2]) ++nlcount;
  if (nlcount)
  {
    cch = cch2 + nlcount;
    wchar_t chPrev = 0, *buf = (wchar_t*) malloc(cch * sizeof(wchar_t));
    if (!buf) return false;
    for(size_t s = 0, d = 0; d < cch; ++s, ++d)
    {
      if (L'\n' == Str[s])
      {
        if (L'\r' != chPrev) buf[d++] = L'\r'; else --cch;
      }
      buf[d] = chPrev = Str[s];
    }
    bool retval = WriteString(buf, cch);
    free(buf);
    return retval;
  }
#endif
  return WriteString(Str, cch);
}

tstring NStreamLineReader::GetErrorMessage(UINT Error, const TCHAR*Filename, UINT Line)
{
  tstring msg;
  TCHAR buf[40];
  switch(Error)
  {
  case NStream::ERR_BUFFEROVERFLOW:
    msg = _T("Line too long");
    break;
  case NStream::ERR_IOERROR:
    msg = _T("I/O error"), Filename = 0;
    break;
  case NStream::ERR_UNSUPPORTEDENCODING:
    StreamEncoding().GetCPDisplayName(buf);
    msg = tstring(buf) + _T(" is not supported"), Filename = 0;
    break;
  default:
    msg = _T("Bad text encoding");
    break;
  }
  if (Filename)
  {
    const TCHAR *filelinesep = *Filename ? _T(":") : _T("");
    _stprintf(buf,_T("%") NPRIs _T("%u"),filelinesep,Line);
    msg = msg + _T(": ") + Filename + buf;
  }
  return msg + _T("\n");
}

UINT NStreamLineReader::ReadLine(wchar_t*Buffer, UINT cchBuf)
{
  /*\
  Reads from the associated stream until it finds a new-line or 
  the read fails (I/O error or EOF). It fails with ERR_BUFFEROVERFLOW if 
  cchBuf-1 wchar_t's are read without finding the end of the line.
  Buffer MUST be a valid pointer, it will be \0 terminated as long as cchBuf > 0.
  \*/
  if (!cchBuf) return NStream::ERR_BUFFEROVERFLOW;
#ifndef MB_ERR_INVALID_CHARS
  const UINT MB_ERR_INVALID_CHARS = 8;
#endif
  const UINT cchFullBuf = cchBuf;
  NIStream&strm = GetStream();
#ifndef _WIN32
  iconvdescriptor iconvd;
#endif

l_restart:
  if (StreamEncoding().IsUTF8())
  {
    for(;;)
    {
      BYTE cb = 0; // bytes in chU8 -1
      BYTE chU8[6];
      if (!strm.ReadOctet(&chU8[0])) goto l_ioerror;
      UINT cchWC;
#if defined(_WIN32) || defined(__CYGWIN__) // wchar_t==UTF16LE on Cygwin: www.mail-archive.com/bug-gnulib@gnu.org/msg21543.html
      // Fast path if wchar_t == UTF16 and in ASCII range
      if (chU8[0] <= 127 && sizeof(wchar_t) == 2)
      {
        cchWC = ++cb;
        if (cchBuf <= cchWC) goto l_lineoverflow;
        *Buffer = (wchar_t) chU8[0];
      }
      else
#endif
      {
        if (!UTF8_GetTrailCount(chU8[0], cb)) goto l_badutf;
        for(BYTE moreU8 = 0; moreU8 < cb;) 
        {
          BYTE b;
          if (!strm.ReadOctet(&b)) goto l_ioerror;
          if (0x80 != (0xC0 & b)) goto l_badutf; // chU8[1..n] must be 0b10xxxxxx
          chU8[++moreU8] = b;
        }
        ++cb;
        cchWC = UTF8ToWC_Prepare((LPCSTR)chU8,cb);
        if (!cchWC) goto l_badutf;
        if (cchBuf <= cchWC) goto l_lineoverflow;
        cchWC = UTF8ToWC_Convert((LPCSTR)chU8,cb,Buffer,cchWC);
      }
      if (CompleteLine(Buffer,cchWC,cchBuf,true)) goto l_success;
    }
  }
  else if (StreamEncoding().IsUTF16())
  {
#ifndef _WIN32
    if (!iconvd.Open(nsis_iconv_get_wchar_t_code(), iconvdescriptor::GetHostEndianUCS4Code())) goto l_unsupportedencoding;
#endif
    const bool utf16be = StreamEncoding().IsUTF16BE();
    unsigned short lead, trail, cchWC;
    for(;;)
    {
      if (!strm.ReadInt16(&lead)) goto l_ioerror;
      FIX_ENDIAN_INT16LETOHOST_INPLACE(lead);
      if (utf16be) lead = SWAP_ENDIAN_INT16(lead);
      if (IsTrailSurrogateUTF16(lead)) goto l_badutf;
      UINT32 codpt = lead;
      if (cchBuf <= 1) goto l_lineoverflow;
      Buffer[0] = lead, cchWC = 1;
      if (IsLeadSurrogateUTF16(lead))
      {
        if (!strm.ReadInt16(&trail)) goto l_ioerror;
        FIX_ENDIAN_INT16LETOHOST_INPLACE(trail);
        if (utf16be) trail = SWAP_ENDIAN_INT16(trail);
        if (!IsTrailSurrogateUTF16(trail)) goto l_badutf;
        codpt = CodePointFromUTF16SurrogatePair(lead,trail);
#ifdef _WIN32
        if (cchBuf <= 2) goto l_lineoverflow;
        Buffer[1] = trail, ++cchWC;
#endif
      }
      if (!IsValidUnicodeCodePoint(codpt)) goto l_badutf;
#ifndef _WIN32
      char tmpdest[8]; // Should be plenty of space to store one UCS4 character as wchar_t(s)
      size_t inleft = 4;
      cchWC = iconvd.Convert(&codpt,&inleft,tmpdest,sizeof(tmpdest)) / sizeof(wchar_t);
      if (!cchWC) goto l_badutf;
      if (cchBuf <= cchWC) goto l_lineoverflow;
      for (UINT i = cchWC; i;) --i, Buffer[i] = ((wchar_t*)tmpdest)[i];
#endif
      if (CompleteLine(Buffer,cchWC,cchBuf,true)) goto l_success;
    }
  }
  else if (StreamEncoding().IsUnicode())
  {
    goto l_unsupportedencoding;
  }
  else
  {
    const UINT cp = StreamEncoding().GetCodepage();
    UINT mbtowcflags = (cp < 50220 && cp != 42) ? MB_ERR_INVALID_CHARS : 0;
    for(;;) 
    {
      BYTE bufMB[2];
      BYTE mb = 0;
      if (!strm.ReadOctet(&bufMB[0])) goto l_ioerror;
      if (IsDBCSLeadByteEx(cp,bufMB[0]))
      {
        if (!strm.ReadOctet(&bufMB[++mb])) goto l_ioerror;
      }
      ++mb;
      UINT cchWC = MultiByteToWideChar(cp,mbtowcflags,(LPCSTR)bufMB,mb,0,0);
      if (!cchWC) goto l_badencoding;
      if (cchBuf <= cchWC) goto l_lineoverflow;
      cchWC = MultiByteToWideChar(cp,mbtowcflags,(LPCSTR)bufMB,mb,Buffer,cchWC);
      if (CompleteLine(Buffer,cchWC,cchBuf,false)) goto l_success;
    }
  }
l_ioerror:
  *Buffer = 0;
  return NStream::ERR_IOERROR;
l_lineoverflow:
  *Buffer = 0;
  return NStream::ERR_BUFFEROVERFLOW;
l_badutf:
l_badencoding:
  *Buffer = 0;
  return NStream::ERR_INVALIDENCODING;
l_unsupportedencoding:
  *Buffer = 0;
  return NStream::ERR_UNSUPPORTEDENCODING;
l_success:
  *Buffer = 0;
  // "Foo\r\nBar" is 2 and not 3 lines
  const wchar_t chThisNL = *--Buffer, chPrevNL = m_PrevNL;
  const bool onlyNL = ++cchBuf == cchFullBuf;
  m_PrevNL = chThisNL;
  if (onlyNL && (chPrevNL^chThisNL) == ('\r'^'\n'))
  {
    m_PrevNL = 0;
    goto l_restart; // Previous line was "Foo\r". This line was "\n", ignore it.
  }
  return NStream::OK;
}
