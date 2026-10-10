/*
 * util.cpp
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
 */

#include "Platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include "tchar.h"
#include "exehead/fileform.h"
#include "util.h"
#include "strlist.h"
#include "winchar.h"
#include "utf.h"
#include "BinInterop.h"

#ifndef _WIN32
#  include <ctype.h>
#  include <unistd.h> // for close(2)
#  include <fcntl.h> // for open(2)
#  include <iconv.h>
#  include <locale.h>
#  include <stdlib.h>
#  include <limits.h>
#  ifdef _UNICODE
#    include <wchar.h>
#  endif
#endif

#ifdef __APPLE__
namespace Apple { // defines struct section
#  include <mach-o/dyld.h> // for _NSGetExecutablePath
};
#  define FALSE 0 // bugs #1851365, #2497290
#  define TRUE 1
#  include <sys/param.h> // for MAXPATHLEN
#endif

#include <cassert> // for assert
#include <algorithm>
#include <stdexcept>
#include <errno.h>

using namespace std;

extern int g_display_errors;
extern FILE *g_output, *g_errout;


#ifdef _WIN32
static char* CreateMappedFileView(LPCTSTR Path, DWORD FAccess, DWORD FShare, DWORD FMode, DWORD PProtect, DWORD MAccess, size_t &FSize)
{
  char *pView = NULL, restoreGLE = false, validSize;
  HANDLE hFile = CreateFile(Path, FAccess, FShare, NULL, FMode, 0, NULL);
  if (hFile == INVALID_HANDLE_VALUE) return pView;
  ULARGE_INTEGER fs;
  validSize = GetFileSize64(hFile, fs) && (sizeof(size_t) >= 8 || !fs.HighPart);
  FSize = sizeof(size_t) >= 8 ? (size_t) fs.QuadPart : fs.LowPart;
  HANDLE hMap = validSize ? CreateFileMapping(hFile, NULL, PProtect, 0, 0, NULL) : INVALID_HANDLE_VALUE;
  if (hMap != INVALID_HANDLE_VALUE)
  {
    CloseHandle(hFile);
    if ((pView = (char*) MapViewOfFile(hMap, MAccess, 0, 0, 0)))
    {
      CloseHandle(hMap);
    }
    else
    {
      DWORD error = restoreGLE ? GetLastError() : 0;
      CloseHandle(hMap);
      if (restoreGLE) SetLastError(error);
    }
  }
  else
  {
      DWORD error = restoreGLE ? GetLastError() : 0;
      CloseHandle(hFile);
      if (restoreGLE) SetLastError(error);
  }
  return pView;
}
#else
#include <sys/stat.h>
#include <sys/mman.h>
static char* CreateMappedFileView(const TCHAR *Path, const char *FMode, int PProtect, int MFlags, size_t &FSize)
{
  char *pView = NULL;
  FILE *pFile = FOPEN(Path, FMode);
  if (pFile)
  {
    struct stat fs;
    int fd = fileno(pFile), toobig;
    if (-1 != fd && 0 == fstat(fd, &fs))
    {
      FSize = (size_t) fs.st_size, toobig = sizeof(size_t) < sizeof(fs.st_size) && (INT64)FSize != fs.st_size;
      void *p = !toobig ? mmap(NULL, FSize, PProtect, MFlags, fd, 0) : MAP_FAILED;
      if (p != MAP_FAILED) pView = (char*) p;
    }
    fclose(pFile);
  }
  return pView;
}
#endif //~ _WIN32


double my_wtof(const wchar_t *str) 
{
  char buf[100];
  WideCharToMultiByte(0,0,str,-1,buf,100,0,0);
  return atof(buf);
}

size_t my_strncpy(TCHAR*Dest, const TCHAR*Src, size_t cchMax)
{
  // Dest and Src must be valid, Dest is always \0 terminated.
  // Returns number of TCHARs copied to Dest (not counting \0); min(strlen(Src),cchMax-1).
  size_t cch = 0;
  if (cchMax) for (TCHAR c; --cchMax;) { if (!(c = Src[cch])) break; Dest[cch++] = c; }
  Dest[cch] = _T('\0');
  return cch;
}

size_t my_strftime(TCHAR *s, size_t max, const TCHAR  *fmt, const struct tm *tm)
{
  return _tcsftime(s, max, fmt, tm);
}

// Returns 0 if everything is OK
// Returns -1 if can't find the file
// Returns -2 if the file is an invalid bitmap
// Returns -3 if the size doesn't match
// Returns -4 if the bpp doesn't match
int update_bitmap(CResourceEditor* re, WORD id, const TCHAR* filename, int width/*=0*/, int height/*=0*/, int maxbpp/*=0*/) {
  FILE *f = FOPEN(filename, ("rb"));
  if (!f) return -1;
  signed char hdr[14+124], retval = -2;
  size_t size = fread(hdr, 1, sizeof(hdr), f);
  GENERICIMAGEINFO info;
  if (IsBMPFile(hdr, size, &info) && 0 == fseek(f, 0, SEEK_SET) && LoadImageCanLoadFileFromResource(hdr, size))
  {
    if ((width && width != (int) info.Width) || (height && height != (int) info.Height))
      retval = -3;
    else if (maxbpp && maxbpp < info.BPP)
      retval = -4;
    else if (re->UpdateResource(RT_BITMAP, id, NSIS_DEFAULT_LANG, f, CResourceEditor::TM_AUTO))
      retval = 0;
  }
  fclose(f);
  return retval;
}

tstring make_friendly_resource_path(const TCHAR*rt, const TCHAR*rn, LANGID rl)
{
  tstring s = _T("");
  TCHAR buf[42], sep = _T('\\');
  s += IS_INTRESOURCE(rt) ? (wsprintf(buf, _T("#%d"), (int)(size_t) rt), buf) : rt;
  s += sep;
  s += IS_INTRESOURCE(rn) ? (wsprintf(buf, _T("#%d"), (int)(size_t) rn), buf) : rn;
  s += sep;
  if (rl == CResourceEditor::ALLLANGID)
    s += _T("All");
  else
    s += (wsprintf(buf, _T("%d"), (int)(size_t) rl), buf);
  return s;
}

#ifndef _WIN32
void PathConvertWinToPosix(char*p);

BOOL IsDBCSLeadByteEx(unsigned int CodePage, unsigned char TestChar)
{
  if (CP_UTF8 == CodePage) return false; //blogs.msdn.com/b/michkap/archive/2007/04/19/2190207.aspx
  const char buf[] = {(char)TestChar, 'a', 'b', '\0'}; // Untested and probably not the best way to do this!
  return CharNextExA(CodePage, buf, 0) > &buf[1];
}
TCHAR *CharPrev(const TCHAR *s, const TCHAR *p) {
  if (!s || !p || p < s)
    return NULL;
  while (*s) {
    TCHAR *n = CharNext(s);
    if (n >= p)
      break;
    s = n;
  }
  return const_cast<TCHAR*>(s);
}

char *CharNextA(const char *s) {
  int l = 0, mbl;
  if (s && *s)
    mbl = mblen(s, MB_CUR_MAX), l = max(1, mbl);
  return const_cast<char*>(s + l);
}

wchar_t *CharNextW(const wchar_t *s) {
  if (sizeof(*s) == 2 && IsLeadSurrogateUTF16(*s)) ++s; //BUGBUG: This assumes that 16bit wchar_t == UTF16
  // else if (...) BUGBUG: Is this the best we can do? What about combining characters/diacritics etc?
  return const_cast<wchar_t*>(s + 1);
}

char *CharNextExA(WORD codepage, const char *s, int flags) {
  // blogs.msdn.com/b/michkap/archive/2007/04/19/2190207.aspx implies that 
  // CharNextExA uses IsDBCSLeadByteEx, should we do the same?
  const char* orglocct = NSISRT_setlocale_wincp(LC_CTYPE, codepage), *np;
  int len = mblen(s, strlen(s));
  if (len > 0) np = s + len; else np = s + 1;
  setlocale(LC_CTYPE, orglocct);
  return const_cast<char*>(np);
}

int wsprintf(TCHAR *s, const TCHAR *format, ...) {
  va_list val;
  va_start(val, format);
  int res = _vsntprintf(s, 1024+1, format, val);
  if (res >= 0) s[res] = _T('\0');
  va_end(val);
  return res;
}

static char g_nrt_iconv_narrowlocbuf[50], *g_nrt_iconv_narrowloc = 0;
#define setlocale_ACP(cat) setlocale((cat), "")
#define iconv_ACP g_nrt_iconv_narrowloc
#define setlocale_OEM(cat) NSISRT_setlocale_wincp((cat), 1252)
#define iconv_OEM "CP1252"

#ifdef HAVE_LANGINFO_H // BUGBUG: scons needs to check for HAVE_LANGINFO_H and HAVE_NL_LANGINFO support?
#include <langinfo.h>
#endif
bool NSISRT_Initialize() // Init function for POSIX
{
  iconvdescriptor id;
  const char* wchar_iconv_enc = nsis_iconv_get_wchar_t_code();
  g_nrt_iconv_narrowloc = const_cast<char*>(""); // Use "" and not "char", "char" is a GNU extension?
  if (!id.Open(wchar_iconv_enc, g_nrt_iconv_narrowloc)) 
  {
    unsigned int cchmax = COUNTOF(g_nrt_iconv_narrowlocbuf);
    const char *tmp = "";
#ifdef HAVE_NL_LANGINFO
    tmp = nl_langinfo(CODESET); // TODO: Use libcharset or locale_charset if possible
    if (strlen(tmp) >= cchmax) tmp = "";
#endif
    strcpy(g_nrt_iconv_narrowloc = g_nrt_iconv_narrowlocbuf, tmp);
    if (!id.Open(wchar_iconv_enc, g_nrt_iconv_narrowloc))
    {
      // Unable to determine the iconv narrow string code, UTF-8 is the best we can do
      create_code_page_string(g_nrt_iconv_narrowloc, cchmax, CP_UTF8);
    }
  }
  return nsis_iconv_get_host_endian_ucs4_code() && IsValidCodePage(NSISRT_GetASCIICodepage());
}

const char* NSISRT_setlocale_wincp(int cat, unsigned int cp)
{
  if (cp <= 1) return CP_ACP == cp ? setlocale_ACP(cat) : setlocale_OEM(cat);
  char buf[40];
  const char *p, *p1 = 0, *p2 = 0;
  snprintf(buf, COUNTOF(buf), ".CP%u", cp);
  p = setlocale(cat, &buf[1]); // "CP%u"
  if (!p) p = setlocale(cat, buf); // ".CP%u" this is the format used by MSVCRT?
  if (!p)
  {
    switch(cp) // This list is probably incomplete
    {
    case 932: p1 = "SJIS"; break; // Not an exact match but CP932 already failed, this is the best we can do
    case 936: p1 = "EUCCN", p2 = "euc-CN"; break;
    case 949: p1 = "EUCKR", p2 = "euc-KR"; break;
    case 950: p1 = "Big5"; break;
    case 20866: p1 = "KOI8-R"; break;
    case 20932: p1 = "EUCJP", p2 = "euc-JP"; break;
    case 65001: p1 = "UTF-8", p2 = "utf8"; break;
    default:
      if (cp >= 28591 && cp <= 28599) snprintf(buf, COUNTOF(buf), "ISO-8859-%u", cp - 28590), p1 = buf;
      if (cp == 28603 || cp == 28605) snprintf(buf, COUNTOF(buf), "ISO-8859-%u", cp - 28600 + 10), p1 = buf;
    }
    if (!(p = setlocale(cat, p1))) p = setlocale(cat, p2);
  }
  return p;
}

wchar_t* NSISRT_mbtowc(const char *Str)
{
  const char *orglocct = setlocale(LC_CTYPE, "");
  wchar_t *d = 0;
  const char *s = Str;
  size_t cch = mbsrtowcs(0, &s, 0, 0);
  if ((size_t)-1 != cch && (d = (wchar_t*) malloc(++cch * sizeof(wchar_t))))
  {
    cch = mbsrtowcs(d, &Str, cch, 0);
    if ((size_t) -1 == cch) { NSISRT_free(d); d = 0; }
  }
  if (!errno) errno = ENOMEM;
  setlocale(LC_CTYPE, orglocct);
  return d;
}
char* NSISRT_wctomb(const wchar_t *Str)
{
  const char *orglocct = setlocale(LC_CTYPE, "");
  char *d = 0;
  const wchar_t *s = Str;
  errno = 0;
  size_t cb = wcsrtombs(0, &s, 0, 0);
  if ((size_t) -1 != cb && (d = (char*) malloc(++cb)))
  {
    cb = wcsrtombs(d, &Str, cb, 0);
    if ((size_t) -1 == cb) { NSISRT_free(d); d = 0; }
  }
  if (!errno) errno = ENOMEM;
  setlocale(LC_CTYPE, orglocct);
  return d;
}
char* NSISRT_wctombpath(const wchar_t *Path)
{
  char *p = NSISRT_wctomb(Path);
  if (p) PathConvertWinToPosix(p);
  return p;
}
char* NSISRT_ttombpath(const TCHAR *Path)
{
#ifdef _UNICODE
  return NSISRT_wctombpath(Path);
#else
  char *p = _tcsdup(Path);
  if (p) PathConvertWinToPosix(p); else errno = ENOMEM;
  return p;
#endif
}

#include <wctype.h>
int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t n)
{
  // Note: Behavior depends on the LC_CTYPE category of the current locale.
#if _XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L
  return wcsncasecmp(a, b, n);
#else
  int diff = 0;
  for ( ; n--; ++a, ++b )
    if ((diff = (int) towlower(*a) - (int) towlower(*b)) || !*a) break;
  return diff;
#endif
}
int _wcsicmp(const wchar_t *a, const wchar_t *b)
{
  return _wcsnicmp(a, b, (size_t)-1);
}

long _wtol(const wchar_t *s) { return wcstol(s, 0, 10); }
int _wtoi(const wchar_t *s)
{
  errno = 0;
  long int r = _wtol(s);
  if (errno) r = 0;
  if (LONG_MIN == r) r = INT_MIN;
  if (LONG_MAX == r) r = INT_MAX;
  return (int) r;
}

int _swprintf(wchar_t *d, const wchar_t *f, ...)
{
  va_list val;
  va_start(val, f);
  int res = _vsnwprintf(d, INT_MAX / sizeof(*d), f, val);
  va_end(val);
  return res;
}

wchar_t* _wcsdup(const wchar_t *s)
{
  wchar_t *d = (wchar_t*) malloc((wcslen(s)+1) * sizeof(wchar_t));
  if (d) wcscpy(d, s);
  return d;
}

wchar_t* _wgetenv(const wchar_t *wname)
{
  mbstate_t mbs;
  memset(&mbs,0,sizeof(mbs));
  char *nval, *nval2, nname[200]; // Hopefully this is enough
  size_t cbnn = wcsrtombs(nname, &wname, sizeof(nname), &mbs);
  if ((size_t)-1 == cbnn || !(nval = getenv(nname))) return NULL;
  static wchar_t *g_wval = 0; // Not thread safe!
  for ( unsigned int cch = 200;; cch *= 2 )
  {
    if (!(g_wval = (wchar_t*) realloc(g_wval, cch * sizeof(wchar_t)))) break;
    nval2 = nval;
    memset(&mbs,0,sizeof(mbs));
    size_t cchwv = mbsrtowcs(g_wval, const_cast<const char**>(&nval2), cch, &mbs);
    if ((size_t)-1 == cchwv) return NULL;
    if (!nval2) return g_wval;
  }
  errno = ENOMEM;
  return 0;
}

int _wremove(const wchar_t *Path)
{
  const char *path = NSISRT_wctomb(Path); // Caller should have converted to POSIX path
  if (!path) return -1;
  const int retval = remove(path);
  NSISRT_free(path);
  return retval;
}

int _wchdir(const wchar_t *Path)
{
  const char *path = NSISRT_wctomb(Path); // Caller should have converted to POSIX path
  if (!path) return -1;
  const int retval = chdir(path);
  NSISRT_free(path);
  return retval;
}

#include <sys/types.h>
#include <sys/stat.h>
int _wstat(const wchar_t *Path, struct stat *pS)
{
  const char *path = NSISRT_wctomb(Path); // Caller should have converted to POSIX path
  if (!path) return -1;
  const int retval = stat(path, pS);
  NSISRT_free(path);
  return retval;
}

#ifdef _UNICODE
static int NSISRT_wsystem(const wchar_t *wcmd)
{
  if (!wcmd) return system(NULL);
  // NOTE: Only the first drive in the path will be converted to posix style (c:\foo d:\bar > /c/foo d:/bar)
  const char *cmd = NSISRT_wctombpath(wcmd);
  if (!cmd) return -1;
  const int retval = system(cmd);
  NSISRT_free(cmd);
  return retval;
}
#endif

const char* nsis_iconv_get_host_endian_ucs4_code()
{
  static const char* ucs4 = 0;
  if (!ucs4)
  {
    iconvdescriptor id;
#define NRT_TMP_IGHEUC(s) if (id.Open("wchar_t", (s))) return ucs4 = (s)
    NRT_TMP_IGHEUC("UCS-4-INTERNAL");
    NRT_TMP_IGHEUC(Platform_IsBigEndian() ? "UCS-4BE" : "UCS-4LE");
    NRT_TMP_IGHEUC(Platform_IsBigEndian() ? "UTF-32BE" : "UTF-32LE");
#undef NRT_TMP_IGHEUC
  }
  return ucs4;
}

bool nsis_iconv_reallociconv(iconv_t CD, char**In, size_t*cbInLeft, char**Mem, size_t&cbConverted)
{
  char *in, *heap = *Mem;
  UINT cbMem = 512;
  size_t inleft, outleft, icvret = (size_t) -1;
  for(;;)
  {
    in = *In, inleft = *cbInLeft, outleft = cbMem - sizeof(UINT32); // Leave room for \0
    char *p = (char*) realloc(heap, cbMem), *out = p;
    if (!p) break;
    heap = p, icvret = nsis_iconv_adaptor(iconv, CD, &in, &inleft, &out, &outleft);
    if ((size_t) -1 != icvret || E2BIG != errno) break;
    cbMem *= 4;
  }
  *In = in, *Mem = heap;
  cbConverted = cbMem - (outleft + sizeof(UINT32)), *cbInLeft = inleft;
  if ((size_t) -1 != icvret)
  {
    *((UINT32*)(&heap[cbConverted])) = 0;
    return true;
  }
  return false;
}

const unsigned short CODEPAGESTR_MAXLEN = 50; // Should be plenty
void create_code_page_string(TCHAR *buf, size_t len, UINT code_page)
{
  if (!g_nrt_iconv_narrowloc) NSISRT_Initialize(); // For winchar.cpp unit test
  switch(code_page)
  {
  case CP_ACP:   _sntprintf(buf, len, _T("%") NPRIns, iconv_ACP); return;
  case CP_OEMCP: _sntprintf(buf, len, _T("%") NPRIns, iconv_OEM); return;
  case CP_UTF8:  _sntprintf(buf, len, _T("UTF-8")); return;
  case 1200: // UTF16LE
  case 1201: // UTF16BE
    _sntprintf(buf, len, _T("UTF-16%cE"), 1200 == code_page ? 'L' : 'B');
    return;
  }
  _sntprintf(buf, len, _T("CP%d//TRANSLIT"), code_page);
}
#ifdef _UNICODE
void create_code_page_string(char*buf, size_t len, UINT code_page)
{
  TCHAR t[CODEPAGESTR_MAXLEN];
  create_code_page_string(t, COUNTOF(t), code_page);
  RawTStrToASCII(t, buf, len);
}
#endif

int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, const wchar_t* lpWideCharStr,
    int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCSTR lpDefaultChar,
    LPBOOL lpUsedDefaultChar) {
  static char buffer[4096]; // BUGBUG: Should this be 4*NSIS_MAX_STRLEN for large string build?

  char cp[CODEPAGESTR_MAXLEN];
  create_code_page_string(cp, COUNTOF(cp), CodePage);
  iconv_t cd = iconv_open(cp, nsis_iconv_get_wchar_t_code());
  size_t cchNull = 0;
  if (cd == (iconv_t) -1) return 0;
  if (cchWideChar < 0) cchWideChar = (int) wcslen(lpWideCharStr), ++cchNull;
  if (cbMultiByte == 0) cbMultiByte = COUNTOF(buffer) - cchNull, lpMultiByteStr = buffer;

  char *in = (char *) lpWideCharStr, *out = lpMultiByteStr;
  size_t inbytes = cchWideChar * sizeof(wchar_t), outbytes = cbMultiByte;
  if (nsis_iconv_adaptor(iconv, cd, &in, &inbytes, &out, &outbytes) == (size_t) -1) {
    iconv_close(cd);
    return 0;
  }
  iconv_close(cd);

  size_t cConv = (cbMultiByte - outbytes);
  if (cchNull && cbMultiByte) lpMultiByteStr[cConv] = '\0';
  return cConv + cchNull;
}

int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr,
    int cbMultiByte, wchar_t* lpWideCharStr, int cchWideChar) {
  static wchar_t buffer[4096]; // BUGBUG: Should this be 4*NSIS_MAX_STRLEN for large string build?

  char cp[CODEPAGESTR_MAXLEN];
  create_code_page_string(cp, COUNTOF(cp), CodePage);
  iconv_t cd = iconv_open(nsis_iconv_get_wchar_t_code(), cp);
  size_t cchNull = 0; // iconv on MacOS 14 & 15 does not parse the \0, we have to keep track of it.
  if (cd == (iconv_t) -1) return 0;
  if (cbMultiByte < 0) cbMultiByte = strlen(lpMultiByteStr), ++cchNull;
  if (cchWideChar == 0) cchWideChar = COUNTOF(buffer) - cchNull, lpWideCharStr = buffer;

  char *in = (char *) lpMultiByteStr, *out = (char *) lpWideCharStr;
  size_t inbytes = cbMultiByte, outbytes = cchWideChar * sizeof(wchar_t);
  if (nsis_iconv_adaptor(iconv, cd, &in, &inbytes, &out, &outbytes) == (size_t) -1) {
    iconv_close(cd);
    return 0;
  }
  iconv_close(cd);

  size_t cConv = cchWideChar - (outbytes / sizeof (wchar_t));
  if (cchNull && cchWideChar) lpWideCharStr[cConv] = '\0';
  return cConv + cchNull;
}

BOOL IsValidCodePage(UINT CodePage)
{
  char cp[CODEPAGESTR_MAXLEN];
  create_code_page_string(cp, COUNTOF(cp), CodePage);
  iconv_t cd = iconv_open(nsis_iconv_get_wchar_t_code(), cp);
  if (cd == (iconv_t) -1) return FALSE;
  iconv_close(cd);
  return TRUE;
}

#ifdef _UNICODE
void PathConvertWinToPosix(char*p)
{
  if ('\"' == *p) ++p; // Skip opening quote if any (For !system)
  size_t len = strlen(p);
  /* Replace drive letter X: by /x */
  if (len >= 2 && ':' == p[1]) p[1] = (char) tolower((int) p[0]), p[0] = '/';
  do if ('\\' == *p) *p = '/'; while (*(p = CharNextA(p)));
}
#endif
void PathConvertWinToPosix(TCHAR*p)
{
  if (_T('\"') == *p) ++p; // Skip opening quote if any (For !system)
  size_t len = _tcsclen(p);
  /* Replace drive letter X: by /x */
  if (len >= 2 && _T(':') == p[1]) p[1] = (TCHAR) tolower((int) p[0]), p[0] = _T('/');
  do if (_T('\\') == *p) *p = _T('/'); while (*(p = CharNext(p)));
}

#define MY_ERROR_MSG(x) {if (g_display_errors) {PrintColorFmtMsg_ERR(_T("%") NPRIs, x);}}

TCHAR *my_convert(const TCHAR *path)
{
  TCHAR *converted_path = _tcsdup(path);
  if (!converted_path)
  {
    MY_ERROR_MSG(_T("Error: could not allocate memory in my_convert()\n"));
    return 0;
  }
  PathConvertWinToPosix(converted_path);
  return converted_path;
}

void my_convert_free(TCHAR *converted_path)
{
  free(converted_path);
}

int my_open(const TCHAR *pathname, int flags)
{
#ifndef _UNICODE
  int result = open(pathname, flags);
#else
  char *nativepath = NSISRT_ttombpath(pathname);
  if (!nativepath) return -1;
  int result = open(nativepath, flags);
  NSISRT_free(nativepath);
#endif
  return result;
}
#endif //! _WIN32

FILE* my_fopen(const TCHAR *path, const char *mode)
{
  FILE*f = 0;
#ifndef _UNICODE
  f = fopen(path, mode);
#else
#ifdef _WIN32
  TCHAR tmode[20];
  for (int i=0; ; ++i) if (0 == (tmode[i] = mode[i])) break;
  f = _wfopen(path, tmode);
#else
  char *nativepath = NSISRT_wctombpath(path);
  if (nativepath)
  {
    f = fopen(nativepath, mode);
    NSISRT_free(nativepath);
  }
#endif
#endif
  return f;
}

static inline const TCHAR* getptrstrfmt_tstr()
{
#ifdef _WIN32
  return sizeof(void*) > 4 ? _T("%I64u") : _T("%u");
#else
  assert(sizeof(void*) <= sizeof(unsigned long long));
  return sizeof(void*) > sizeof(unsigned long) ? _T("%llu") : _T("%lu");
#endif
}

static inline const char* getptrstrfmt_cstr()
{
#ifdef _WIN32
  return sizeof(void*) > 4 ? "%I64u" : "%u";
#else
  assert(sizeof(void*) <= sizeof(unsigned long long));
  return sizeof(void*) > sizeof(unsigned long) ? "%llu" : "%lu";
#endif
}

int ptrtostr(const void* Src, TCHAR*Dst)
{
  return wsprintf(Dst, getptrstrfmt_tstr(), Src);
}

void* strtoptr(const TCHAR*Src)
{
#ifdef _WIN32
  void*v;
  return _stscanf(Src, getptrstrfmt_tstr(), &v) == 1 ? v : 0;
#else
#ifdef _UNICODE
  char buf[42];
  size_t cb = wcsrtombs(buf, &Src, sizeof(buf), 0);
  if (cb >= sizeof(buf)) return 0;
#else
  const char* buf = Src;
#endif
  if (sizeof(void*) > sizeof(unsigned long))
  {
    unsigned long long v;
    return sscanf(buf, getptrstrfmt_cstr(), &v) == 1 ? (void*) v : 0;
  }
  else
  {
    unsigned long v;
    return sscanf(buf, getptrstrfmt_cstr(), &v) == 1 ? (void*) v : 0;
  }
#endif
}

#include <sys/types.h>
#include <sys/stat.h>
#if (defined(_MSC_VER) && (_MSC_VER >= 1200)) || defined(__MINGW32__)
#include <io.h>
UINT64 get_file_size64(FILE *f)
{
  INT64 s = _filelengthi64(_fileno(f)); // Could also use _get_osfhandle+GetFileSize64
  return (INT64) -1L != s ? s : invalid_file_size64;
}
#else
UINT64 get_file_size64(FILE *f)
{
  UINT64 result = invalid_file_size64;
  // 32bit platforms require _FILE_OFFSET_BITS = 64 to correctly return the size
#if _XOPEN_SOURCE >= 500 || _POSIX_C_SOURCE >= 200112L || defined(__APPLE__) || defined(__FreeBSD__)
  struct stat st;
  if (0 == fstat(fileno(f), &st) && st.st_size <= (sizeof(st.st_size) >= 8 ? (off_t)0x7fffffffffffffffLL : LONG_MAX))
    result = (UINT64) st.st_size;
#else
  long cb, restoreseek = true;
  fpos_t orgpos;
  if (!restoreseek || 0 == fgetpos(f, &orgpos))
    if (0 == fseek(f, 0, SEEK_END)) // Not fully portable!
      if ((cb = ftell(f)) != -1L) // This might not be correct for files in text mode!
        if (!restoreseek || 0 == fsetpos(f, &orgpos))
          result = cb;
#endif
  return result;
}
#endif

UINT32 get_file_size32(FILE *f)
{
  UINT32 result = invalid_file_size32;
#if _XOPEN_SOURCE >= 500 || _POSIX_C_SOURCE >= 200112L
  struct stat st;
  if (0 == fstat(fileno(f), &st) && st.st_size <= (sizeof(st.st_size) >= 8 ? (off_t)0xffffffffUL : LONG_MAX))
    result = (UINT32) st.st_size;
#else
  UINT64 size64 = get_file_size64(f);
  if (size64 <= 0xffffffffUL && invalid_file_size64 != size64)
    result = (UINT32) size64;
#endif
  return result;
}

UINT64 Platform_GetMaxFileSize()
{
#ifdef _WIN32
  return ~(UINT64)0;
#elif _XOPEN_SOURCE >= 500 || _POSIX_C_SOURCE >= 200112L || defined(__APPLE__) || defined(__FreeBSD__)
  struct stat st;
  if (sizeof(st.st_size) >= 8)
  {
#ifdef LLONG_MAX
    return (UINT64)(off_t)LLONG_MAX;
#endif
    const UINT64 sixthree = (~(UINT64)0) >> 1;
    return (off_t)sixthree > 0 ? sixthree : (UINT32)(off_t)0xffffffffUL;
  }
  return (UINT64)(off_t)LONG_MAX;
#endif
  return (UINT)INT_MAX;
}

BYTE* alloc_and_read_file(FILE *f, unsigned long &size)
{
  BYTE *result = 0, *mem = 0;
  if (!f) return result;
  UINT32 size32 = get_file_size32(f);
  mem = (invalid_file_size32 != size32) ? (BYTE*) malloc(size = size32) : 0;
  if (mem)
    if (0 == fseek(f, 0, SEEK_SET))
      if (fread(mem, 1, size, f) == size)
        result = mem, mem = 0;
  free(mem);
  return result;
}

BYTE* alloc_and_read_file(const TCHAR *filepath, unsigned long &size)
{
  BYTE *result = 0;
  FILE*f = FOPEN(filepath, ("rb"));
  if (f)
  {
    result = alloc_and_read_file(f, size);
    fclose(f);
  }
  return result;
}

void close_file_view(FILEVIEW&mmfv)
{
#ifdef _WIN32
  if (mmfv.base) UnmapViewOfFile(mmfv.base);
#else
  if (mmfv.base) munmap(mmfv.base, mmfv.size);
#endif
}
char* create_file_view_readonly(const TCHAR *filepath, FILEVIEW&mmfv)
{
#ifdef _WIN32
  return mmfv.base = CreateMappedFileView(filepath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, PAGE_READONLY, FILE_MAP_READ, mmfv.size);
#else
  return mmfv.base = CreateMappedFileView(filepath, "rb", PROT_READ, MAP_SHARED, mmfv.size);
#endif
}

size_t write_octets_to_file(const TCHAR *filename, const void *data, size_t cb)
{
  FILE *hfile = FOPEN(filename, ("wb"));
  if (!hfile) return 0;
  size_t ret = fwrite(data, 1, cb, hfile);
  fclose(hfile);
  return ret;
}

TCHAR* create_tempfile_path()
{
  TCHAR *tfpath = NULL;
#ifdef _WIN32
  TCHAR buftmpdir[MAX_PATH], buf[MAX_PATH];
  DWORD cch = GetTempPath(COUNTOF(buftmpdir), buftmpdir);
  if (cch && cch < COUNTOF(buftmpdir) && GetTempFileName(buftmpdir, _T("nst"), 0, buf))
    tfpath = _tcsdup(buf);
#else //! _WIN32
  char narrowpath[] = ("/tmp/makensisXXXXXX");
  const mode_t org_umask = umask(0077);
  int fd = mkstemp(narrowpath);
  umask(org_umask);
  if (fd != -1)
  {
#ifdef _UNICODE
    assert(NSISRT_free_is_STDC_free());
    tfpath = NSISRT_mbtowc(narrowpath);
#else
    tfpath = _tcsdup(narrowpath);
#endif
    close(fd);
  }
#endif //~ _WIN32
  return tfpath;
}

tstring get_full_path(const tstring &path) {
#ifdef _WIN32
  TCHAR real_path[1024], *fnpart;
  DWORD rc = GetFullPathName(path.c_str(), COUNTOF(real_path), real_path, &fnpart);
  assert(rc <= 1024); // path size is limited by MAX_PATH (260)
  assert(rc != 0); // rc==0 in case of error
  return tstring(real_path);
#else // !_WIN32
  tstring result;
  char *rpret = 0, *inputpath = NSISRT_ttombpath(path.c_str());
  if (!inputpath) return tstring(path);
#ifdef PATH_MAX
  static char buffer[PATH_MAX];
#else // !PATH_MAX
#if _POSIX_C_SOURCE >= 200809L
  char *buffer = NULL; // realpath can malloc
#else
  int path_max = pathconf(inputpath, _PC_PATH_MAX);
  if (path_max <= 0) path_max = 4096;
  char *buffer = (char *) malloc(path_max * sizeof(char));
  if (buffer)
#endif
#endif // ~PATH_MAX
  {
    rpret = realpath(inputpath, buffer);
  }
  result = CtoTString(rpret ? rpret : inputpath);
#ifndef PATH_MAX
  free(rpret ? rpret : buffer);
#endif
  NSISRT_free(inputpath);
  return result;
#endif // ~_WIN32
}

tstring get_string_prefix(const tstring& str, const tstring& separator) {
  const tstring::size_type last_separator_pos = str.rfind(separator);
  if (last_separator_pos == string::npos) return str;
  return str.substr(0, last_separator_pos);
}

tstring get_string_suffix(const tstring& str, const tstring& separator) {
  const tstring::size_type last_separator_pos = str.rfind(separator);
  if (last_separator_pos == tstring::npos) return str;
  return str.substr(last_separator_pos + separator.size(), tstring::npos);
}

tstring get_dir_name(const tstring& path) {
  return get_string_prefix(path, PLATFORM_PATH_SEPARATOR_STR); // BUGBUG: Windows should support "\" and "/"
}

tstring get_file_name(const tstring& path) {
  return get_string_suffix(path, PLATFORM_PATH_SEPARATOR_STR); // BUGBUG: Windows should support "\" and "/"
}

tstring get_executable_path(const TCHAR* argv0) {
#ifdef _WIN32
  TCHAR temp_buf[MAX_PATH+1];
  temp_buf[0] = _T('\0');
  int rc = GetModuleFileName(NULL,temp_buf,MAX_PATH);
  assert(rc != 0);
  return tstring(temp_buf);
#elif __APPLE__
  char temp_buf[MAXPATHLEN+1];
  unsigned int buf_len = MAXPATHLEN;
  int rc = Apple::_NSGetExecutablePath(temp_buf, &buf_len);
  assert(rc == 0);
  return tstring(CtoTString(temp_buf));
#else /* Linux/BSD/POSIX/etc */
  char *path = NULL, *pathtmp;
  size_t len = 100;
  int nchars;
  while(1){
    pathtmp = (char*)realloc(path,len+1);
    if (!pathtmp) {
basic:
      free(path);
      // Note: Not using _tgetenv(_T("_")) here because it is not always set correctly. !makensis instruction in SVN r7372 will execute "/usr/bin/scons"!
      return get_full_path(argv0);
    }
    path = pathtmp;
    nchars = readlink("/proc/self/exe", path, len);
    if (nchars == -1) {
      goto basic;
    }
    if (nchars < (int) len) {
      path[nchars] = '\0';
      tstring result;
      result = CtoTString(path);
      free(path);
      return result;
    }
    len *= 2;
  }
#endif
}

tstring get_executable_dir(const TCHAR *argv0) {
  return get_dir_name(get_executable_path(argv0));
}

tstring remove_file_extension(const tstring& path) {
  return get_string_prefix(path, _T("."));
}

tstring& path_append_separator(tstring& path)
{
  tstring::iterator ib = path.begin(), ie = path.end();
  if (ib != ie && !IsPathSeparator(*--ie))
    path += PLATFORM_PATH_SEPARATOR_STR;
  return path;
}

tstring& path_append(tstring& base, const TCHAR* more)
{
  if (IsPathSeparator(*more)) ++more;
  return path_append_separator(base) += more;
}

static int PathGetDosDriveNumber(const TCHAR *p)
{
  // Note: Unlike PathGetDriveNumber(), we require a path separator after the colon.
  if (p[0] && _T(':') == p[1] && IsAgnosticPathSeparator(p[2]))
  {
    const TCHAR loch = p[0]|32;
    if (loch >= _T('a') && loch <= _T('z')) return loch - _T('a');
  }
  return -1;
}
bool IsWindowsPathRelative(const TCHAR *p)
{
  if (_T('\\') == p[0]) return _T('\\') != p[1]; // Current drive relative, not (unverified) UNC
  return PathGetDosDriveNumber(p) < 0;
}

tstring replace_all(const TCHAR *str, const TCHAR *find, const TCHAR *repl)
{
  tstring out = str;
  for (size_t cchF = _tcslen(find), cchR = _tcslen(repl), i = 0; ; i += cchR)
    if ((i = out.find(find, i)) == tstring::npos)
      return out;
    else
      out.replace(i, cchF, repl);
}

struct ToLower
{
   TCHAR operator() (TCHAR c) const { return _totlower(c); }
};

tstring lowercase(const tstring &str) {
  tstring result = str;
  transform(str.begin(), str.end(), result.begin(), ToLower());
  return result;
}

void RawTStrToASCII(const TCHAR*in,char*out,UINT maxcch)
{
  const bool empty = !maxcch;
  for(; maxcch && *in; --maxcch) *out++ = (char) *in++;
  if (!empty) *out = 0;
}

/*
 * ExpandoStrFmtVaList returns the number of characters written excluding
 * the \0 terminator or 0 on error.
 * realloc() is used on *ppMalloc if cchStack is not 
 * large enough to hold the formatted string.
*/
size_t ExpandoStrFmtVaList(wchar_t*Stack, size_t cchStack, wchar_t**ppMalloc, const wchar_t*FmtStr, va_list Args)
{
#ifdef _WIN32
  static size_t qlen = INT_MAX;
// For _vsnwprintf, the \0 terminator is not part of the input size
#  if _MSC_VER < 1310
  const bool have__vscwprintf = false;
#    define ExpandoStrFmtVaList_vsnwprintf(d,c,f,v) _vsnwprintf((d),(c)?(c)-1:0,(f),(v)) // Allow INT_MAX hack on MinGW and older versions of VC that don't have _vscwprintf
#  else
  const bool have__vscwprintf = true;
#    define ExpandoStrFmtVaList_vsnwprintf(d,c,f,v) ( INT_MAX==(c) ? _vscwprintf((f),(v)) : _vsnwprintf((d),(c)?(c)-1:0,(f),(v)) )
#  endif
#else
  const size_t qlen = INT_MAX;
#  define ExpandoStrFmtVaList_vsnwprintf vswprintf
#endif
#if defined(_ISOC99_SOURCE) || _POSIX_C_SOURCE >= 200112L
  const bool cansizecalc = true, msvcbackdoor = false;
#else
  static char testedsizecalc = 0;
  if (!testedsizecalc)
  {
#ifdef _WIN32
    int cch = have__vscwprintf ? 0 : ExpandoStrFmtVaList_vsnwprintf(0, qlen = 0, L"333", Args);
    if (cch != 3) cch = ExpandoStrFmtVaList_vsnwprintf(0, qlen = INT_MAX, L"333", Args); // Is this actually necessary? Just set qlen = INT_MAX if have__vscwprintf?
#else
    wchar_t testbuf[1+!0];
    int cch = ExpandoStrFmtVaList_vsnwprintf(testbuf, COUNTOF(testbuf), L"333", Args);
#endif
    testedsizecalc = (3 == cch) + 1;
  }
#ifdef _WIN32
  const bool msvcbackdoor = !!(testedsizecalc - 1), cansizecalc = false;
#else
  const bool cansizecalc = !!(testedsizecalc - 1), msvcbackdoor = false;
#endif
#endif
  size_t &cchAvail = cchStack, cch;
  wchar_t *&dest = Stack, *mem = *ppMalloc;
  for(;;)
  {
    cch = ExpandoStrFmtVaList_vsnwprintf(dest, cchAvail, FmtStr, Args);
    if ((int)cch < 0)
    {
      cch = 0;
      if (cansizecalc) break; // vswprintf error, abort!
      if (msvcbackdoor)
        cchAvail = ExpandoStrFmtVaList_vsnwprintf(0, qlen, FmtStr, Args) + 1;
      else
        cchAvail = 4 * STD_MAX(cchAvail, (size_t)500);
    }
    else
    {
      if (cch < cchAvail) break; // We are done.
      cchAvail = ++cch; // cch from vswprintf did not include the \0 terminator
    }
    dest = mem = (wchar_t*) realloc(mem, cchAvail * sizeof(wchar_t));
    if (!mem) return 0;
  }
  *ppMalloc = mem;
  return cch;
}

const TCHAR* GetFriendlySize(UINT64 n, unsigned int&fn, GETFRIENDLYSIZEFLAGS f)
{
  static const TCHAR* scale[] = {
    _T(" bytes"), _T(" KiB"), _T(" MiB"), _T(" GiB"), _T(" TiB")
  };
  unsigned char s = 0, accurate = f&GFSF_BYTESIFPOSSIBLE;
  while(n > ((s || !accurate) ? (1024*1024)-1 : UINT_MAX)) n /= 1024, ++s;
  fn = (unsigned int) n;
  if (!s) return (f&GFSF_HIDEBYTESCALE) ? _T("") : 1 == fn ? _T(" byte") : scale[s];
  return s >= COUNTOF(scale) ? _T(" ?") : scale[s];
}

#ifdef _WIN32
#ifdef _UNICODE
int RunChildProcessRedirected(LPCWSTR cmdprefix, LPCWSTR cmdmain, bool ForceUTF8)
{
  // We have to deliver the requested output encoding to our host (if any) and the 
  // only way to do that is to convert the pipe content from what we hope is UTF-8.
  // The reason we need a pipe in the first place is because we cannot trust the 
  // child to call GetConsoleOutputCP(), and even if we could, UTF-16 is not valid there.
  UINT cp = CP_UTF8, mbtwcf = MB_ERR_INVALID_CHARS, oemcp = GetOEMCP();
  errno = ENOMEM;
  if (!cmdprefix) cmdprefix = L"";
  size_t cch1 = _tcslen(cmdprefix), cch2 = _tcslen(cmdmain);
  WCHAR *cmd = (WCHAR*) malloc( (cch1 + cch2 + 1) * sizeof(WCHAR) );
  if (!cmd) return -1;
  _tcscpy(cmd, cmdprefix);
  _tcscat(cmd, cmdmain);
  SECURITY_DESCRIPTOR sd = { 1, 0, SE_DACL_PRESENT, NULL, };
  SECURITY_ATTRIBUTES sa = { sizeof(sa), &sd, TRUE };
  const UINT orgwinconcp = GetConsoleCP(), orgwinconoutcp = GetConsoleOutputCP();
  if (orgwinconoutcp == oemcp && !ForceUTF8) cp = oemcp, mbtwcf = 0; // Bug #1092: Batch files not a fan of UTF-8
  HANDLE hSIRd, hSIWr, hSORd, hSOWr;
  PROCESS_INFORMATION pi;
  if (!CreatePipe(&hSIRd, &hSIWr, &sa, 0)) // XCopy.exe does not work without a valid StdIn!
    hSIRd = hSIWr = INVALID_HANDLE_VALUE;
  BOOL ok = CreatePipe(&hSORd, &hSOWr, &sa, 0);
  if (!ok)
    hSORd = hSOWr = 0;
  else
  {
    STARTUPINFO si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = hSOWr;
    si.hStdInput = hSIRd;
    errno = ECHILD;
    SetConsoleOutputCP(cp);
    ok = CreateProcess(0, cmd, 0, 0, TRUE, 0, 0, 0, &si, &pi);
    CloseHandle(hSOWr); // We want ERROR_BROKEN_PIPE when the child is done
  }
  free(cmd);
  DWORD childec = -1;
  if (ok)
  {
    bool fullbuf = false, utf8 = CP_UTF8 == cp, okt;
    char iobuf[512];
    DWORD cbRead, cbOfs = 0, cchwb = 0, i;
    WCHAR wbuf[100], wchbuf[2+1]; // A surrogate pair + \0
    for(;;)
    {
      BOOL okr = ReadFile(hSORd, iobuf+cbOfs, sizeof(iobuf)-cbOfs, &cbRead, 0);
      cbRead += cbOfs, cbOfs = 0;
      unsigned char cbTrail, cch;
      for(i = 0; i < cbRead;)
      {
        cch = 0;
        if (utf8)
        {
          okt = UTF8_GetTrailCount(iobuf[i], cbTrail);
          if (!okt) // Not UTF-8? Switching to a MBCS CP
          {
switchcp:   cp = orgwinconoutcp, mbtwcf = 0, utf8 = false;
            SetConsoleOutputCP(cp = (CP_UTF8 == cp ? CP_ACP : cp));
            continue;
          }
          if (!cbTrail) cch++, wchbuf[0] = iobuf[i]; // ASCII
        }
        else
        {
          cbTrail = !!IsDBCSLeadByteEx(cp, iobuf[i]);
        }
        if (i+cbTrail >= cbRead) // Read more first?
        {
          memmove(iobuf, iobuf+i, cbOfs = cbRead - i);
          if (okr) break; else i = 0;
        }
        if (!cch)
        {
          cch = MultiByteToWideChar(cp, mbtwcf, &iobuf[i], 1+cbTrail, wchbuf, COUNTOF(wchbuf)-1);
          if (!cch)
          {
            if (utf8) goto switchcp;
            cch++, wchbuf[0] = UNICODE_REPLACEMENT_CHARACTER;
          }
        }
        i += 1+cbTrail;
        if (0xfeff == wchbuf[0] && 1 == cch) cch = 0; // MakeNsisW is not a fan of the BOM, eat it.
        if (!cch) continue;
        wbuf[cchwb++] = wchbuf[0];
        if (--cch) wbuf[cchwb++] = wchbuf[1];
        fullbuf = cchwb+cch >= COUNTOF(wbuf)-1; // cch is 1 for surrogate pairs
        if (!okr || fullbuf || L'\n' == wchbuf[0]) // Stop on \n so \r\n conversion has enough context (...\r\n vs ...\n)
        { finalwrite:
#ifdef MAKENSIS
          extern WINSIO_OSDATA g_osdata_stdout;
          WinStdIO_OStreamWrite(g_osdata_stdout, wbuf, cchwb); // Faster than _ftprintf
#else
          wbuf[cchwb] = L'\0';
          _ftprintf(g_output, _T("%") NPRIs, wbuf);
#endif
          cchwb = 0;
        }
      }
      if (!okr)
      {
        if (cchwb) goto finalwrite; // End of stream without a ending newline, write out the remaining data.
        break;
      }
    }
    fflush(g_output);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &childec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  SetConsoleCP(orgwinconcp), SetConsoleOutputCP(orgwinconoutcp);
  CloseHandle(hSIRd), CloseHandle(hSIWr);
  CloseHandle(hSORd);
  return childec;
}
#else
int RunChildProcessRedirected(LPCSTR cmd, bool ForceUTF8)
{
  STARTUPINFO si = { sizeof(STARTUPINFO), };
  PROCESS_INFORMATION pi;
  if (!CreateProcess(NULL, const_cast<LPSTR>(cmd), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return GetLastError();
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &si.cb);
  CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
  return (int) si.cb;
}
#endif //~ _UNICODE
#endif //~ _WIN32

int sane_system(const TCHAR *command)
{
#ifdef _WIN32

  // workaround for bug #1509909
  // http://sf.net/tracker/?func=detail&atid=373085&aid=1509909&group_id=22049
  //
  // cmd.exe /C has some weird handling for quotes. it strips
  // the surrounding quotes, if they exist. if there are quotes
  // around the program path and its arguments, it will strip
  // the outer quotes. this may result in something like:
  //   `program files\nsis\makensis.exe" "args`
  // which obviously fails...
  //
  // to avoid the stripping, a harmless string is prefixed to the command line.
  const TCHAR*const prefix = _T("IF 1==1 ");
#ifdef _UNICODE
  if (!command) return 0; else if (!*command) return 1;
  tstring fixedcmd = _tgetenv(_T("COMSPEC"));
  if (!fixedcmd.length()) fixedcmd = _T("CMD.EXE");
  fixedcmd += _T(" /C "), fixedcmd += prefix;
  return RunChildProcessRedirected(fixedcmd.c_str(), command);
#else
  tstring fixedcmd = prefix;
  fixedcmd += _T(""), fixedcmd += command;
  return _tsystem(fixedcmd.c_str());
#endif // ~_UNICODE
#else // !_WIN32
#ifndef _UNICODE
  TCHAR* cmd = const_cast<TCHAR*>(command);
  PATH_CONVERT(cmd);
  return _tsystem(cmd);
#else
  return NSISRT_wsystem(command);
#endif
#endif // ~_WIN32
}

#ifdef _WIN32
bool GetFileSize64(HANDLE hFile, ULARGE_INTEGER &uli)
{
  uli.LowPart = GetFileSize(hFile, &uli.HighPart);
  return INVALID_FILE_SIZE != uli.LowPart || !GetLastError();
}
#endif //~ _WIN32
#if defined(_WIN32) && defined(_UNICODE) && defined(MAKENSIS)
#include <io.h> // for _get_osfhandle
bool WINAPI WinStdIO_OStreamInit(WINSIO_OSDATA&osd, FILE*strm, WORD cp, int bom)
{
  // bom < 0: override cp if UTF detected but never write BOM
  // bom = 0: ignore BOM and force cp
  // bom > 0: override cp if UTF detected, write BOM if it does not already exist
  const int fd = _fileno(strm);
  osd.mode = 0, osd.hCRT = strm, osd.hNative = (HANDLE) _get_osfhandle(fd);
  if (INVALID_HANDLE_VALUE == osd.hNative) return false;
  DWORD conmode;
  if (GetConsoleMode(osd.hNative, &conmode)) osd.mode++; else osd.mode--;
  bool succ = NStream::SetBinaryMode(fd);
  DWORD cbio = 0;
  ULARGE_INTEGER uli;
  if (succ && 0 != bom && GetFileSize64(osd.hNative, uli) && uli.QuadPart)
  {
    OVERLAPPED olap = {0}; // Used to read from start of file
    unsigned char bufbom[4];
    if (ReadFile(osd.hNative, bufbom, sizeof(bufbom), &cbio, &olap))
    {
      UINT detbom = DetectUTFBOM(bufbom, cbio);
      if (detbom) cp = (WORD) detbom, bom = 0;
    }
    SetFilePointer(osd.hNative, 0, 0, FILE_END);
  }
  osd.mustwritebom = bom > 0 && !cbio, osd.cp = cp;
  return succ || (sizeof(TCHAR) > 1 && WinStdIO_IsConsole(osd)); // Don't care about BOM for WriteConsoleW
}
bool WINAPI WinStdIO_OStreamWrite(WINSIO_OSDATA&osd, const wchar_t *Str, UINT cch)
{
  if ((UINT)-1 == cch) cch = (UINT)_tcslen(Str);
  DWORD cbio;
  if (WinStdIO_IsConsole(osd))
    return WriteConsoleW(osd.hNative, Str, cch, &cbio, 0) || !cch;
  NOStream strm(osd.hCRT);
  NStreamEncoding &enc = strm.StreamEncoding();
  enc.SetCodepage(osd.cp);
  bool retval = false;
  if (osd.mustwritebom)
  {
    osd.mustwritebom = false;
    if (enc.IsUnicode() && !strm.WriteBOM(enc))
    {
      osd.mode = 1, osd.hNative = 0; // Something is wrong, stop writing!
      goto end;
    }
  }
  retval = strm.WritePlatformNLString(Str, cch);
end:
  strm.Detach();
  return retval;
}
static WINSIO_OSDATA*WinStdIO_GetNativeStreamData(FILE*strm)
{
  extern WINSIO_OSDATA g_osdata_stdout, g_osdata_stderr;
  if (g_output == strm) return &g_osdata_stdout;
  return g_errout == strm ? &g_osdata_stderr : NULL;
}
int WINAPI WinStdIO_vfwprintf(FILE*strm, const wchar_t*Fmt, va_list val)
{
  WINSIO_OSDATA*pOSD;
  if (Fmt && (pOSD = WinStdIO_GetNativeStreamData(strm)))
  {
    ExpandoString<wchar_t, NSIS_MAX_STRLEN> buf;
    errno = ENOMEM;
    const size_t cchfmt = buf.StrVFmt(Fmt, val, false);
    UINT cch = (UINT) cchfmt;
    assert(sizeof(size_t) <= 4 || cchfmt == cch);
    if (cch && !WinStdIO_OStreamWrite(*pOSD, buf, cch))
    {
      cch = 0, errno = EIO;
    }
    return cch ? cch : (*Fmt ? -1 : 0);
  }
  return vfwprintf(strm, Fmt, val);
}
int WinStdIO_fwprintf(FILE*strm, const wchar_t*Fmt, ...)
{
  va_list val;
  va_start(val, Fmt);
  int rv = _vftprintf(strm, Fmt, val);
  va_end(val);
  return rv;
}
int WinStdIO_wprintf(const wchar_t*Fmt, ...)
{
  va_list val;
  va_start(val, Fmt);
  int rv = _vftprintf(g_output, Fmt, val);
  va_end(val);
  return rv;
}
static HANDLE NSISRT_FastGetConsoleScreenHandle()
{
  extern WINSIO_OSDATA g_osdata_stdout, g_osdata_stderr;
  return WinStdIO_IsConsole(g_osdata_stdout) ? g_osdata_stdout.hNative : g_osdata_stderr.hNative;
}
bool NSISRT_Initialize() // Init function for MakeNSIS Win32
{
  static bool inited = false;
  if (inited) return inited;
  extern WINSIO_OSDATA g_osdata_stdout, g_osdata_stderr;
  g_osdata_stderr.mode = g_osdata_stdout.mode = 0, g_osdata_stderr.hNative = g_osdata_stdout.hNative = 0;
  return (inited = true);
}
#elif defined(_WIN32)
#define NSISRT_FastGetConsoleScreenHandle NSISRT_GetConsoleScreenHandle
bool NSISRT_Initialize() { return true; } // Init function for non-MakeNSIS Win32 (NSISRT_DEFINEGLOBALS sets g_output and g_errout)
static HANDLE NSISRT_GetConsoleScreenHandle()
{
  DWORD cm;
  HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
  return GetConsoleMode(hCon, &cm) ? hCon : GetStdHandle(STD_ERROR_HANDLE);
}
#endif

void PrintColorFmtErrMsg(const TCHAR *fmtstr, va_list args)
{
  PrintColorFmtMsg_WARN(_T("")); // flush g_output
  SetPrintColorERR();
  _vftprintf(g_errout, fmtstr, args), fflush(g_errout);
  ResetPrintColor();
}

void PrintColorFmtMsg(unsigned int type, const TCHAR *fmtstr, va_list args)
{
#ifdef _WIN32
  HANDLE hWin32Con = NSISRT_FastGetConsoleScreenHandle();
  static INT32 contxtattrbak = -1;
  WORD txtattr = 0;
  if (contxtattrbak < 0)
  {
    if (-1 == contxtattrbak)
    {
      CONSOLE_SCREEN_BUFFER_INFO csbi;
      contxtattrbak = -2;
      if (GetConsoleScreenBufferInfo(hWin32Con, &csbi))
      {
        contxtattrbak = csbi.wAttributes;
        goto gottxtattrbak;
      }
    }
  }
  else
  {
gottxtattrbak:
    switch(type & 0xF)
    {
    case 0: goto resettxtattr;
    case 1: txtattr = FOREGROUND_INTENSITY|FOREGROUND_GREEN|FOREGROUND_RED; break;
    case 2: txtattr = FOREGROUND_INTENSITY|FOREGROUND_RED; break;
    }
    // Use original background color if our text will still be readable
    if ((contxtattrbak & 0xF0) != (txtattr<<4)) txtattr |= (contxtattrbak & 0xF0);
    if ((txtattr & 0xFF) == 0xFE) txtattr &= ~FOREGROUND_INTENSITY; // BrightYellow on BrightWhite is hard to read
    SetConsoleTextAttribute(hWin32Con, txtattr);
  }
#endif

  if (fmtstr) _vftprintf(g_output, fmtstr, args);
  fflush(g_output);

#ifdef _WIN32
  if (contxtattrbak >= 0 && !(0x10 & type))
  {
resettxtattr:
    SetConsoleTextAttribute(hWin32Con, contxtattrbak);
  }
#endif
}

void FlushOutputAndResetPrintColor()
{
  fflush(g_output);
#ifdef _WIN32
  PrintColorFmtMsg(0, NULL, (va_list)NULL); //va_list is just a pointer on windows so this is ok
#endif
}

unsigned char Platform_SupportsUTF8Conversion()
{
  static unsigned char cached = 0;
  if (0 == cached) cached = 1 + !!IsValidCodePage(CP_UTF8);
  return (cached - 1);
}

void *operator new(size_t size) NSIS_CXX_THROWSPEC(bad_alloc)
{
  void *p = malloc(size);
  if (!p) throw bad_alloc();
  return p;
}
void *operator new[](size_t size) NSIS_CXX_THROWSPEC(bad_alloc)
{
  return operator new(size);
}
void operator delete(void *p) NSIS_CXX_NOEXCEPT() { if (p) free(p); }
void operator delete [](void *p) NSIS_CXX_NOEXCEPT() { if (p) free(p); }
