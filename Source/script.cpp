/*
 * script.cpp
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
#include <ctype.h>
#include "tokens.h"
#include "build.h"
#include "util.h"
#include "winchar.h"
#include "ResourceEditor.h"
#include "DialogTemplate.h"
#include "BinInterop.h"
#include "lang.h"
#include "dirreader.h"
#include <nsis-version.h>
#include "icon.h"
#include "exehead/api.h"
#include "exehead/resource.h"
#include <cassert> // for assert(3)
#include <time.h>
#include "tstring.h"
#include "utf.h"
#include <algorithm>
#include "boost/scoped_ptr.hpp"

using namespace std;

#ifndef _WIN32
#  include <sys/stat.h> // for stat and umask
#  include <sys/types.h> // for mode_t
#  include <fcntl.h> // for O_RDONLY
#  include <unistd.h>
#  include <stdlib.h> // for mkstemp
#endif

#define REGROOTKEYTOINT(hk) ( (INT) (((INT_PTR)(hk)) & 0xffffffff) ) // Masking off non-existing top bits to make GCC happy
#define REGROOTKEYTOINTEX(hk, removeviewbits) ( REGROOTKEYTOINT(hk) & ~(removeviewbits ? (REGROOTVIEW32|REGROOTVIEW64) : 0) )

#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
typedef enum { LU_INVALID = -1, LU_PIXEL = 0, LU_DIALOG } LAYOUTUNIT;
static int ParseLayoutUnit(const TCHAR*Str, LAYOUTUNIT&LU)
{
  TCHAR buf[200];
  int succ, val = LineParser::parse_int(Str, &succ);
  if (succ) return (LU = LU_PIXEL, val);
  size_t cch = my_strncpy(buf, Str, COUNTOF(buf));
  if (cch > 1 && S7IsChEqualI('u', buf[cch-1])) // Something with a 'u' suffix?
  {
    buf[cch-1] = _T('\0');
    val = LineParser::parse_int(buf, &succ);
    if (succ) return (LU = LU_DIALOG, val);
  }
  return (LU = LU_INVALID, -1);
}
#endif

#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
static bool LookupWinSysColorId(const TCHAR*Str, UINT&Clr)
{
  static const struct { const TCHAR*Name; UINT Id; } map[] = { // Note: This list is incomplete.
    { _T("WINDOW"), 5 }, { _T("WINDOWTEXT"), 8 },
    { _T("3DFACE"), 15 }, { _T("BTNTEXT"), 18 }, // "Three-dimensional display elements and dialog box"
    { _T("HIGHLIGHT"), 13 }, { _T("HIGHLIGHTTEXT"), 14 }, // "Item(s) selected in a control"
    { _T("GRAYTEXT"), 17 }, // "Grayed (disabled) text"
    { _T("HOTLIGHT"), 26 }, // "Color for a hyperlink or hot-tracked item" (Win98+)
  };
  for (UINT i = 0; i < COUNTOF(map); ++i)
    if (!_tcsicmp(map[i].Name, Str)) return (Clr = map[i].Id, true);
  return false;
}
static UINT ParseCtlColor(const TCHAR*Str, int&CCFlags, int CCFlagmask)
{
  UINT clr, v;
  TCHAR buf[7+!0], *pEnd;
  my_strncpy(buf, Str, 7+!0), buf[7] = '\0';
  if (!_tcscmp(_T("SYSCLR:"), buf))
  {
    CCFlags |= ((CC_TEXT_SYS|CC_BK_SYS) & CCFlagmask); // ExeHead must call GetSysColor
    if (!LookupWinSysColorId(Str+7, clr)) clr = _tcstoul(Str+7, &pEnd, 0);
  }
  else
    v = _tcstoul(Str, &pEnd, 16), clr = ((v&0xff)<<16)|(v&0xff00)|((v&0xff0000)>>16);
  return clr;
}
#endif //~ NSIS_CONFIG_ENHANCEDUI_SUPPORT

static LANGID ParseLangId(const TCHAR*Str)
{
  const TCHAR *p = Str;
  if (_T('+') == *p || _T('-') == *p) ++p;
  return _T('0') == p[0] && _T('x') == (p[1]|32) ? LineParser::parse_int(Str) : _ttoi(Str);
}

LANGID CEXEBuild::ParseLangIdParameter(const LineParser&line, int token)
{
  int succ, lid = line.gettoken_int(token, &succ);
  if (!lid) lid = last_used_lang;
  if (!succ)
    warning_fl(DW_BAD_LANGID, _T("\"%") NPRIs _T("\" is not a valid language id, using language id %u!"), line.gettoken_str(token), lid);
  return lid;
}

int CEXEBuild::process_script(NIStream&Strm, const TCHAR *filename)
{
  NStreamLineReader linereader(Strm);
  curlinereader = &linereader;
  curfilename = filename;
  linecnt = 0;

  if (has_called_write_output)
  {
    ERROR_MSG(_T("Error (process_script): write_output already called, can't continue\n"));
    return PS_ERROR;
  }

#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
  set_date_time_predefines();
  TCHAR *oldfilename = set_file_predefine(curfilename);
  TCHAR *oldtimestamp = set_timestamp_predefine(curfilename);
#endif

  int ret=parseScript();

#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
  restore_file_predefine(oldfilename);
  restore_timestamp_predefine(oldtimestamp);
  del_date_time_predefines();
#endif

  curlinereader = 0;
  curfilename = 0;

  if (m_linebuild.getlen())
  {
    ERROR_MSG(_T("Error: invalid script: last line ended with \\\n"));
    return PS_ERROR;
  }

  if (ret == PS_EOF && num_ifblock())
  {
    ERROR_MSG(_T("!if[macro][n]def: open at EOF - need !endif\n"));
    return PS_ERROR;
  }

  return ret;
}

#define PRINTHELPEX(cmdname) { print_help((cmdname)); return PS_ERROR; }
#define PRINTHELP() PRINTHELPEX(line.gettoken_str(0))

void CEXEBuild::start_ifblock()
{
  ifblock ib = {0, };
  if (cur_ifblock)
    ib.inherited_ignore = cur_ifblock->ignore || cur_ifblock->inherited_ignore;
  int num = build_preprocessor_data.getlen() / sizeof(ifblock);
  build_preprocessor_data.add(&ib, sizeof(ifblock));
  cur_ifblock = (ifblock *) build_preprocessor_data.get() + num;
}

void CEXEBuild::end_ifblock()
{
  if (build_preprocessor_data.getlen())
  {
    cur_ifblock--;
    build_preprocessor_data.resize(build_preprocessor_data.getlen() - sizeof(ifblock));
    if (!build_preprocessor_data.getlen())
      cur_ifblock = 0;
  }
}

int CEXEBuild::num_ifblock()
{
  return build_preprocessor_data.getlen() / sizeof(ifblock);
}

int CEXEBuild::doParse(int verbosity, const TCHAR *fmt, ...)
{
  ExpandoString<TCHAR, NSIS_MAX_STRLEN> buf;
  int orgv = get_verbosity(), res;
  va_list val;
  va_start(val, fmt);
  buf.StrVFmt(fmt, val);
  va_end(val);
  if (verbosity >= 0) set_verbosity(verbosity);
  res = doParse(buf.GetPtr());
  set_verbosity(orgv);
  return res;
}

int CEXEBuild::doParse(const TCHAR *str)
{
  LineParser line(inside_comment);
  int res;

  while (*str == _T(' ') || *str == _T('\t')) str++;

  // remove trailing slash and null, if there's a previous line
  if (m_linebuild.getlen()>1)
    m_linebuild.resize(m_linebuild.getlen()-(2*sizeof(TCHAR)));

  // warn of comment with line-continuation
  if (m_linebuild.getlen())
  {
    LineParser prevline(inside_comment);
    prevline.parse((TCHAR*)m_linebuild.get());
    LineParser thisline(inside_comment);
    thisline.parse(str);

    if (prevline.inComment() && !thisline.inComment())
    {
      warning_fl(DW_COMMENT_NEWLINE, _T("comment contains line-continuation character, following line will be ignored"));
    }
  }

  // add new line to line buffer
  const unsigned int cchstr = (unsigned int) _tcslen(str);
  m_linebuild.add(str,(cchstr+1)*sizeof(TCHAR));

  // keep waiting for more lines if this line ends with a backslash
  if (str[0] && CharPrev(str,str+cchstr)[0] == _T('\\'))
  {
    return PS_OK;
  }

  // parse before checking if the line should be ignored, so block comments won't be missed
  
  // escaped quotes should be ignored for compile time commands that set defines
  // because defines can be inserted in commands at a later stage
  bool ignore_escaping = (!_tcsnicmp((TCHAR*)m_linebuild.get(),_T("!define"),7) || !_tcsncicmp((TCHAR*)m_linebuild.get(),_T("!insertmacro"),12));

  NStreamEncoding enc(NStreamEncoding::UNKNOWN);

  res=line.parse((TCHAR*)m_linebuild.get(), ignore_escaping, linecnt < 3 ? &enc : NULL);

  if (enc.GetCodepage() != NStreamEncoding::UNKNOWN && curlinereader)
      curlinereader->StreamEncoding().SafeSetCodepage(enc.GetCodepage());

  inside_comment = line.inCommentBlock();

  // if ignoring, ignore all lines that don't begin with an exclamation mark
  {
    bool ignore_line = cur_ifblock && (cur_ifblock->ignore || cur_ifblock->inherited_ignore);
    if (ignore_line)
    {
      TCHAR *rawline = (TCHAR*) m_linebuild.get(), first_char = *rawline, buf[30], *first_token = buf;
      if (!res)
        first_token = line.gettoken_str(0);
      else // LineParser::parse() failed so we cannot call gettoken_str but we still might need to ignore this line
        for (size_t i = 0; i < COUNTOF(buf); ++i) if ((buf[i] = rawline[i]) <= ' ') { buf[i] = _T('\0'); break; }
      if (first_char!=_T('!') || !is_ppbranch_token(first_token))
      {
        m_linebuild.resize(0);
        return PS_OK;
      }
    }
  }

  GrowBuf ppoline;
  if (preprocessonly) m_linebuild.swap(ppoline); // LineParser strips quotes and we need to display them
  m_linebuild.resize(0);

  if (res)
  {
    if (res==-2) ERROR_MSG(_T("Error: unterminated string parsing line at %") NPRIs _T(":%d\n"),curfilename,linecnt);
    else ERROR_MSG(_T("Error: error parsing line (%") NPRIs _T(":%d)\n"),curfilename,linecnt);
    return PS_ERROR;
  }

parse_again:
  if (line.getnumtokens() < 1) return PS_OK;

  const TCHAR* const tokstr0 = line.gettoken_str(0);
  int np,op,pos;
  int tkid=get_commandtoken(tokstr0,&np,&op,&pos);
  if (tkid == -1)
  {
    const TCHAR *p=tokstr0;
    if (p[0] && p[_tcslen(p)-1]==_T(':'))
    {
      if (p[0] == _T('!') || (p[0] >= _T('0') && p[0] <= _T('9')) || p[0] == _T('$') || p[0] == _T('-') || p[0] == _T('+'))
      {
        ERROR_MSG(_T("Invalid label: %") NPRIs _T(" (labels cannot begin with !, $, -, +, or 0-9)\n"),tokstr0);
        return PS_ERROR;
      }
      extern FILE *g_output;
      if (preprocessonly)
        _ftprintf(g_output,_T("%") NPRIs _T("\n"),tokstr0);
      else
        if (add_label(tokstr0)) return PS_ERROR;
      line.eattoken();
      goto parse_again;
    }

#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
    // We didn't recognise this command, could it be the name of a function exported from a dll?
    // Plugins cannot be called in global scope so there is no need to initialize the list first
    if (m_pPlugins && m_pPlugins->IsPluginCommand(tokstr0))
    {
      np   = 0;   // parameters are optional
      op   = -1;  // unlimited number of optional parameters
      pos  = -1;  // placement will tested later
      tkid = TOK__PLUGINCOMMAND;
    }
    else
#endif
    {
#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
      if (Plugins::IsPluginCallSyntax(tokstr0))
      {
        if (m_pPlugins && display_warnings) m_pPlugins->PrintPluginDirs();
        ERROR_MSG(_T("Plugin%") NPRIs _T(" not found, cannot call %") NPRIs _T("\n"),m_pPlugins && m_pPlugins->IsKnownPlugin(tokstr0) ? _T(" function") : _T(""),tokstr0);
      }
      else
#endif
        ERROR_MSG(_T("Invalid command: \"%") NPRIs _T("\"\n"),tokstr0);
      return PS_ERROR;
    }
  }

  if (IsTokenPlacedRight(pos, tokstr0) != PS_OK)
    return PS_ERROR;

  int v=line.getnumtokens()-(np+1);
  if (v < 0 || (op >= 0 && v > op)) // opt_parms is -1 for unlimited
  {
    ERROR_MSG(_T("%") NPRIs _T(" expects %d"),tokstr0,np);
    if (op < 0) ERROR_MSG(_T("+"));
    if (op > 0) ERROR_MSG(_T("-%d"),op+np);
    ERROR_MSG(_T(" parameters, got %d.\n"),line.getnumtokens()-1);
    PRINTHELP()
  }

  int if_from_else = 0;

  if (tkid == TOK_P_ELSE)
  {
    if (cur_ifblock && cur_ifblock->inherited_ignore)
      return PS_OK;

    if (!num_ifblock())
    {
      ERROR_MSG(_T("!else: no if block open (!if[macro][n][def])\n"));
      return PS_ERROR;
    }
    
    if (cur_ifblock->elseused)
    {
      ERROR_MSG(_T("!else: else already used in current if block\n"));
      return PS_ERROR;
    }

    if (cur_ifblock->hasexeced)
    {
      cur_ifblock->ignore++;
      return PS_OK;
    }

    if (line.getnumtokens() == 1)
    {
      cur_ifblock->ignore = !cur_ifblock->ignore;
      // if not executed up until now, it will now
      cur_ifblock->hasexeced++;
      cur_ifblock->elseused++;
      return PS_OK;
    }

    line.eattoken();

    int v=line.gettoken_enum(0,_T("if\0ifdef\0ifndef\0ifmacrodef\0ifmacrondef\0"));
    if (v < 0) PRINTHELP()
    if (line.getnumtokens() == 1) PRINTHELP()
    const int cmds[] = {TOK_P_IF, TOK_P_IFDEF, TOK_P_IFNDEF, TOK_P_IFMACRODEF, TOK_P_IFMACRONDEF};
    tkid = cmds[v];
    if_from_else++;
  }

  if (tkid == TOK_P_IFNDEF || tkid == TOK_P_IFDEF ||
      tkid == TOK_P_IFMACRODEF || tkid == TOK_P_IFMACRONDEF ||
      tkid == TOK_P_IF)
  {
    if (!if_from_else)
      start_ifblock();

    if (cur_ifblock && cur_ifblock->inherited_ignore)
    {
      return PS_OK;
    }

    int istrue=0, mod=0;

    if (tkid == TOK_P_IF) {
      res = pp_boolifyexpression(line, istrue, true);
      if (res != PS_OK) return res;
    }
    else {
  
      // pure left to right precedence. Not too powerful, but useful.
      for (int p = 1; p < line.getnumtokens(); p++)
      {
        if (p & 1)
        {
          bool new_s;
          if (tkid == TOK_P_IFNDEF || tkid == TOK_P_IFDEF)
            new_s=!!definedlist.find(line.gettoken_str(p));
          else
            new_s=MacroExists(line.gettoken_str(p));
          if (tkid == TOK_P_IFNDEF || tkid == TOK_P_IFMACRONDEF)
            new_s=!new_s;
  
          if (mod == 0) istrue = istrue || new_s;
          else istrue = istrue && new_s;
        }
        else
        {
          mod=line.gettoken_enum(p,_T("|\0&\0||\0&&\0"));
          if (mod == -1) PRINTHELP()
          mod &= 1;
        }
      }
    }

    if (istrue)
    {
      cur_ifblock->hasexeced++;
      cur_ifblock->ignore = 0;
    }
    else
      cur_ifblock->ignore++;

    return PS_OK;
  }
  if (tkid == TOK_P_ENDIF) {
    if (!num_ifblock())
    {
      ERROR_MSG(_T("!endif: no if block open (!if[macro][n][def])\n"));
      return PS_ERROR;
    }
    end_ifblock();
    return PS_OK;
  }
  if (!cur_ifblock || (!cur_ifblock->ignore && !cur_ifblock->inherited_ignore))
  {
    if (preprocessonly)
    {
      extern FILE *g_output;
      bool pptok = is_pp_token(tkid), docmd = pptok;
      bool both = TOK_P_VERBOSE == tkid || TOK_P_WARNING == tkid || TOK_P_ECHO == tkid;
      if (TOK_P_FINALIZE == tkid || TOK_P_UNINSTFINALIZE == tkid || TOK_P_PACKEXEHEADER == tkid) docmd = false;
      if (docmd && is_unsafe_pp_token(tkid) && preprocessonly > 0) docmd = false;
      if (!docmd || both) _ftprintf(g_output,(_T("%") NPRIs _T("\n")),ppoline.get());
      if (!docmd && !both) return PS_OK;
    }
    return doCommand(tkid,line);
  }

  return PS_OK;
}

#ifdef NSIS_FIX_DEFINES_IN_STRINGS
void CEXEBuild::ps_addtoline(const TCHAR *str, GrowBuf &linedata, StringList &hist, bool bIgnoreDefines /*= false*/)
#else
void CEXEBuild::ps_addtoline(const TCHAR *str, GrowBuf &linedata, StringList &hist)
#endif
{
  // convert $\r, $\n to their literals
  // preprocessor replace ${VAR} and $%VAR% with whatever value
  // note that if VAR does not exist, ${VAR} or $%VAR% will go through unmodified
  const TCHAR *in=str;
  while (*in)
  {
    int add=1;
    TCHAR c=*in, *t=CharNext(in);

    if (t-in > 1) // handle multibyte chars (no escape)
    {
      linedata.add((void*)in,truncate_cast(int, (size_t)((t-in)*sizeof(TCHAR))));
      in=t;
      continue;
    }
    in=t;

    if (c == _T('$'))
    {
      if (in[0] == _T('\\'))
      {
        if (in[1] == _T('r'))
          in+=2, c=_T('\r');
        else if (in[1] == _T('n'))
          in+=2, c=_T('\n');
        else if (in[1] == _T('t'))
          in+=2, c=_T('\t');
      }
      else if (in[0] == _T('{'))
      {
        TCHAR *s=_tcsdup(in+1), *t=s;
        MANAGE_WITH(s, free);
        unsigned int bn = 0;
        while (*t)
        {
          if (*t == _T('{')) bn++;
          if (*t == _T('}') && bn-- == 0) break;
          t=CharNext(t);
        }
        if (*t && t!=s
#ifdef NSIS_FIX_DEFINES_IN_STRINGS
          && !bIgnoreDefines
#endif
          )
        {
          *t=0;
          // check for defines inside the define name - ${bla${blo}}
          GrowBuf defname;
          ps_addtoline(s,defname,hist);
          defname.add(_T(""),sizeof(_T("")));
          t=definedlist.find((TCHAR*)defname.get());
          TCHAR dyndefbuf[10+1];
          if (!t)
          {
            if (_T('_')==s[0] && _T('_')==s[1])
            {
              if (!_tcscmp(s,_T("__COUNTER__")))
              {
                static unsigned long cntr=0;
                _stprintf(dyndefbuf,_T("%lu"),cntr++);
                t=dyndefbuf;
              }
            }
            if (_T('U')==s[0] && _T('+')==s[1])
            {
              TCHAR *n=s+2;
              UINT32 utf32=_tcstoul(n,&t,16);
              // We only want to accept "${U+HEXDIGITS}" and not "${U+ -HEXDIGITS }"
              if (*t || _T('-')==*n || _T('+')==*n) t=0;
              if (_T(' ')==*n || _T('\t')==*n) t=0; // TODO: _istspace()?
              if (!utf32) t=0; // Don't allow "${U+0}"
              if (t)
              {
                UINT32 codpts[]={utf32,UNICODE_REPLACEMENT_CHARACTER,'?'};
                for(size_t i=0, cch; i < COUNTOF(codpts); ++i)
                {
                  cch = WCFromCodePoint(dyndefbuf,COUNTOF(dyndefbuf),codpts[i]);
                  if (cch) { dyndefbuf[cch] = _T('\0'); break; }
                }
                t=dyndefbuf;
              }
            }
          }
          if (t && hist.find((TCHAR*)defname.get(),0)<0)
          {
            in+=_tcslen(s)+2;
            add=0;
            hist.add((TCHAR*)defname.get(),0);
#ifdef NSIS_FIX_DEFINES_IN_STRINGS
            ps_addtoline(t,linedata,hist,true);
#else
            ps_addtoline(t,linedata,hist);
#endif
            hist.delbypos(hist.find((TCHAR*)defname.get(),0));
          }
        }
      }
      else if (in[0] == _T('%'))
      {
        TCHAR *s=_tcsdup(in+1);
        MANAGE_WITH(s, free);
        TCHAR *t=s;
        while (*t)
        {
          if (*t == _T('%')) break;
          t=CharNext(t);
        }
        if (*t && t!=s)
        {
          *t=0;
          // check for defines inside the define name - ${bla${blo}}
          GrowBuf defname;
          ps_addtoline(s,defname,hist);
          defname.add(_T(""),sizeof(_T("")));
          t=_tgetenv((TCHAR*)defname.get());
          if (t && hist.find((TCHAR*)defname.get(),0)<0)
          {
            in+=_tcslen(s)+2;
            add=0;
            hist.add((TCHAR*)defname.get(),0);
#ifdef NSIS_FIX_DEFINES_IN_STRINGS
            ps_addtoline(t,linedata,hist,true);
#else
            ps_addtoline(t,linedata,hist);
#endif
            hist.delbypos(hist.find((TCHAR*)defname.get(),0));
          }
        }
      }
#ifdef NSIS_FIX_DEFINES_IN_STRINGS
      else if (in[0] == _T('$'))
      {
        if (in[1] == _T('{')) // Found $$ before - Don't replace this define
        {
          TCHAR *s=_tcsdup(in+2);
          MANAGE_WITH(s, free);
          TCHAR *t=s;
          unsigned int bn = 0;
          while (*t)
          {
            if (*t == _T('{')) bn++;
            if (*t == _T('}') && bn-- == 0) break;
            t=CharNext(t);
          }
          if (*t && t!=s)
          {
            *t=0;
            // add text unchanged
            GrowBuf defname;
            ps_addtoline(s,defname,hist);
            in++;
          }
        }
        else
        {
          linedata.add((void*)&c,1*sizeof(TCHAR));
          in++;
        }
      }
#endif
    }
    if (add) linedata.add((void*)&c,1*sizeof(TCHAR));
  }
}

int CEXEBuild::parseScript()
{
  assert(curlinereader);
  TCHAR *str = m_templinebuf;
  NStreamLineReader &linereader = *curlinereader;

  for (;;)
  {
    UINT lrres = linereader.ReadLine(str,MAX_LINELENGTH);
    linecnt++;
    if (NStream::OK != lrres)
    {
      if (linereader.IsEOF())
      {
        if (!str[0]) break;
      }
      else
      {
        ERROR_MSG(linereader.GetErrorMessage(lrres,curfilename,linecnt).c_str());
        return PS_ERROR;
      }
    }

    // remove trailing whitespace
    TCHAR *p = str;
    while (*p) p++;
    if (p > str) p--;
    while (p >= str && (*p == _T('\r') || *p == _T('\n') || *p == _T(' ') || *p == _T('\t'))) p--;
    *++p=0;

    StringList hist;
    GrowBuf linedata;

#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
    TCHAR *oldline = set_line_predefine(linecnt, FALSE);
#endif
    ps_addtoline(str,linedata,hist);
    linedata.add(_T(""),sizeof(_T("")));
    int ret=doParse((TCHAR*)linedata.get());
#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
    restore_line_predefine(oldline);
#endif

    if (ret != PS_OK) return ret;
  }

  return PS_EOF;
}

int CEXEBuild::LoadLicenseFile(const TCHAR *file, TCHAR** pdata, const TCHAR *cmdname, WORD AnsiCP) // caller must free *pdata, even on error result
{
  NIStream strm;
  if (!strm.OpenFileForReading(file)) 
  {
    ERROR_MSG(_T("%") NPRIs _T(": open failed \"%") NPRIs _T("\"\n"),cmdname,file);
    print_help(cmdname);
    return PS_ERROR;
  }
  FILE *f=strm.GetHandle();
  UINT cbBOMOffset=ftell(f); // We might be positioned after a BOM
  UINT32 cbFileSize=get_file_size32(f);
  UINT cbFileData=(invalid_file_size32 == cbFileSize) ? 0 : cbFileSize - cbBOMOffset;
  if (!cbFileData)
  {
    warning_fl(DW_LICENSE_EMPTY, _T("%") NPRIs _T(": empty license file \"%") NPRIs _T("\"\n"),cmdname,file);
  }
  else
    build_lockedunicodetarget=true;

  fseek(f,cbBOMOffset,SEEK_SET);
  UINT cbTotalData=sizeof(TCHAR)+cbFileData+sizeof(TCHAR); // SF_*+file+\0
  TCHAR*data=(TCHAR*)malloc(cbTotalData);
  *pdata=data; // memory will be released by caller
  if (!data)
  {
    ERROR_MSG(_T("Internal compiler error #12345: %") NPRIs _T(" malloc(%d) failed.\n"),cmdname,cbTotalData);
    return PS_ERROR;
  }
  *((TCHAR*)((char*)data+cbTotalData-sizeof(TCHAR)))=_T('\0');

  TCHAR*ldata=data+1;
  if (!strm.ReadOctets(ldata,&cbFileData))
  {
    ERROR_MSG(_T("%") NPRIs _T(": can't read file.\n"),cmdname);
    return PS_ERROR;
  }
  // We have to convert the content of the license file to wchar_t
  const WORD srccp=strm.StreamEncoding().IsUnicode() ? strm.StreamEncoding().GetCodepage() : AnsiCP;
  const UINT cbcu=NStreamEncoding::GetCodeUnitSize(srccp);
  if (sizeof(TCHAR) < cbcu)
  {
l_errwcconv:
    ERROR_MSG(_T("%") NPRIs _T(": wchar_t conversion failed!\n"),cmdname);
    return PS_ERROR;
  }
  // Create a fake character in the "header" part of the buffer (For DupWCFromBytes)
  char*lichdr=((char*)ldata) - cbcu;
  *((char*)lichdr)='X';
  if (cbcu > 1) *((WORD*)lichdr)='X';
  //BUGBUG: No room, cannot support UTF-32: if (cbcu > 2) *((UINT32*)lichdr)='X';
  const bool canOptRet = (char*)data == lichdr;
  wchar_t*wcdata=DupWCFromBytes(lichdr,cbcu+cbFileData,srccp|(canOptRet?DWCFBF_ALLOWOPTIMIZEDRETURN:0));
  if (!wcdata) goto l_errwcconv;
  if (wcdata != data) free(data);
  *pdata=data=wcdata, ldata=data+1;

  const bool isRTF=!memcmp(ldata,_T("{\\rtf"),5*sizeof(TCHAR));
  if (isRTF)
    *data=SF_RTF;
  else
    *data=build_unicode ? (SF_TEXT|SF_UNICODE) : (SF_TEXT);

  return PS_OK;
}

int CEXEBuild::process_oneline(const TCHAR *line, const TCHAR *filename, int linenum, unsigned int plflags)
{
  const TCHAR *last_filename = curfilename;
  int last_linecnt = linecnt;
  curfilename = filename, linecnt = linenum;

  StringList hist;
  GrowBuf linedata;

#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
  TCHAR *oldfilename = NULL, *oldtimestamp = NULL, *oldline = NULL;
  bool realfile = !(plflags & PLF_VIRTUALFILE);
  bool is_macro = (plflags & PLF_MACRO);
  bool setline = linenum != 0;

  if (setline) {
    if (!is_macro) {
      oldfilename = set_file_predefine(curfilename);
      if (realfile) oldtimestamp = set_timestamp_predefine(curfilename);
    }
    oldline = set_line_predefine(linecnt, is_macro);
  }
#endif

  ps_addtoline(line,linedata,hist);
  linedata.add(_T(""),sizeof(_T("")));
  int ret = doParse((TCHAR*)linedata.get());

#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
  if (setline) {
    if (!is_macro) {
      restore_file_predefine(oldfilename);
      if (realfile) restore_timestamp_predefine(oldtimestamp);
    }
    restore_line_predefine(oldline);
  }
#endif
  curfilename = last_filename, linecnt = last_linecnt;
  return ret;
}

int CEXEBuild::process_jump(LineParser &line, int wt, int *offs)
{
  const TCHAR *s=line.gettoken_str(wt);
  int v;

  if (!_tcsicmp(s,_T("0")) || !_tcsicmp(s,_T(""))) *offs=0;
  else if ((v=GetUserVarIndex(line, wt))>=0)
  {
    *offs=-v-1; // to jump to a user variable target, -variable_index-1 is stored.
  }
  else
  {
    if ((s[0] == _T('-') || s[0] == _T('+')) && !_ttoi(s+1))
    {
      ERROR_MSG(_T("Error: Goto targets beginning with '+' or '-' must be followed by nonzero integer (relative jump)\n"));
      return 1;
    }
    if ((s[0] >= _T('0') && s[0] <= _T('9')) || s[0] == _T('$') || s[0] == _T('!'))
    {
      ERROR_MSG(_T("Error: Goto targets cannot begin with 0-9, $, !\n"));
      return 1;
    }
    *offs=ns_label.add(s,0);
  }
  return 0;
}

#define SECTION_FIELD_GET(field) (FIELD_OFFSET(section, field)/sizeof(int))
#define SECTION_FIELD_SET(field) (-1 - (int)(FIELD_OFFSET(section, field)/sizeof(int)))

#define INVALIDREGROOT ( (HKEY) (UINT_PTR) 0x8000baad )
static HKEY ParseRegRootKey(LineParser &line, int tok)
{
  static const TCHAR *rootkeys[2] = {
    _T("HKCR\0HKLM\0HKCU\0HKU\0HKCC\0HKDD\0HKPD\0SHCTX\0HKCR32\0HKCR64\0HKCU32\0HKCU64\0HKLM32\0HKLM64\0HKCRANY\0HKCUANY\0HKLMANY\0SHCTX32\0SHCTX64\0SHCTXANY\0"),
    _T("HKEY_CLASSES_ROOT\0HKEY_LOCAL_MACHINE\0HKEY_CURRENT_USER\0HKEY_USERS\0HKEY_CURRENT_CONFIG\0HKEY_DYN_DATA\0HKEY_PERFORMANCE_DATA\0SHELL_CONTEXT\0")
  };
  static const HKEY rootkey_tab[] = {
    HKEY_CLASSES_ROOT,HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER,HKEY_USERS,HKEY_CURRENT_CONFIG,HKEY_DYN_DATA,HKEY_PERFORMANCE_DATA,HKSHCTX,HKCR32,HKCR64,HKCU32,HKCU64,HKLM32,HKLM64,HKCRANY,HKCUANY,HKLMANY,HKSHCTX32,HKSHCTX64,HKSHCTXANY
  };
  int k = line.gettoken_enum(tok, rootkeys[0]);
  if (k == -1) k = line.gettoken_enum(tok, rootkeys[1]);
  return k == -1 ? INVALIDREGROOT : rootkey_tab[k];
}

#define AFIE_LASTUSED ( -1 )
int CEXEBuild::add_flag_instruction_entry(int which_token, int opcode, LineParser &line, int offset, int data)
{
  entry ent = { opcode, };
  switch(opcode)
  {
    case EW_SETFLAG:
      ent.offsets[0] = offset;
      if (data != AFIE_LASTUSED) ent.offsets[1] = data; else ent.offsets[2] = 1;
      if (display_script) SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(1));
      return add_entry(&ent);
    case EW_IFFLAG:
      if (process_jump(line, 1, &ent.offsets[0]) || process_jump(line, 2, &ent.offsets[1])) PRINTHELP()
      ent.offsets[2]=offset;
      ent.offsets[3]=data;
      if (display_script) SCRIPT_MSG(_T("%") NPRIs _T(" ?%") NPRIs _T(":%") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(1), line.gettoken_str(2));
      return add_entry(&ent);
    case EW_GETFLAG:
      if ((ent.offsets[0] = GetUserVarIndex(line, 1)) < 0) PRINTHELP();
      ent.offsets[1] = offset;
      if (display_script) SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(1));
      return add_entry(&ent);
  }
  return PS_ERROR;
}

int CEXEBuild::doCommand(int which_token, LineParser &line)
{
#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
  if (PS_OK != initialize_default_plugins()) return PS_ERROR;
#endif

  multiple_entries_instruction=0;
  entry ent={0,};
  switch (which_token)
  {
    // macro stuff
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_P_MACRO:
    return pp_macro(line);
    case TOK_P_MACROEND:
    return (ERROR_MSG(_T("!macroend: no macro currently open.\n")), PS_ERROR);
    case TOK_P_MACROUNDEF:
    return pp_macroundef(line);
    case TOK_P_INSERTMACRO:
    return pp_insertmacro(line);

    // preprocessor files fun
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_P_TEMPFILE:
    return pp_tempfile(line);
    case TOK_P_DELFILE:
    return pp_delfile(line);
    case TOK_P_APPENDFILE:
    return pp_appendfile(line);
    case TOK_P_APPENDMEMFILE:
    return pp_appendmemfile(line);
    case TOK_P_GETDLLVERSION:
    case TOK_P_GETTLBVERSION:
    return pp_getversion(which_token, line);

    // page ordering stuff
    ///////////////////////////////////////////////////////////////////////////////
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
    case TOK_UNINSTPAGE:
      set_uninstall_mode(1);
    case TOK_PAGE:
      {
        if (!uninstall_mode) {
          enable_last_page_cancel = 0;
          if (!_tcsicmp(line.gettoken_str(line.getnumtokens()-1),_T("/ENABLECANCEL")))
            enable_last_page_cancel = 1;
        }
        else {
          uenable_last_page_cancel = 0;
          if (!_tcsicmp(line.gettoken_str(line.getnumtokens()-1),_T("/ENABLECANCEL")))
            uenable_last_page_cancel = 1;
        }

        int k = line.gettoken_enum(1,_T("custom\0license\0components\0directory\0instfiles\0uninstConfirm\0"));
        if (k < 0) PRINTHELP();
        if (add_page(k) != PS_OK)
          return PS_ERROR;

#ifndef NSIS_SUPPORT_CODECALLBACKS
        if (!k) {
          ERROR_MSG(_T("Error: custom page specified, NSIS_SUPPORT_CODECALLBACKS not defined.\n"));
          return PS_ERROR;
        }
#endif //~ NSIS_SUPPORT_CODECALLBACKS
        if (k) { // not custom
#ifdef NSIS_SUPPORT_CODECALLBACKS
          switch (line.getnumtokens() - enable_last_page_cancel) {
            case 6:
              PRINTHELP();
            case 5:
              if (*line.gettoken_str(4))
                cur_page->leavefunc = ns_func.add(line.gettoken_str(4),0);
            case 4:
              if (*line.gettoken_str(3))
                cur_page->showfunc = ns_func.add(line.gettoken_str(3),0);
            case 3:
              if (*line.gettoken_str(2))
                cur_page->prefunc = ns_func.add(line.gettoken_str(2),0);
          }
#endif //~ NSIS_SUPPORT_CODECALLBACKS
        }
#ifdef NSIS_SUPPORT_CODECALLBACKS
        else { // a custom page
          switch (line.getnumtokens() - enable_last_page_cancel) {
            case 6:
              PRINTHELP();
            case 5:
              cur_page->caption = add_string(line.gettoken_str(4));
            case 4:
              if (*line.gettoken_str(3))
                cur_page->leavefunc = ns_func.add(line.gettoken_str(3),0);
            case 3:
              if (*line.gettoken_str(2))
                cur_page->prefunc = ns_func.add(line.gettoken_str(2),0);
              break;
            case 2:
              ERROR_MSG(_T("Error: custom page must have a creator function!\n"));
              PRINTHELP();
          }
        }
#endif //~ NSIS_SUPPORT_CODECALLBACKS

        SCRIPT_MSG(_T("%") NPRIs _T("Page: %") NPRIs, uninstall_mode?_T("Uninst"):_T(""), line.gettoken_str(1));
#ifdef NSIS_SUPPORT_CODECALLBACKS
        if (cur_page->prefunc>=0)
          SCRIPT_MSG(_T(" (%") NPRIs _T(":%") NPRIs _T(")"), k?_T("pre"):_T("creator"), line.gettoken_str(2));
        if (cur_page->showfunc>=0 && k)
          SCRIPT_MSG(_T(" (show:%") NPRIs _T(")"), line.gettoken_str(3));
        if (cur_page->leavefunc>=0)
          SCRIPT_MSG(_T(" (leave:%") NPRIs _T(")"), line.gettoken_str(4-!k));
        else if (cur_page->caption && !k)
          SCRIPT_MSG(_T(" (caption:%") NPRIs _T(")"), line.gettoken_str(3));
#endif
        SCRIPT_MSG(_T("\n"));

        page_end();

        if (k == PAGE_INSTFILES) {
          add_page(PAGE_COMPLETED);
          page_end();
        }
        set_uninstall_mode(0);
      }
    return PS_OK;
    case TOK_PAGEEX: // extended page setting
    {
      int k = line.gettoken_enum(1,_T("custom\0license\0components\0directory\0instfiles\0uninstConfirm\0"));
      if (k < 0) {
        k = line.gettoken_enum(1,_T("un.custom\0un.license\0un.components\0un.directory\0un.instfiles\0un.uninstConfirm\0"));
        if (k < 0) PRINTHELP();
        set_uninstall_mode(1);
      }
      SCRIPT_MSG(_T("PageEx: %") NPRIs _T("\n"), line.gettoken_str(1));
      if (add_page(k) != PS_OK)
        return PS_ERROR;

      cur_page->flags |= PF_PAGE_EX;
    }
    return PS_OK;
    case TOK_PAGEEXEND:
    {
      SCRIPT_MSG(_T("PageExEnd\n"));
#ifdef NSIS_SUPPORT_CODECALLBACKS
      if (cur_page_type == PAGE_CUSTOM && !cur_page->prefunc) {
        ERROR_MSG(_T("Error: custom pages must have a creator function.\n"));
        return PS_ERROR;
      }
#endif
      page_end();

      if (cur_page_type == PAGE_INSTFILES) {
        add_page(PAGE_COMPLETED);
        page_end();
      }
      set_uninstall_mode(0);
    }
    return PS_OK;
    case TOK_PAGECALLBACKS:
#ifdef NSIS_SUPPORT_CODECALLBACKS
    {
      SCRIPT_MSG(_T("PageCallbacks:"));
      if (cur_page_type == PAGE_CUSTOM)
      {
        switch (line.getnumtokens())
        {
          case 4:
            PRINTHELP();
          case 3:
          {
            if (*line.gettoken_str(2))
            {
              if (_tcsnicmp(line.gettoken_str(2), _T("un."), 3))
              {
                if (uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              else
              {
                if (!uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              cur_page->leavefunc = ns_func.add(line.gettoken_str(2),0);
            }
          }
          case 2:
          {
            if (*line.gettoken_str(1))
            {
              if (_tcsnicmp(line.gettoken_str(1), _T("un."), 3))
              {
                if (uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              else
              {
                if (!uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              cur_page->prefunc = ns_func.add(line.gettoken_str(1),0);
            }
          }
        }
      }
      else
      {
        switch (line.getnumtokens())
        {
          case 4:
          {
            if (*line.gettoken_str(3))
            {
              if (_tcsnicmp(line.gettoken_str(3), _T("un."), 3))
              {
                if (uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              else
              {
                if (!uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              cur_page->leavefunc = ns_func.add(line.gettoken_str(3),0);
            }
          }
          case 3:
          {
            if (*line.gettoken_str(2))
            {
              if (_tcsnicmp(line.gettoken_str(2), _T("un."), 3))
              {
                if (uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              else
              {
                if (!uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              cur_page->showfunc = ns_func.add(line.gettoken_str(2),0);
            }
          }
          case 2:
          {
            if (*line.gettoken_str(1))
            {
              if (_tcsnicmp(line.gettoken_str(1), _T("un."), 3))
              {
                if (uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              else
              {
                if (!uninstall_mode)
                {
                  ERROR_MSG(_T("\nError: function names must start with \"un.\" in an uninstall page.\n"));
                  return PS_ERROR;
                }
              }
              cur_page->prefunc = ns_func.add(line.gettoken_str(1),0);
            }
          }
        }
      }
      int custom = cur_page_type == PAGE_CUSTOM ? 1 : 0;
      if (cur_page->prefunc>=0)
        SCRIPT_MSG(_T(" %") NPRIs _T(":%") NPRIs, !custom?_T("pre"):_T("creator"), line.gettoken_str(1));
      if (cur_page->showfunc>=0 && !custom)
        SCRIPT_MSG(_T(" show:%") NPRIs, line.gettoken_str(2));
      if (cur_page->leavefunc>=0)
        SCRIPT_MSG(_T(" leave:%") NPRIs, line.gettoken_str(3-custom));
      SCRIPT_MSG(_T("\n"));
    }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_CODECALLBACKS not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_CODECALLBACKS
#else
    case TOK_PAGE:
    case TOK_UNINSTPAGE:
    case TOK_PAGEEX:
    case TOK_PAGEEXEND:
    case TOK_PAGECALLBACKS:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_VISIBLE_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_VISIBLE_SUPPORT

    // header flags
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_LANGSTRING:
    {
      TCHAR *name = line.gettoken_str(1);
      LANGID lang = ParseLangIdParameter(line, 2);
      TCHAR *str = line.gettoken_str(3);
      const int ret = SetLangString(name, lang, str);
      if (ret == PS_WARNING)
        warning_fl(DW_LANGSTRING_MULTISETWASTE, _T("LangString \"%") NPRIs _T("\" set multiple times for %d, wasting space"), name, lang);
      else if (ret == PS_ERROR) {
        ERROR_MSG(_T("Error: can't set LangString \"%") NPRIs _T("\"!\n"), name);
        return PS_ERROR;
      }
      SCRIPT_MSG(_T("LangString: \"%") NPRIs _T("\" %d \"%") NPRIs _T("\"\n"), name, lang, str);
    }
    return PS_OK;
    case TOK_LANGSTRINGUP:
      SCRIPT_MSG(_T("Error: LangStringUP is obsolete, there are no more unprocessed strings. Use LangString.\n"));
    return PS_ERROR;
    case TOK_LICENSELANGSTRING:
    {
      const TCHAR *cmdnam = get_commandtoken_name(which_token);
#ifdef NSIS_CONFIG_SILENT_SUPPORT
      if (build_header.flags&(CH_FLAGS_SILENT|CH_FLAGS_SILENT_LOG))
        warning_fl(DW_LANGSTRING_SILENTLICENSE, _T("%") NPRIs _T(": SilentInstall enabled, wasting space"), cmdnam);
#endif
      TCHAR *name = line.gettoken_str(1);
      LANGID lang = ParseLangIdParameter(line, 2);
      TCHAR *file = line.gettoken_str(3), *data = NULL;
      MANAGE_WITH(data, free);

      LanguageTable *pLT = GetLangTable(lang);
      WORD acp = pLT ? pLT->nlf.m_uCodePage : CP_ACP;
      int ret = LoadLicenseFile(file, &data, cmdnam, acp);
      if (ret != PS_OK)
        return ret;

      ret = SetLangString(name, lang, data, true);
      if (ret == PS_WARNING)
        warning_fl(DW_LANGSTRING_MULTISETWASTE, _T("%") NPRIs _T(" \"%") NPRIs _T("\" set multiple times for %d, wasting space"), cmdnam, name, lang);
      else if (ret == PS_ERROR)
      {
        ERROR_MSG(_T("Error: can't set %") NPRIs _T(" \"%") NPRIs _T("\"!\n"), cmdnam, name);
        return PS_ERROR;
      }

      SCRIPT_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\" %d \"%") NPRIs _T("\"\n"), cmdnam, name, lang, file);
      return PS_OK;
    }
    return PS_OK;
    case TOK_NAME:
      {
        if (SetInnerString(NLF_NAME,line.gettoken_str(1)) == PS_WARNING)
          warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
        tstring da;
        const TCHAR *normstr = line.gettoken_str(1), *dastr = line.gettoken_str(2);
        if (!*dastr && _tcschr(normstr,_T('&'))) dastr = (da = replace_all(normstr,_T("&"),_T("&&"))).c_str();
        SetInnerString(NLF_NAME_DA,dastr);
        SCRIPT_MSG(_T("Name: \"%") NPRIs _T("\""),line.gettoken_str(1));
        if (*line.gettoken_str(2))
          SCRIPT_MSG(_T(" \"%") NPRIs _T("\""),line.gettoken_str(2));
        SCRIPT_MSG(_T("\n"));
      }
    return PS_OK;
    case TOK_CAPTION:
      {
        if (!cur_page)
        {
          if (SetInnerString(NLF_CAPTION,line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
        }
        else
        {
          cur_page->caption = add_string(line.gettoken_str(1));
        }
        SCRIPT_MSG(_T("Caption: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      }
    return PS_OK;
    case TOK_ICON:
      SCRIPT_MSG(_T("Icon: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      try {
        free_loaded_icon(installer_icon);
        installer_icon = load_icon(line.gettoken_str(1));
      }
      catch (exception& err) {
        ERROR_MSG(_T("Error while loading icon from \"%") NPRIs _T("\": %") NPRIs _T("\n"), line.gettoken_str(1), CtoTStrParam(err.what()));
        return PS_ERROR;
      }
    return PS_OK;
#ifdef NSIS_CONFIG_COMPONENTPAGE
    case TOK_CHECKBITMAP:
      SCRIPT_MSG(_T("CheckBitmap: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      try {
        init_res_editor();
        int err = update_bitmap(res_editor, IDB_BITMAP1, line.gettoken_str(1), 96, 16, 8);
        if (err) {
          switch (err) {
            case -1:
              ERROR_MSG(_T("Error: can't find bitmap\n"));
              break;
            case -2:
              ERROR_MSG(_T("Error: invalid bitmap file - corrupted or not a bitmap\n"));
              break;
            case -3:
              ERROR_MSG(_T("Error: bitmap isn't 96x16 in size\n"));
              break;
            case -4:
              ERROR_MSG(_T("Error: bitmap has more than 8bpp\n"));
              break;
          }
          return PS_ERROR;
        }
      }
      catch (exception& err) {
        ERROR_MSG(_T("Error while replacing bitmap: %") NPRIs _T("\n"), CtoTStrParam(err.what()));
        return PS_ERROR;
      }
    return PS_OK;
#else
    case TOK_CHECKBITMAP:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_COMPONENTPAGE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_COMPONENTPAGE
    case TOK_DIRTEXT:
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
      {
        if (!cur_page) {
          if (SetInnerString(NLF_DIR_TEXT, line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
          if (line.getnumtokens() > 2)
            SetInnerString(NLF_DIR_SUBTEXT, line.gettoken_str(2));
          if (line.getnumtokens() > 3)
            SetInnerString(NLF_BTN_BROWSE, line.gettoken_str(3));
          if (line.getnumtokens() > 4)
            SetInnerString(NLF_DIR_BROWSETEXT, line.gettoken_str(4));
        }
        else {
          if (cur_page_type != PAGE_DIRECTORY) {
            ERROR_MSG(_T("Error: DirText can only be used inside PageEx directory.\n"));
            return PS_ERROR;
          }
          cur_page->parms[0] = add_string(line.gettoken_str(1));
          if (line.getnumtokens() > 2)
            cur_page->parms[1] = add_string(line.gettoken_str(2));
          if (line.getnumtokens() > 3)
            cur_page->parms[2] = add_string(line.gettoken_str(3));
          if (line.getnumtokens() > 4)
            cur_page->parms[3] = add_string(line.gettoken_str(4));
        }
        SCRIPT_MSG(_T("DirText: \"%") NPRIs _T("\" \"%") NPRIs _T("\" \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
      }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_VISIBLE_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_VISIBLE_SUPPORT
    case TOK_DIRVAR:
    {
      if (cur_page_type != PAGE_DIRECTORY && cur_page_type != PAGE_UNINSTCONFIRM) {
        ERROR_MSG(_T("Error: can't use DirVar outside of PageEx directory|uninstConfirm.\n"));
        return PS_ERROR;
      }
      cur_page->parms[4] = GetUserVarIndex(line, 1) + 1;
      if (cur_page->parms[4] <= 0) PRINTHELP();
      SCRIPT_MSG(_T("DirVar: %") NPRIs _T("\n"), line.gettoken_str(1));
    }
    return PS_OK;
    case TOK_DIRVERIFY:
    {
      if (cur_page_type != PAGE_DIRECTORY) {
        ERROR_MSG(_T("Error: can't use DirVerify outside of PageEx directory.\n"));
        return PS_ERROR;
      }
      cur_page->flags &= ~PF_DIR_NO_BTN_DISABLE;
      int k = line.gettoken_enum(1,_T("auto\0leave\0"));
      if (k == -1)
        PRINTHELP();
      if (k)
        cur_page->flags |= PF_DIR_NO_BTN_DISABLE;
      SCRIPT_MSG(_T("DirVerify: %") NPRIs _T("\n"), line.gettoken_str(1));
    }
    return PS_OK;
    case TOK_GETINSTDIRERROR:
      return add_flag_instruction_entry(which_token, EW_GETFLAG, line, FLAG_OFFSET(instdir_error));
#ifdef NSIS_CONFIG_COMPONENTPAGE
    case TOK_COMPTEXT:
      {
        if (!cur_page) {
          if (SetInnerString(NLF_COMP_TEXT, line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
          if (line.getnumtokens() > 2)
            SetInnerString(NLF_COMP_SUBTEXT1, line.gettoken_str(2));
          if (line.getnumtokens() > 3)
            SetInnerString(NLF_COMP_SUBTEXT2, line.gettoken_str(3));
        }
        else {
          if (cur_page_type != PAGE_COMPONENTS) {
            ERROR_MSG(_T("Error: ComponentText can only be used inside PageEx components.\n"));
            return PS_ERROR;
          }
          cur_page->parms[0] = add_string(line.gettoken_str(1));
          cur_page->parms[1] = add_string(line.gettoken_str(2));
          cur_page->parms[2] = add_string(line.gettoken_str(3));
          cur_page->parms[3] = cur_page->parms[1];
          cur_page->parms[4] = cur_page->parms[2];
        }
        SCRIPT_MSG(_T("ComponentText: \"%") NPRIs _T("\" \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
      }
    return PS_OK;
    case TOK_INSTTYPE:
      {
        int x;
        if (!_tcsicmp(line.gettoken_str(1),_T("/NOCUSTOM")))
        {
          build_header.flags|=CH_FLAGS_NO_CUSTOM;
          SCRIPT_MSG(_T("InstType: disabling custom %") NPRIs _T("install type\n"), _T(""));
        }
        else if (!_tcsicmp(line.gettoken_str(1),_T("/UNINSTNOCUSTOM")))
        {
          build_uninst.flags|=CH_FLAGS_NO_CUSTOM;
          SCRIPT_MSG(_T("InstType: disabling custom %") NPRIs _T("install type\n"), _T("un"));
        }
        else if (!_tcsicmp(line.gettoken_str(1),_T("/COMPONENTSONLYONCUSTOM")))
        {
          build_header.flags|=CH_FLAGS_COMP_ONLY_ON_CUSTOM;
          SCRIPT_MSG(_T("InstType: making components viewable only on custom %") NPRIs _T("install type\n"), _T(""));
        }
        else if (!_tcsicmp(line.gettoken_str(1),_T("/UNINSTCOMPONENTSONLYONCUSTOM")))
        {
          build_uninst.flags|=CH_FLAGS_COMP_ONLY_ON_CUSTOM;
          SCRIPT_MSG(_T("InstType: making components viewable only on custom %") NPRIs _T("install type\n"), _T("un"));
        }
        else if (!_tcsnicmp(line.gettoken_str(1),_T("/CUSTOMSTRING="),14))
        {
          SCRIPT_MSG(_T("InstType: setting custom text to: \"%") NPRIs _T("\"\n"),line.gettoken_str(1)+14);
          if (SetInnerString(NLF_COMP_CUSTOM,line.gettoken_str(1)+14) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),_T("InstType /CUSTOMSTRING"));
        }
        else if (line.gettoken_str(1)[0]==_T('/'))
        {
          PRINTHELP()
        }
        else
        {
          const TCHAR *itname = line.gettoken_str(1), *defname = line.gettoken_str(2), setdef = *defname, *eqstr = setdef ? _T("=") : _T("");
          if (!_tcsnicmp(itname, _T("un."), 3)) set_uninstall_mode(1), itname += 3;

          for (x = 0; x < NSIS_MAX_INST_TYPES && cur_header->install_types[x]; x++);
          if (x == NSIS_MAX_INST_TYPES)
          {
            ERROR_MSG(_T("InstType: no more than %d install types allowed. %d specified\n"), NSIS_MAX_INST_TYPES, NSIS_MAX_INST_TYPES + 1);
            return PS_ERROR;
          }
          else
          {
            cur_header->install_types[x] = add_string(itname);
            if (setdef) definedlist.set_si32(defname, x);
            SCRIPT_MSG(_T("InstType: %") NPRIs _T("\"%") NPRIs _T("\" (%") NPRIs _T("%") NPRIs _T("%d)\n"), uninstall_mode ? _T("(uninstall) ") : _T(""), itname, defname, eqstr, x);
          }

          set_uninstall_mode(0);
        }
      }
    return PS_OK;
#else
    case TOK_COMPTEXT:
    case TOK_INSTTYPE:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified but NSIS_CONFIG_COMPONENTPAGE not defined\n"),line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_COMPONENTPAGE
#ifdef NSIS_CONFIG_LICENSEPAGE
    case TOK_LICENSETEXT:
      {
        if (!cur_page) {
          if (SetInnerString(NLF_LICENSE_TEXT, line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
          SetInnerString(NLF_LICENSE_TEXT_FSRB, line.gettoken_str(1));
          SetInnerString(NLF_LICENSE_TEXT_FSCB, line.gettoken_str(1));
          if (line.getnumtokens() > 2)
            SetInnerString(NLF_BTN_LICENSE, line.gettoken_str(2));
        }
        else {
          if (cur_page_type != PAGE_LICENSE) {
            ERROR_MSG(_T("Error: LicenseText can only be used inside PageEx license.\n"));
            return PS_ERROR;
          }
          cur_page->parms[0] = add_string(line.gettoken_str(1));
          cur_page->next = add_string(line.gettoken_str(2));
        }
        SCRIPT_MSG(_T("LicenseText: \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2));
      }
    return PS_OK;
    case TOK_LICENSEDATA:
      {
        const TCHAR *cmdnam = get_commandtoken_name(which_token);
        int idx = 0;
        TCHAR *file = line.gettoken_str(1), *data = NULL, *filedata = NULL;
        MANAGE_WITH(filedata, free);
        WORD wincp = CP_ACP;

        if (file[0] == _T('$') && file[1] == _T('('))
        {
          TCHAR *cp = _tcsdup(file+2);
          MANAGE_WITH(cp, free);
          TCHAR *p = _tcschr(cp, _T(')'));
          if (p && p[1] == 0) { // if string is only a language str identifier
            *p = 0;
            idx = DefineLangString(cp, 0);
          }
          data = file;
        }

        if (!idx)
        {
          int ret = LoadLicenseFile(file, &filedata, cmdnam, wincp);
          if (ret != PS_OK)
            return ret;
          data = filedata;
        }

        if (!cur_page) {
          if (SetInnerString(NLF_LICENSE_DATA,data) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),cmdnam);
        }
        else {
          if (cur_page_type != PAGE_LICENSE) {
            ERROR_MSG(_T("Error: LicenseData can only be used inside PageEx license.\n"));
            return PS_ERROR;
          }
          cur_page->parms[1] = add_string(data, false, wincp);
        }

        SCRIPT_MSG(_T("LicenseData: \"%") NPRIs _T("\"\n"),file);
      }
    return PS_OK;
    case TOK_LICENSEFORCESELECTION:
    {
      int k=line.gettoken_enum(1,_T("off\0checkbox\0radiobuttons\0"));
      if (k == -1) PRINTHELP()
      if (k < line.getnumtokens() - 2) PRINTHELP()

      if (!cur_page) {
        switch (line.getnumtokens()) {
          case 4:
            SetInnerString(NLF_BTN_LICENSE_DISAGREE, line.gettoken_str(3));
          case 3:
            SetInnerString(NLF_BTN_LICENSE_AGREE, line.gettoken_str(2));
            break;
        }
        switch (k) {
          case 0: license_res_id = IDD_LICENSE; break;
          case 1: license_res_id = IDD_LICENSE_FSCB; break;
          case 2: license_res_id = IDD_LICENSE_FSRB; break;
        }
      }
      else {
        if (cur_page_type != PAGE_LICENSE) {
          ERROR_MSG(_T("Error: LicenseForceSelection can only be used inside PageEx license.\n"));
          return PS_ERROR;
        }
        switch (line.getnumtokens()) {
          case 4:
            cur_page->parms[3] = add_string(line.gettoken_str(3));
          case 3:
            cur_page->parms[2] = add_string(line.gettoken_str(2));
            break;
        }

        cur_page->flags &= ~(PF_LICENSE_FORCE_SELECTION | PF_LICENSE_NO_FORCE_SELECTION);
        switch (k) {
          case 0:
            cur_page->dlg_id = IDD_LICENSE;
            cur_page->flags |= PF_LICENSE_NO_FORCE_SELECTION;
            break;
          case 1:
            cur_page->dlg_id = IDD_LICENSE_FSCB;
            cur_page->flags |= PF_LICENSE_FORCE_SELECTION;
            break;
          case 2:
            cur_page->dlg_id = IDD_LICENSE_FSRB;
            cur_page->flags |= PF_LICENSE_FORCE_SELECTION;
            break;
        }
      }
      SCRIPT_MSG(_T("LicenseForceSelection: %") NPRIs _T(" \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"), line.gettoken_str(1), line.gettoken_str(2), line.gettoken_str(3));
    }
    return PS_OK;
    case TOK_LICENSEBKCOLOR:
      {
        const TCHAR *cmdname = _T("LicenseBkColor"), *paramname;
        TCHAR *p = line.gettoken_str(1), *p2;
        if (!_tcsicmp(p,paramname = _T("/windows")) || !_tcsicmp(p,_T("/window")))
        {
          build_header.license_bg=-COLOR_WINDOW;
          SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"),cmdname,paramname);
        }
        else if (!_tcsicmp(p,paramname = _T("/grey")) || !_tcsicmp(p,_T("/gray")))
        {
          build_header.license_bg=-COLOR_BTNFACE; /* Note: This might not actually be gray */
          SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"),cmdname,paramname);
        }
        else
        {
          const int v=_tcstoul(p,&p2,16);
          if (p2 == p) return PS_ERROR;
          build_header.license_bg=((v&0xff)<<16)|(v&0xff00)|((v&0xff0000)>>16);
          SCRIPT_MSG(_T("%") NPRIs _T(": %06X\n"),cmdname,v);
        }
        build_uninst.license_bg=build_header.license_bg;
      }
    return PS_OK;
#else
    case TOK_LICENSETEXT:
    case TOK_LICENSEDATA:
    case TOK_LICENSEBKCOLOR:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_LICENSEPAGE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_LICENSEPAGE
#ifdef NSIS_CONFIG_SILENT_SUPPORT
    case TOK_SILENTINST:
    {
      int k=line.gettoken_enum(1,_T("normal\0silent\0silentlog\0"));
      if (k<0) PRINTHELP()
#ifndef NSIS_CONFIG_LOG
      if (k == 2)
      {
        ERROR_MSG(_T("SilentInstall: silentlog specified, no log support compiled in (use NSIS_CONFIG_LOG)\n"));
        return PS_ERROR;
      }
#endif //~ NSIS_CONFIG_LOG
      SCRIPT_MSG(_T("SilentInstall: %") NPRIs _T("\n"),line.gettoken_str(1));
#ifdef NSIS_CONFIG_LICENSEPAGE
      if (k && HasUserDefined(NLF_LICENSE_DATA))
      {
        warning_fl(DW_LANGSTRING_SILENTLICENSE, _T("SilentInstall: LicenseData already specified. wasting space"));
      }
      if (k) {
        build_header.flags|=CH_FLAGS_SILENT;
        if (k == 2)
          build_header.flags|=CH_FLAGS_SILENT_LOG;
      }
      else {
        build_header.flags&=~CH_FLAGS_SILENT;
        build_header.flags&=~CH_FLAGS_SILENT_LOG;
      }
#endif //~ NSIS_CONFIG_LICENSEPAGE
    }
    return PS_OK;
    case TOK_SILENTUNINST:
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
    {
      int k=line.gettoken_enum(1,_T("normal\0silent\0"));
      if (k<0) PRINTHELP()
      if (k)
        build_uninst.flags|=CH_FLAGS_SILENT;
      else
        build_uninst.flags&=~CH_FLAGS_SILENT;
      SCRIPT_MSG(_T("SilentUnInstall: %") NPRIs _T("\n"),line.gettoken_str(1));
    }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_UNINSTALL_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif
    case TOK_IFSILENT:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(silent), ~0); //new value mask - keep flag
    case TOK_SETSILENT:
      ent.offsets[1] = line.gettoken_enum(1,_T("normal\0silent\0"));
      if (ent.offsets[1] < 0 ) PRINTHELP()
      return add_flag_instruction_entry(which_token, EW_SETFLAG, line, FLAG_OFFSET(silent), add_intstring(ent.offsets[1]));
#else 
    case TOK_SILENTINST:
    case TOK_SILENTUNINST:
    case TOK_IFSILENT:
    case TOK_SETSILENT:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_SILENT_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_SILENT_SUPPORT
    case TOK_IFRTLLANG:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(rtl), ~0); //new value mask - keep flag
    case TOK_OUTFILE:
      my_strncpy(build_output_filename,line.gettoken_str(1),COUNTOF(build_output_filename));
      SCRIPT_MSG(_T("OutFile: \"%") NPRIs _T("\"\n"),build_output_filename);
    return PS_OK;
    case TOK_INSTDIR:
    {
      TCHAR *p = line.gettoken_str(1);
      if (build_header.install_directory_ptr)
      {
        warning_fl(DW_STRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times. wasting space"),line.gettoken_str(0));
      }
      build_header.install_directory_ptr = add_string(p);
      build_header.install_directory_auto_append = 0;
      TCHAR *p2 = p + _tcslen(p);
      if (*p && *CharPrev(p, p2) != _T('\\'))
      {
        // we risk hitting $\r or something like $(bla\ad) or ${bla\ad} here, but it's better
        // than hitting backslashes in processed strings
        while (p2 > p && *p2 != _T('\\'))
          p2 = CharPrev(p, p2);
        if (*p2 == _T('\\'))
        {
          build_header.install_directory_auto_append = add_string(p2 + 1);
        }
      }
      SCRIPT_MSG(_T("InstallDir: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
    }
    return PS_OK;
    case TOK_INSTALLDIRREGKEY: // InstallDirRegKey
      {
        if (build_header.install_reg_key_ptr)
          warning_fl(DW_STRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));

        HKEY hRK = ParseRegRootKey(line,1);
        if (INVALIDREGROOT == hRK) PRINTHELP()
        if (HKSHCTX == hRK) PRINTHELP() // SHCTX is invalid here
        if (IsRegRootkeyForcedView(hRK)) PRINTHELP() // 32|64 views are also invalid
        build_header.install_reg_rootkey=REGROOTKEYTOINT(hRK);
        build_header.install_reg_key_ptr = add_string(line.gettoken_str(2),0);
        if (line.gettoken_str(2)[0] == _T('\\'))
          warning_fl(DW_PARSE_REGPATHPREFIX, _T("%") NPRIs _T(": registry path name begins with \'\\\', may cause problems"),line.gettoken_str(0));
        build_header.install_reg_value_ptr = add_string(line.gettoken_str(3),0);
        SCRIPT_MSG(_T("InstallRegKey: \"%") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
      }
    return PS_OK;
    case TOK_CRCCHECK:
      build_crcchk=line.gettoken_enum(1,_T("off\0on\0force\0"));
      if (build_crcchk==-1) PRINTHELP()
      SCRIPT_MSG(_T("CRCCheck: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
    case TOK_INSTPROGRESSFLAGS:
      {
        int smooth=0;
        build_header.flags&=~CH_FLAGS_PROGRESS_COLORED;
        for (int x = 1; x < line.getnumtokens(); x++)
        {
          if (!_tcsicmp(line.gettoken_str(x),_T("smooth"))) smooth=1;
          else if (!_tcsicmp(line.gettoken_str(x),_T("colored"))) build_header.flags|=CH_FLAGS_PROGRESS_COLORED;
          else PRINTHELP()
        }
        try {
          init_res_editor();

          BYTE* dlg = res_editor->GetResource(RT_DIALOG, IDD_INSTFILES, NSIS_DEFAULT_LANG);
          if (!dlg) throw runtime_error("IDD_INSTFILES doesn't exist!");
          CDialogTemplate dt(dlg,build_unicode,uDefCodePage);
          res_editor->FreeResource(dlg);
          DialogItemTemplate* progress = dt.GetItem(IDC_PROGRESS);
          if (!progress) throw runtime_error("IDC_PROGRESS doesn't exist!");

          if (smooth) progress->dwStyle |= PBS_SMOOTH; else progress->dwStyle &= ~PBS_SMOOTH;

          DWORD dwSize;
          dlg = dt.Save(dwSize);
          res_editor->UpdateResource(RT_DIALOG, IDD_INSTFILES, NSIS_DEFAULT_LANG, dlg, dwSize);
          dt.FreeSavedTemplate(dlg);
        }
        catch (exception& err) {
          ERROR_MSG(_T("Error setting smooth progress bar: %") NPRIs _T("\n"), CtoTStrParam(err.what()));
          return PS_ERROR;
        }
        SCRIPT_MSG(_T("InstProgressFlags: smooth=%d, colored=%d\n"),smooth,
          !!(build_header.flags&CH_FLAGS_PROGRESS_COLORED));
      }
    return PS_OK;
    case TOK_AUTOCLOSE:
      {
        int k=line.gettoken_enum(1,_T("false\0true\0"));
        if (k == -1) PRINTHELP();
        if (k)
          build_header.flags|=CH_FLAGS_AUTO_CLOSE;
        else
          build_header.flags&=~CH_FLAGS_AUTO_CLOSE;
        SCRIPT_MSG(_T("AutoCloseWindow: %") NPRIs _T("\n"),k?_T("true"):_T("false"));
      }
    return PS_OK;
    case TOK_WINDOWICON:
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
      disable_window_icon=line.gettoken_enum(1,_T("on\0off\0"));
      if (disable_window_icon == -1) PRINTHELP();
      SCRIPT_MSG(_T("WindowIcon: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
#else
    ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_VISIBLE_SUPPORT not defined.\n"),line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_VISIBLE_SUPPORT
    case TOK_SHOWDETAILSUNINST:
#ifndef NSIS_CONFIG_UNINSTALL_SUPPORT
      ERROR_MSG(_T("Error: ShowUninstDetails specified but NSIS_CONFIG_UNINSTALL_SUPPORT not defined\n"));
      return PS_ERROR;
#endif
    case TOK_SHOWDETAILS:
      {
        int k=line.gettoken_enum(1,_T("hide\0show\0nevershow\0"));
        if (k == -1) PRINTHELP()
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
        if (which_token == TOK_SHOWDETAILSUNINST)
        {
          build_uninst.flags&=~(CH_FLAGS_DETAILS_NEVERSHOW|CH_FLAGS_DETAILS_SHOWDETAILS);
          if (k==1)
            build_uninst.flags|=CH_FLAGS_DETAILS_SHOWDETAILS;
          else if (k==2)
            build_uninst.flags|=CH_FLAGS_DETAILS_NEVERSHOW;
        }
        else
#endif
        {
          build_header.flags&=~(CH_FLAGS_DETAILS_NEVERSHOW|CH_FLAGS_DETAILS_SHOWDETAILS);
          if (k==1)
            build_header.flags|=CH_FLAGS_DETAILS_SHOWDETAILS;
          else if (k==2)
            build_header.flags|=CH_FLAGS_DETAILS_NEVERSHOW;
        }
        SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"),line.gettoken_str(0),line.gettoken_str(1));
      }
    return PS_OK;
    case TOK_DIRSHOW:
      /*{
        int k=line.gettoken_enum(1,_T("show\0hide\0"));
        if (k < 0) PRINTHELP();
        if (k)
          build_header.flags|=CH_FLAGS_DIR_NO_SHOW;
        else
          build_header.flags&=~CH_FLAGS_DIR_NO_SHOW;
        SCRIPT_MSG(_T("DirShow: %") NPRIs _T("\n"),k?_T("hide"):_T("show"));
      }*/
      ERROR_MSG(_T("Error: DirShow doesn't currently work\n"));
    return PS_ERROR;
    case TOK_ROOTDIRINST:
      {
        int k=line.gettoken_enum(1,_T("true\0false\0"));
        if (k == -1) PRINTHELP();
        if (k)
          build_header.flags|=CH_FLAGS_NO_ROOT_DIR;
        else
          build_header.flags&=~CH_FLAGS_NO_ROOT_DIR;
        SCRIPT_MSG(_T("AllowRootDirInstall: %") NPRIs _T("\n"),k?_T("false"):_T("true"));
      }
    return PS_OK;
    case TOK_BGFONT:
#ifndef NSIS_SUPPORT_BGBG
      ERROR_MSG(_T("Error: BGFont specified but NSIS_SUPPORT_BGBG not defined\n"));
      return PS_ERROR;
#else // NSIS_SUPPORT_BGBG
      if (line.getnumtokens()==1)
      {
        memcpy(&bg_font,&bg_default_font,sizeof(LOGFONT));
        SCRIPT_MSG(_T("BGFont: default font\n"));
        return PS_OK;
      }
      LOGFONT newfont;
      newfont.lfHeight=40;
      newfont.lfWidth=0;
      newfont.lfEscapement=0, newfont.lfOrientation=0;
      newfont.lfWeight=FW_NORMAL;
      newfont.lfItalic=newfont.lfUnderline=newfont.lfStrikeOut=FALSE;
      newfont.lfCharSet=DEFAULT_CHARSET;
      newfont.lfOutPrecision=OUT_DEFAULT_PRECIS;
      newfont.lfClipPrecision=CLIP_DEFAULT_PRECIS;
      newfont.lfQuality=DEFAULT_QUALITY;
      newfont.lfPitchAndFamily=DEFAULT_PITCH;
      my_strncpy(newfont.lfFaceName,line.gettoken_str(1),LF_FACESIZE);

      SCRIPT_MSG(_T("BGFont: \"%") NPRIs _T("\""),line.gettoken_str(1));
      {
        bool height=false, weight=false;
        for (int i = 2; i < line.getnumtokens(); i++) {
          TCHAR *tok=line.gettoken_str(i);
          if (tok[0]==_T('/')) {
            if (!_tcsicmp(tok,_T("/ITALIC"))) {
              SCRIPT_MSG(_T(" /ITALIC"));
              newfont.lfItalic=TRUE;
            }
            else if (!_tcsicmp(tok,_T("/UNDERLINE"))) {
              SCRIPT_MSG(_T(" /UNDERLINE"));
              newfont.lfUnderline=TRUE;
            }
            else if (!_tcsicmp(tok,_T("/STRIKE"))) {
              SCRIPT_MSG(_T(" /STRIKE"));
              newfont.lfStrikeOut=TRUE;
            }
            else {
              SCRIPT_MSG(_T("\n"));
              PRINTHELP();
            }
          }
          else {
            if (!height) {
              SCRIPT_MSG(_T(" height=%") NPRIs,tok);
              newfont.lfHeight=line.gettoken_int(i);
              height=true;
            }
            else if (!weight) {
              SCRIPT_MSG(_T(" weight=%") NPRIs,tok);
              newfont.lfWeight=line.gettoken_int(i);
              weight=true;
            }
            else {
              SCRIPT_MSG(_T("\n"));
              PRINTHELP();
            }
          }
        }
      }
      SCRIPT_MSG(_T("\n"));
      memcpy(&bg_font, &newfont, sizeof(LOGFONT));
    return PS_OK;
#endif //~ NSIS_SUPPORT_BGBG
    case TOK_BGGRADIENT:
#ifndef NSIS_SUPPORT_BGBG
      ERROR_MSG(_T("Error: BGGradient specified but NSIS_SUPPORT_BGBG not defined\n"));
      return PS_ERROR;
#else // NSIS_SUPPORT_BGBG
      if (line.getnumtokens()==1)
      {
        SCRIPT_MSG(_T("BGGradient: default colors\n"));
        build_header.bg_color1=0;
        build_header.bg_color2=RGB(0,0,255);
      }
      else if (!_tcsicmp(line.gettoken_str(1),_T("off")))
      {
        build_header.bg_color1=build_header.bg_color2=build_header.bg_textcolor=-1;
        SCRIPT_MSG(_T("BGGradient: off\n"));
        if (line.getnumtokens()>2) PRINTHELP()
      }
      else
      {
        TCHAR *p = line.gettoken_str(1);
        int v1,v2,v3=-1;
        v1=_tcstoul(p,&p,16);
        build_header.bg_color1=((v1&0xff)<<16)|(v1&0xff00)|((v1&0xff0000)>>16);
        p=line.gettoken_str(2);
        v2=_tcstoul(p,&p,16);
        build_header.bg_color2=((v2&0xff)<<16)|(v2&0xff00)|((v2&0xff0000)>>16);

        p=line.gettoken_str(3);
        if (*p)
        {
          if (!_tcsicmp(p,_T("notext"))) build_header.bg_textcolor=-1;
          else
          {
            v3=_tcstoul(p,&p,16);
            build_header.bg_textcolor=((v3&0xff)<<16)|(v3&0xff00)|((v3&0xff0000)>>16);
          }
        }

        SCRIPT_MSG(_T("BGGradient: 0x%06X->0x%06X (text=0x%06X)\n"),v1,v2,v3);
      }
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
      build_uninst.bg_color1=build_header.bg_color1;
      build_uninst.bg_color2=build_header.bg_color2;
      build_uninst.bg_textcolor=build_header.bg_textcolor;
#endif //~ NSIS_CONFIG_UNINSTALL_SUPPORT
#endif //~ NSIS_SUPPORT_BGBG
    return PS_OK;
#ifdef NSIS_CONFIG_VISIBLE_SUPPORT
    case TOK_INSTCOLORS:
    {
      TCHAR *p = line.gettoken_str(1);
      if (p[0]==_T('/'))
      {
        if (_tcsicmp(p,_T("/windows")) || line.getnumtokens()!=2) PRINTHELP()
        build_header.lb_fg=build_header.lb_bg=-1;
        SCRIPT_MSG(_T("InstallColors: windows default colors\n"));
      }
      else
      {
        if (line.getnumtokens()!=3) PRINTHELP()
        int v1=_tcstoul(p,&p,16);
        build_header.lb_fg=((v1&0xff)<<16)|(v1&0xff00)|((v1&0xff0000)>>16);
        p=line.gettoken_str(2);
        int v2=_tcstoul(p,&p,16);
        build_header.lb_bg=((v2&0xff)<<16)|(v2&0xff00)|((v2&0xff0000)>>16);
        SCRIPT_MSG(_T("InstallColors: fg=%06X bg=%06X\n"),v1,v2);
      }
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
      build_uninst.lb_fg=build_header.lb_fg;
      build_uninst.lb_bg=build_header.lb_bg;
#endif
    }
    return PS_OK;
    case TOK_XPSTYLE:
      {
        int k=line.gettoken_enum(1,_T("on\0off\0"));
        if (k == -1) PRINTHELP()
        SCRIPT_MSG(_T("XPStyle: %") NPRIs _T("\n"), line.gettoken_str(1));
        manifest_comctl = !k ? manifest::comctl_xp : manifest::comctl_old;
      }
    return PS_OK;
    case TOK_CHANGEUI:
      try {
        DWORD dwSize;
        int k=line.gettoken_enum(1, _T("all\0IDD_LICENSE\0IDD_DIR\0IDD_SELCOM\0IDD_INST\0IDD_INSTFILES\0IDD_UNINST\0IDD_VERIFY\0IDD_LICENSE_FSRB\0IDD_LICENSE_FSCB\0"));
        if (k<0) PRINTHELP();

        unsigned long uifilesize;
        BYTE *uifiledata = alloc_and_read_file(line.gettoken_str(2), uifilesize);
        if (!uifiledata) {
          ERROR_MSG(_T("Error: Can't read \"%") NPRIs _T("\"!\n"), line.gettoken_str(2));
          return PS_ERROR;
        }
        MANAGE_WITH(uifiledata, free);

        CResourceEditor *uire = new CResourceEditor(uifiledata, uifilesize);
        init_res_editor();

        // Search for required items
        #define CUISEARCHERR(n,v) ERROR_MSG(_T("Error: Can't find %") NPRIs _T(" (%u) in the custom UI!\n"), n, v);
        #define GET(x) if (!(dlg = uire->GetResource(RT_DIALOG, x, uire->ANYLANGID))) { CUISEARCHERR(_T(#x), x); return PS_ERROR; } CDialogTemplate UIDlg(dlg, build_unicode, uDefCodePage);
        #define SEARCH(x) if (!UIDlg.GetItem(x)) { CUISEARCHERR(_T(#x), x); uire->FreeResource(dlg); delete uire; return PS_ERROR; }
        #define SAVE(x) uire->FreeResource(dlg); dlg = UIDlg.Save(dwSize); res_editor->UpdateResource(RT_DIALOG, x, NSIS_DEFAULT_LANG, dlg, dwSize); UIDlg.FreeSavedTemplate(dlg);

        LPBYTE dlg = NULL;
        if (k == 0 || k == 1) {
          GET(IDD_LICENSE);
          SEARCH(IDC_EDIT1);
          SAVE(IDD_LICENSE);
        }

        if (k == 0 || k == 2) {
          GET(IDD_DIR);
          SEARCH(IDC_DIR);
          SEARCH(IDC_BROWSE);
#ifdef NSIS_CONFIG_LOG
          SEARCH(IDC_CHECK1);
#endif
          SAVE(IDD_DIR);
        }

        if (k == 0 || k == 3) {
          GET(IDD_SELCOM);
          SEARCH(IDC_TREE1);
          SEARCH(IDC_COMBO1);
          SAVE(IDD_SELCOM);
        }

        if (k == 0 || k == 4) {
          GET(IDD_INST);
          SEARCH(IDC_BACK);
          SEARCH(IDC_CHILDRECT);
          SEARCH(IDC_VERSTR);
          SEARCH(IDOK);
          SEARCH(IDCANCEL);

          // Search for bitmap holder (default for SetBrandingImage)
          branding_image_found = false;
          DialogItemTemplate* dlgItem = 0;
          for (int i = 0; (dlgItem = UIDlg.GetItemByIdx(i)); i++) {
            bool check = false;
            if (IS_INTRESOURCE(dlgItem->szClass)) {
              if (dlgItem->szClass == MAKEINTRESOURCEWINW(0x0082)) {
                check = true;
              }
            } else
              check = WinWStrICmpASCII(dlgItem->szClass, "Static") == 0;

            if (check) {
              if ((dlgItem->dwStyle & SS_BITMAP) == SS_BITMAP) {
                branding_image_found = true;
                branding_image_id = dlgItem->wId;
                break;
              }
            }
          }
          SAVE(IDD_INST);
        }

        if (k == 0 || k == 5) {
          GET(IDD_INSTFILES);
          SEARCH(IDC_LIST1);
          SEARCH(IDC_PROGRESS);
          SEARCH(IDC_SHOWDETAILS);
          SAVE(IDD_INSTFILES);
        }

        if (k == 0 || k == 6) {
          GET(IDD_UNINST);
          SEARCH(IDC_EDIT1);
          SAVE(IDD_UNINST);
        }

        if (k == 0 || k == 7) {
          GET(IDD_VERIFY);
          SEARCH(IDC_STR);
          SAVE(IDD_VERIFY);
        }

        if (k == 0 || k == 8) {
          GET(IDD_LICENSE_FSRB);
          SEARCH(IDC_EDIT1);
          SEARCH(IDC_LICENSEAGREE);
          SEARCH(IDC_LICENSEDISAGREE);
          SAVE(IDD_LICENSE_FSRB);
        }

        if (k == 0 || k == 9) {
          GET(IDD_LICENSE_FSCB);
          SEARCH(IDC_EDIT1);
          SEARCH(IDC_LICENSEAGREE);
          SAVE(IDD_LICENSE_FSCB);
        }

        delete uire;

        SCRIPT_MSG(_T("ChangeUI: %") NPRIs _T(" %") NPRIs _T("%") NPRIs _T("\n"), line.gettoken_str(1), line.gettoken_str(2), branding_image_found?_T(" (branding image holder found)"):_T(""));
      }
      catch (exception& err) {
        ERROR_MSG(_T("Error while changing UI: %") NPRIs _T("\n"), CtoTStrParam(err.what()));
        return PS_ERROR;
      }
    return PS_OK;
    case TOK_ADDBRANDINGIMAGE:
      try {
        LAYOUTUNIT whtype, padtype;
        int k = line.gettoken_enum(1,_T("top\0left\0bottom\0right\0")), defpadding = 2;
        int wh = ParseLayoutUnit(line.gettoken_str(2), whtype);
        if (k == -1) PRINTHELP();
        int padding = (line.getnumtokens() >= 4) ? ParseLayoutUnit(line.gettoken_str(3), padtype) : (padtype = whtype, defpadding);
        if (whtype == LU_INVALID || whtype != padtype)
          throw runtime_error("Invalid number!");
        init_res_editor();
        BYTE* dlg = res_editor->GetResource(RT_DIALOG, IDD_INST, NSIS_DEFAULT_LANG);
        CDialogTemplate dt(dlg, build_unicode, uDefCodePage);
        res_editor->FreeResource(dlg);
        if (whtype != LU_DIALOG && !CDialogTemplate::SupportsDialogUnitComputation())
          throw runtime_error("Must use dialog units on non-Win32 platforms!");

        DialogItemTemplate brandingCtl = {0,};
        brandingCtl.dwStyle = SS_BITMAP | WS_CHILD | WS_VISIBLE;
        brandingCtl.sX = brandingCtl.sY = padding;
        brandingCtl.szClass = MAKEINTRESOURCEWINW(0x0082);
        brandingCtl.szTitle = NULL;
        brandingCtl.wId = IDC_BRANDIMAGE;
        brandingCtl.sHeight = brandingCtl.sWidth = wh;
        if (whtype == LU_PIXEL) dt.PixelsToDlgUnits(brandingCtl.sWidth, brandingCtl.sHeight);
        if (k%2) { // left (1) / right (3)
          if (k & 2) // right
            brandingCtl.sX += dt.GetWidth();
          else // left
            dt.MoveAll(brandingCtl.sWidth + (padding * 2), 0);

          dt.Resize(brandingCtl.sWidth + (padding * 2), 0);
          brandingCtl.sHeight = dt.GetHeight() - (padding * 2);
        }
        else { // top (0) / bottom (2)
          if (k & 2) // bottom
            brandingCtl.sY += dt.GetHeight();
          else // top
            dt.MoveAll(0, brandingCtl.sHeight + (padding * 2));

          dt.Resize(0, brandingCtl.sHeight + (padding * 2));
          brandingCtl.sWidth = dt.GetWidth() - (padding * 2);
        }
        dt.AddItem(brandingCtl);

        DWORD dwDlgSize;
        dlg = dt.Save(dwDlgSize);
        res_editor->UpdateResource(RT_DIALOG, IDD_INST, NSIS_DEFAULT_LANG, dlg, dwDlgSize);
        dt.FreeSavedTemplate(dlg);

        if (whtype == LU_PIXEL) dt.DlgUnitsToPixels(brandingCtl.sWidth, brandingCtl.sHeight);
        const char* unitstr = whtype == LU_PIXEL ? "pixels" : "dialog units";
        SCRIPT_MSG(_T("AddBrandingImage: %") NPRIs _T(" %ux%u %") NPRIns _T("\n"), line.gettoken_str(1), brandingCtl.sWidth, brandingCtl.sHeight, unitstr);

        branding_image_found = true;
        branding_image_id = IDC_BRANDIMAGE;
      }
      catch (exception& err) {
        ERROR_MSG(_T("Error while adding image branding support: %") NPRIs _T("\n"), CtoTStrParam(err.what()));
        return PS_ERROR;
      }
    return PS_OK;
    case TOK_SETFONT:
    {
      unsigned char failed = 0;
      if (!_tcsnicmp(line.gettoken_str(1), _T("/LANG="), 6))
      {
        LANGID lang_id = ParseLangId(line.gettoken_str(1) + 6);
        LanguageTable *table = GetLangTable(lang_id);
        const TCHAR*facename = line.gettoken_str(2);
        table->nlf.m_szFont = _tcsdup(facename);
        table->nlf.m_iFontSize = line.gettoken_int(3);
        
        if (table->nlf.m_szFont)
          SCRIPT_MSG(_T("SetFont: lang=%d \"%") NPRIs _T("\" %") NPRIs _T("\n"), lang_id, facename, line.gettoken_str(3));
        else
          ++failed;
      }
      else
      {
        const TCHAR*facename = line.gettoken_str(1);
        my_strncpy(build_font, facename, COUNTOF(build_font));
        build_font_size = line.gettoken_int(2);

        if (!failed) SCRIPT_MSG(_T("SetFont: \"%") NPRIs _T("\" %") NPRIs _T("\n"), facename, line.gettoken_str(2));
      }
      if (failed)
      {
        ERROR_MSG(_T("Error: Unable to convert font name\n"));
        return PS_ERROR;
      }
    }
    return PS_OK;
#else
    case TOK_INSTCOLORS:
    case TOK_XPSTYLE:
    case TOK_CHANGEUI:
    case TOK_ADDBRANDINGIMAGE:
    case TOK_SETFONT:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_VISIBLE_SUPPORT not defined.\n"),line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_VISIBLE_SUPPORT

    case TOK_PEADDRESOURCE:
    {
      init_res_editor();
      int tokidx = 1, ovr = 0, rep = 0, result = PS_ERROR;
      if (!_tcsicmp(line.gettoken_str(tokidx), _T("/OVERWRITE"))) // Update the resource even if it exists
        ++ovr, ++tokidx;
      else if (!_tcsicmp(line.gettoken_str(tokidx), _T("/REPLACE"))) // Only update existing resource
        ++rep, ++tokidx;
      const TCHAR *rt = line.gettoken_str(tokidx+1), *rnraw = line.gettoken_str(tokidx+2), *rlraw = line.gettoken_str(tokidx+3);
      if (!*rlraw)
        rlraw = _T("Default"); // Language parameter is optional
      LANGID rl = res_editor->ParseResourceTypeNameLangString(&rt, &rnraw, rlraw), foundrl;
      if (rl == CResourceEditor::INVALIDLANGID || rl == CResourceEditor::ALLLANGID)
        PRINTHELP();
      WORD rn = (WORD)(size_t) rnraw; assert(!CResourceEditor::EditorSupportsStringNames());
      bool exists = res_editor->ResourceExists(rt, rn, rl, &foundrl);
      if (rl == CResourceEditor::ANYLANGID)
        rl = exists ? foundrl : NSIS_DEFAULT_LANG;
      if ((rep && !exists) || (!ovr && exists))
      {
        ERROR_MSG(_T("Error: Resource %") NPRIns _T("\n"), rep ? ("does not exist") : ("already exists"));
        return PS_ERROR;
      }
      if (res_editor->UpdateResourceFromExternal(rt, rn, rl, line.gettoken_str(tokidx+0), CResourceEditor::TM_AUTO))
      {
        SCRIPT_MSG(_T("PEAddResource: %") NPRIs _T("=%") NPRIs _T("\n"), make_friendly_resource_path(rt, rnraw, rl).c_str(), line.gettoken_str(tokidx+0));
        result = PS_OK;
      }
      return result;
    }
    return PS_OK;
    case TOK_PEREMOVERESOURCE:
    {
      init_res_editor();
      int tokidx = 1, noerr = 0;
      if (!_tcsicmp(line.gettoken_str(tokidx), _T("/NOERRORS")))
        ++noerr, ++tokidx;
      const TCHAR *rt = line.gettoken_str(tokidx+0), *rnraw = line.gettoken_str(tokidx+1), *rlraw = line.gettoken_str(tokidx+2);
      LANGID rl = res_editor->ParseResourceTypeNameLangString(&rt, &rnraw, rlraw);
      if (rl == CResourceEditor::INVALIDLANGID || rl == CResourceEditor::ANYLANGID)
        PRINTHELP();
      WORD rn = (WORD)(size_t) rnraw; assert(!CResourceEditor::EditorSupportsStringNames());
      if (!noerr && !res_editor->ResourceExists(rt, rn, rl))
      {
        ERROR_MSG(_T("Error: Resource %") NPRIns _T("\n"), ("does not exist"));
        return PS_ERROR;
      }
      if (res_editor->DeleteResource(rt, rn, rl, CResourceEditor::TM_AUTO))
        SCRIPT_MSG(_T("PERemoveResource: %") NPRIs _T("\n"), make_friendly_resource_path(rt, rnraw, rl).c_str());
      else if (!noerr)
        return PS_ERROR;
    }
    return PS_OK;

    case TOK_PEDLLCHARACTERISTICS:
      {
        int s1, s2;
        WORD add = (WORD)line.gettoken_int(1, &s1), del = (WORD)line.gettoken_int(2, &s2), org = PEDllCharacteristics;
        if (!s1 || !s2) PRINTHELP();
        PEDllCharacteristics &= ~del, PEDllCharacteristics |= add;
        SCRIPT_MSG(_T("PEDllCharacteristics: 0x%.4x -> 0x%.4x\n"), org, PEDllCharacteristics);
      }
      return PS_OK;

    case TOK_PESUBSYSVER:
      {
        unsigned int mj, mi;
        if (2 == _stscanf(line.gettoken_str(1), _T("%u.%u"), &mj, &mi))
          if (mj <= 0xffff && mi <= 0xffff)
            return (PESubsysVerMaj = (WORD) mj, PESubsysVerMin = (WORD) mi, PS_OK);
      }
      PRINTHELP();
      return PS_ERROR;

    case TOK_REQEXECLEVEL:
      switch (line.gettoken_enum(1,_T("none\0user\0highest\0admin\0")))
      {
      case 0: manifest_exec_level = manifest::exec_level_none; break;
      case 1: manifest_exec_level = manifest::exec_level_user; break;
      case 2: manifest_exec_level = manifest::exec_level_highest; break;
      case 3: manifest_exec_level = manifest::exec_level_admin; break;
      default: PRINTHELP();
      }
      return PS_OK;

    case TOK_MANIFEST_APPENDCUSTOMSTRING:
      if (!manifest::addappendstring(line.gettoken_str(1), line.gettoken_str(2))) PRINTHELP();
      return PS_OK;
    case TOK_MANIFEST_DPIAWARE:
      switch(line.gettoken_enum(1,_T("none\0notset\0false\0true\0system\0permonitor\0explorer\0")))
      {
      case 0: // A lot of attributes use "none" so we support that along with the documented value
      case 1: manifest_dpiaware = manifest::dpiaware_notset; break;
      case 2: manifest_dpiaware = manifest::dpiaware_false; break;
      case 3: // "True" == "System DPI"
      case 4: manifest_dpiaware = manifest::dpiaware_true; break;
      case 5: manifest_dpiaware = manifest::dpiaware_permonitor; break;
      case 6: manifest_dpiaware = manifest::dpiaware_explorer; break;
      default: PRINTHELP();
      }
      return PS_OK;
    case TOK_MANIFEST_DPIAWARENESS:
      manifest_dpiawareness = line.gettoken_str(1);
      return PS_OK;

    case TOK_MANIFEST_LPAWARE:
      switch(line.gettoken_enum(1,_T("none\0notset\0true\0false\0")))
      {
      case 0: // A lot of attributes use "none" so we support that along with the documented value
      case 1: manifest_lpaware = manifest::lpaware_notset; break;
      case 2: manifest_lpaware = manifest::lpaware_true; break;
      case 3: manifest_lpaware = manifest::lpaware_false; break;
      default: PRINTHELP();
      }
      return PS_OK;

    case TOK_MANIFEST_SUPPORTEDOS:
    {
      manifest_sosl.deleteall();
      if (2 == line.getnumtokens())
        switch(line.gettoken_enum(1,_T("none\0all\0")))
        {
        case 0: return PS_OK;
        case 1: return manifest_sosl.addall() ? PS_OK : PS_ERROR;
        }
      for(int argi = 1; argi < line.getnumtokens(); ++argi)
        if (!manifest_sosl.append(line.gettoken_str(argi)))
          PRINTHELP();
    }
    return PS_OK;
    case TOK_MANIFEST_MAXVERSIONTESTED:
      manifest_maxversiontested = line.gettoken_enum(1, _T("none\0notset\0")) == -1 ? line.gettoken_str(1) : _T("");
      return PS_OK;

    case TOK_MANIFEST_DISABLEWINDOWFILTERING:
      switch(line.gettoken_enum(1,_T("notset\0false\0true")))
      {
      case 0: 
      case 1: manifest_flags &= ~manifest::disablewindowfiltering; break;
      case 2: manifest_flags |= manifest::disablewindowfiltering; break;
      default: PRINTHELP();
      }
      return PS_OK;

    case TOK_MANIFEST_GDISCALING:
      switch(line.gettoken_enum(1,_T("notset\0false\0true")))
      {
      case 0: 
      case 1: manifest_flags &= ~manifest::gdiscaling; break;
      case 2: manifest_flags |= manifest::gdiscaling; break;
      default: PRINTHELP();
      }
      return PS_OK;

#ifdef _UNICODE
    case TOK_TARGET:
    {
      const TCHAR *cmdnam = get_commandtoken_name(which_token);
      CEXEBuild::TARGETTYPE tt = get_target_type(line.gettoken_str(1));
      if (CEXEBuild::TARGET_UNKNOWN == tt)
      {
        print_bad_targettype_parameter(cmdnam);
        return PS_ERROR;
      }
      if (m_target_type != tt && PS_OK != change_target_architecture(tt))
      {
        ERROR_MSG(_T("Error: Unable to set target %") NPRIs _T(" (adequate stub not found?)\n"), _T("architecture"));
        return PS_ERROR;
      }
      SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"), cmdnam, get_target_suffix(tt));
    }
    return PS_OK;
    case TOK_TARGETCPU:
    {
      int k = line.gettoken_enum(1, _T("x86\0x86\0amd64\0x64\0arm64\0"));
      if (-1 == k) PRINTHELP();
      CEXEBuild::TARGETTYPE tt = (CEXEBuild::TARGETTYPE)(k - (k > TARGET_AMD64)); // -1 for extra "x64" variant
      if (k < TARGET_AMD64) tt = m_previous_x86_unicode ? TARGET_X86UNICODE : TARGET_X86ANSI;
      if (m_target_type != tt && PS_OK != change_target_architecture(tt))
      {
        ERROR_MSG(_T("Error: Unable to set target %") NPRIs _T(" (adequate stub not found?)\n"), _T("architecture"));
        return PS_ERROR;
      }
      SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(1));
    }
    return PS_OK;
    case TOK_TARGETUNICODE:
    {
      int k = line.gettoken_enum(1, _T("false\0true\0"));
      if (-1 == k) PRINTHELP();
      CEXEBuild::TARGETTYPE tt = k ? (TARGET_X86ANSI == m_target_type ? TARGET_X86UNICODE : m_target_type) : TARGET_X86ANSI;
      if (tt != m_target_type && (build_compressor_set | build_lockedunicodetarget))
      {
        ERROR_MSG(_T("Error: Can't change target %") NPRIs _T(" after data already got compressed or header already changed!\n"), _T("charset"));
        return PS_ERROR;
      }
      SCRIPT_MSG(_T("Unicode: %") NPRIs _T("\n"), k ? _T("true") : _T("false"));
      if (is_targettype_64bit(tt) != is_targettype_64bit(m_target_type) || PS_OK != change_target_architecture(tt))
      {
        ERROR_MSG(_T("Error: Unable to set target %") NPRIs _T(" (adequate stub not found?)\n"), _T("charset"));
        return PS_ERROR;
      }
    }
    return PS_OK;
#endif //~ _UNICODE

    // Ability to change compression methods from within the script
    case TOK_SETCOMPRESSOR:
#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
    {
      if (build_compressor_set) {
        ERROR_MSG(_T("Error: can't change compressor after data already got compressed or header already changed!\n"));
        return PS_ERROR;
      }
      if (build_compressor_final)
      {
        warning_fl(DW_COMP_FINAL, _T("SetCompressor ignored due to previous call with the /FINAL switch"));
        return PS_OK;
      }
      build_compress_whole = false;

      int a = 1;
      while (line.gettoken_str(a)[0] == _T('/'))
      {
        if (!_tcsicmp(line.gettoken_str(a),_T("/FINAL")))
          build_compressor_final = true, a++;
        else if (!_tcsicmp(line.gettoken_str(a),_T("/SOLID")))
          build_compress_whole = true, a++;
        else
          PRINTHELP();
      }

      if (a != line.getnumtokens() - 1)
      {
        ERROR_MSG(_T("%") NPRIs _T(" expects %d parameters, got %d.\n"), line.gettoken_str(0), a + 1, line.getnumtokens());
        PRINTHELP();
      }

      int k=line.gettoken_enum(a, _T("zlib\0bzip2\0lzma\0"));
      switch (k) {
        case 0: compressor = &zlib_compressor; break;
        case 1: compressor = &bzip2_compressor; break;
        case 2: compressor = &lzma_compressor; break;
        default: PRINTHELP();
      }

      tstring compressor_name = line.gettoken_str(a);
      compressor_name = lowercase(compressor_name);

      if (set_compressor(compressor_name, build_compress_whole) != PS_OK)
      {
        SCRIPT_MSG(_T("SetCompressor: error loading stub for \"%") NPRIs _T("\" compressor.\n"), compressor_name.c_str());
        return PS_ERROR;
      }

      SCRIPT_MSG(_T("SetCompressor: %") NPRIs _T("%") NPRIs _T("%") NPRIs _T("\n"), build_compressor_final ? _T("/FINAL ") : _T(""), build_compress_whole ? _T("/SOLID ") : _T(""), line.gettoken_str(a));
    }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_COMPRESSION_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_COMPRESSION_SUPPORT
    case TOK_LOADNLF:
    {
      SCRIPT_MSG(_T("LoadLanguageFile: %") NPRIs _T("\n"), line.gettoken_str(1));

      LanguageTable *table = LoadLangFile(line.gettoken_str(1));
      if (!table) return PS_ERROR;

      if (!defcodepage_set)
      {
        uDefCodePage = table->nlf.m_uCodePage;
        defcodepage_set = true;
      }

      last_used_lang = table->lang_id;
      // define LANG_LangName as "####" (lang id), for example ${LANG_ENGLISH} = 1033
      TCHAR lang_id[16], lang_cp[16], lang_def[1024];
      wsprintf(lang_def, _T("LANG_%") NPRIs, table->nlf.m_szName);
      wsprintf(lang_id, _T("%u"), table->lang_id);
      definedlist.add(lang_def, lang_id);
      wsprintf(lang_def, _T("LANG_%") NPRIs _T("_CP"), table->nlf.m_szName);
      wsprintf(lang_cp, _T("%u"), table->nlf.m_uCodePage);
      definedlist.add(lang_def, lang_cp);
    }
    return PS_OK;

    // preprocessor-ish (ifdef/ifndef/else/endif are handled one step out from here)
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_P_DEFINE:
    return pp_define(line);
    case TOK_P_UNDEF:
    return pp_undef(line);
    case TOK_P_PACKEXEHEADER:
    return pp_packhdr(line);
    case TOK_P_FINALIZE:
    case TOK_P_UNINSTFINALIZE:
    return pp_finalize(which_token, line);
    case TOK_P_SYSTEMEXEC:
    case TOK_P_EXECUTE:
    case TOK_P_MAKENSIS:
    return pp_execute(which_token, line);
    case TOK_P_ADDINCLUDEDIR:
    return pp_addincludedir(line);
    case TOK_P_INCLUDE:
    return pp_include(line);
    case TOK_P_CD:
    return pp_cd(line);
    case TOK_P_PRAGMA:
    return parse_pragma(line) == PS_ERROR ? PS_ERROR : PS_OK;
    case TOK_P_ERROR:
      ERROR_MSG(_T("!error: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_ERROR;
    case TOK_P_WARNING:
      warning_fl(DIAGCODE_INTERNAL_HIDEDIAGCODE, _T("!warning: %") NPRIs,line.gettoken_str(1));
    return PS_OK;
    case TOK_P_ECHO:
      SCRIPT_MSG(_T("%") NPRIs _T(" (%") NPRIs _T(":%d)\n"), line.gettoken_str(1),curfilename,linecnt);
    return PS_OK;
    case TOK_P_ASSERT:
    return pp_assert(line);
    case TOK_P_SEARCHPARSESTRING:
    return pp_searchparsestring(line);
    case TOK_P_SEARCHREPLACESTRING:
    return pp_searchreplacestring(line);
    case TOK_P_VERBOSE:
    return pp_verbose(line);

    case TOK_UNINSTALLEXENAME: PRINTHELP()

#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
    case TOK_UNINSTCAPTION:
      {
        if (SetInnerString(NLF_UCAPTION,line.gettoken_str(1)) == PS_WARNING)
          warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
        SCRIPT_MSG(_T("UninstCaption: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      }
    return PS_OK;
    case TOK_UNINSTICON:
      SCRIPT_MSG(_T("UninstallIcon: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      try {
        free_loaded_icon(uninstaller_icon);
        uninstaller_icon = load_icon(line.gettoken_str(1));
      }
      catch (exception& err) {
        ERROR_MSG(_T("Error while loading icon from \"%") NPRIs _T("\": %") NPRIs _T("\n"), line.gettoken_str(1), CtoTStrParam(err.what()));
        return PS_ERROR;
      }
    return PS_OK;
    case TOK_UNINSTTEXT:
      {
        if (!cur_page) {
          if (SetInnerString(NLF_UNINST_TEXT, line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
          SetInnerString(NLF_UNINST_SUBTEXT, line.gettoken_str(2));
        }
        else {
          if (cur_page_type != PAGE_UNINSTCONFIRM) {
            ERROR_MSG(_T("Error: UninstallText can only be used inside PageEx uninstConfirm.\n"));
            return PS_ERROR;
          }
          cur_page->parms[0] = add_string(line.gettoken_str(1));
          cur_page->parms[1] = add_string(line.gettoken_str(2));
        }
        SCRIPT_MSG(_T("UninstallText: \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2));
      }
    return PS_OK;
    case TOK_UNINSTSUBCAPTION:
      {
        int succ, w=line.gettoken_int(1,&succ);
        if (!succ || w < 0 || w > 2) PRINTHELP()
        SetInnerString(NLF_USUBCAPTION_CONFIRM+w,line.gettoken_str(2));
        SCRIPT_MSG(_T("UninstSubCaption: page:%d, text=%") NPRIs _T("\n"),w,line.gettoken_str(2));
      }
    return PS_OK;
    case TOK_WRITEUNINSTALLER:
    {
      if (uninstall_mode)
      {
        ERROR_MSG(_T("WriteUninstaller only valid from install, not from uninstall.\n"));
        PRINTHELP()
      }
      uninstaller_writes_used++;
      ent.which=EW_WRITEUNINSTALLER;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      tstring full = tstring(_T("$INSTDIR\\")) + tstring(line.gettoken_str(1));
      ent.offsets[3]=add_string(full.c_str());
      // ent.offsets[1] and ent.offsets[2] are set in CEXEBuild::uninstall_generate()
      if (!ent.offsets[0]) PRINTHELP()
      SCRIPT_MSG(_T("WriteUninstaller: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));

      DefineInnerLangString(NLF_ERR_CREATING);
      DefineInnerLangString(NLF_CREATED_UNINST);
    }
    return add_entry(&ent);
    case TOK_REQUNINSTEXECLEVEL:
      if (uninstall_mode)
      {
        switch (line.gettoken_enum(1,_T("none\0user\0highest\0admin\0")))
        {
        case 0: manifest_exec_level = manifest::exec_level_none; break;
        case 1: manifest_exec_level = manifest::exec_level_user; break;
        case 2: manifest_exec_level = manifest::exec_level_highest; break;
        case 3: manifest_exec_level = manifest::exec_level_admin; break;
        default: PRINTHELP();
        }
      }
    return PS_OK;
#else //! NSIS_CONFIG_UNINSTALL_SUPPORT
    case TOK_WRITEUNINSTALLER:
    case TOK_UNINSTCAPTION:
    case TOK_UNINSTICON:
    case TOK_UNINSTTEXT:
    case TOK_UNINSTSUBCAPTION:
    case TOK_REQUNINSTEXECLEVEL:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_UNINSTALL_SUPPORT not defined.\n"), line.gettoken_str(0));
    return PS_ERROR;
#endif

    // section/function stuff
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_SECTION:
    {
      int a = 1, unselected = 0;
      if (!_tcsicmp(line.gettoken_str(1),_T("/o")))
        unselected++, a++;
      else if (line.getnumtokens() > 3)
        PRINTHELP();
      SCRIPT_MSG(_T("Section: \"%") NPRIs _T("\""),line.gettoken_str(a));
      if (line.gettoken_str(a+1)[0]) SCRIPT_MSG(_T(" ->(%") NPRIs _T(")"),line.gettoken_str(a+1));
      SCRIPT_MSG(_T("\n"));
#ifndef NSIS_CONFIG_UNINSTALL_SUPPORT
      if (!_tcsicmp(line.gettoken_str(a),_T("uninstall")))
      {
        ERROR_MSG(_T("Error: Uninstall section declared, no NSIS_CONFIG_UNINSTALL_SUPPORT\n"));
        return PS_ERROR;
      }
#endif
      int ret = add_section(line.gettoken_str(a),line.gettoken_str(a+1));
      if (PS_OK == ret && unselected)
        build_cursection->flags &= ~SF_SELECTED;
      return ret;
    }
    case TOK_SECTIONEND:
      SCRIPT_MSG(_T("SectionEnd\n"));
    return section_end();
    case TOK_SECTIONINSTTYPE: // 0 based
    case TOK_SECTIONIN: // Legacy 1 based instruction
      {
        int zerobased = which_token == TOK_SECTIONINSTTYPE, itid, succ;
        const TCHAR *cmdname = get_commandtoken_name(which_token);
        SCRIPT_MSG(_T("%") NPRIs _T(": "), cmdname);
        for (int ti = 0; ++ti < line.getnumtokens();)
        {
          const TCHAR *p = line.gettoken_str(ti);
          if (!_tcsicmp(p, _T("RO")))
          {
            if (section_add_flags(SF_RO) != PS_OK) return PS_ERROR;
            SCRIPT_MSG(_T("[RO] "));
          }
          else
          {
            itid = line.gettoken_int(ti, &succ) - (zerobased ? 0 : 1);
            if (succ && itid >= 0 && itid < NSIS_MAX_INST_TYPES)
            {
              if (section_add_install_type(1<<itid) != PS_OK) return PS_ERROR;
              SCRIPT_MSG(_T("[%d] "), itid);
            }
            else
            {
              ERROR_MSG(_T("Error: %") NPRIs _T(" %") NPRIs _T(" out of range %d..%d\n"), cmdname, p, 1 - zerobased, NSIS_MAX_INST_TYPES - zerobased);
              return PS_ERROR;
            }
          }
        }
        SCRIPT_MSG(_T("\n"));
      }
    return PS_OK;
    case TOK_SECTIONGROUPEND:
    case TOK_SUBSECTIONEND:
    case TOK_SECTIONGROUP:
    case TOK_SUBSECTION:
    {
      TCHAR buf[1024];
      int a = 1, ex = 0;
      if (!_tcsicmp(line.gettoken_str(1),_T("/e"))) ex = 1, a++;

      wsprintf(buf,_T("\x1F%") NPRIs,line.gettoken_str(a));
      if (which_token == TOK_SECTIONGROUP || which_token == TOK_SUBSECTION)
      {
        TCHAR *s = line.gettoken_str(a);
        if (!s[0] || (!_tcsicmp(s, _T("un.")) && !s[3]))
          PRINTHELP();
      }

      SCRIPT_MSG(_T("%") NPRIs _T(" %") NPRIs,line.gettoken_str(0),line.gettoken_str(a));
      if (line.gettoken_str(a+1)[0]) SCRIPT_MSG(_T(" ->(%") NPRIs _T(")"),line.gettoken_str(a+1));
      SCRIPT_MSG(_T("\n"));
      return add_section(buf,line.gettoken_str(a+1),ex);
    }
    case TOK_FUNCTION:
      if (!line.gettoken_str(1)[0]) PRINTHELP()
      if (line.gettoken_str(1)[0]==_T(':') || line.gettoken_str(1)[0]==_T('/'))
      {
        ERROR_MSG(_T("Function: function name cannot begin with : or /.\n"));
        PRINTHELP()
      }
      SCRIPT_MSG(_T("Function: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
#ifndef NSIS_CONFIG_UNINSTALL_SUPPORT
      if (!_tcsnicmp(line.gettoken_str(1),_T("un."),3))
      {
        ERROR_MSG(_T("Error: Uninstall function declared, no NSIS_CONFIG_UNINSTALL_SUPPORT\n"));
        return PS_ERROR;
      }
#endif
      return add_function(line.gettoken_str(1));
    case TOK_FUNCTIONEND:
      SCRIPT_MSG(_T("FunctionEnd\n"));
    return function_end();

    // flag setters
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_ALLOWSKIPFILES:
      build_allowskipfiles=line.gettoken_enum(1,_T("off\0on\0"));
      if (build_allowskipfiles==-1) PRINTHELP()
      SCRIPT_MSG(_T("AllowSkipFiles: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
    case TOK_SETDATESAVE:
      build_datesave=line.gettoken_enum(1,_T("off\0on\0"));
      if (build_datesave==-1) PRINTHELP()
      SCRIPT_MSG(_T("SetDateSave: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
    case TOK_SETOVERWRITE:
    {
      int k=line.gettoken_enum(1,_T("on\0off\0try\0ifnewer\0ifdiff\0lastused\0"));
      if (k==-1) PRINTHELP()
      if (k==5)
        k=build_overwrite, build_overwrite=build_last_overwrite, build_last_overwrite=k;
      else
        build_last_overwrite=build_overwrite, build_overwrite=k;
      SCRIPT_MSG(_T("SetOverwrite: %") NPRIs _T("\n"),line.gettoken_str(1));
    }
    return PS_OK;
#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
    case TOK_SETPLUGINUNLOAD:
      build_plugin_unload=line.gettoken_enum(1,_T("manual\0alwaysoff\0"));
      if (build_plugin_unload==-1) PRINTHELP()
      SCRIPT_MSG(_T("SetPluginUnload: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
#endif //~ NSIS_CONFIG_PLUGIN_SUPPORT
    case TOK_SETCOMPRESS:
      build_compress=line.gettoken_enum(1,_T("off\0auto\0force\0"));
      if (build_compress==-1) PRINTHELP()
      if (build_compress==0 && build_compress_whole)
        warning_fl(DW_COMP_WHOLE_IGNORE_OFF, _T("'SetCompress off' encountered, and in whole compression mode. Effectively ignored."));
      SCRIPT_MSG(_T("SetCompress: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
    case TOK_DBOPTIMIZE:
      build_optimize_datablock=line.gettoken_enum(1,_T("off\0on\0"));
      if (build_optimize_datablock==-1) PRINTHELP()
      SCRIPT_MSG(_T("SetDatablockOptimize: %") NPRIs _T("\n"),line.gettoken_str(1));
    return PS_OK;
    case TOK_FILEBUFSIZE:
      build_filebuflen=line.gettoken_int(1);
      build_filebuflen<<=20;
      if (build_filebuflen<=0)
      {
        ERROR_MSG(_T("Error: FileBufSize: invalid buffer size -- %d\n"),build_filebuflen);
        return PS_ERROR;
      }
      SCRIPT_MSG(_T("FileBufSize: %") NPRIs _T("mb (%d bytes)\n"),line.gettoken_str(1),build_filebuflen);
    return PS_OK;
#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
    case TOK_SETCOMPRESSIONLEVEL:
    {
      if (compressor == &lzma_compressor)
        warning_fl(DW_COMP_LEVEL_IGNORE, _T("SetCompressionLevel: compressor is set to LZMA. Effectively ignored."));
      if (build_compressor_set && build_compress_whole)
        warning_fl(DW_COMP_LEVEL_IGNORE, _T("SetCompressionLevel: data already compressed in compress whole mode. Effectively ignored."));

      int s;
      build_compress_level=line.gettoken_int(1,&s);
      if (!s || build_compress_level < 0 || build_compress_level > 9) PRINTHELP();
      SCRIPT_MSG(_T("SetCompressionLevel: %u\n"), build_compress_level);
    }
    return PS_OK;
    case TOK_SETCOMPRESSORDICTSIZE:
    {
      if (compressor != &lzma_compressor)
        warning_fl(DW_COMP_DICT_IGNORE, _T("SetCompressorDictSize: compressor is not set to LZMA. Effectively ignored."));
      if (build_compressor_set && build_compress_whole)
        warning_fl(DW_COMP_DICT_IGNORE, _T("SetCompressorDictSize: data already compressed in compress whole mode. Effectively ignored."));

      int s;
      build_compress_dict_size=line.gettoken_int(1,&s);
      if (!s) PRINTHELP();
      SCRIPT_MSG(_T("SetCompressorDictSize: %u mb\n"), build_compress_dict_size);
      build_compress_dict_size <<= 20;
    }
    return PS_OK;
#else
    case TOK_SETCOMPRESSIONLEVEL:
    case TOK_SETCOMPRESSORDICTSIZE:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_COMPRESSION_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_COMPRESSION_SUPPORT
    case TOK_ADDSIZE:
      {
        int succ, size_kb=line.gettoken_int(1,&succ);
        if (!succ) PRINTHELP()
        SCRIPT_MSG(_T("AddSize: %d kb\n"),size_kb);
        section_add_size_kb(size_kb);
      }
    return PS_OK;
    case TOK_SUBCAPTION:
      {
        int succ, w=line.gettoken_int(1,&succ);
        if (!succ || w < 0 || w > 4) PRINTHELP()
        SetInnerString(NLF_SUBCAPTION_LICENSE+w,line.gettoken_str(2));
        SCRIPT_MSG(_T("SubCaption: page:%d, text=%") NPRIs _T("\n"),w,line.gettoken_str(2));
      }
    return PS_OK;
    case TOK_FILEERRORTEXT:
#ifdef NSIS_SUPPORT_FILE
      {
        SetInnerString(NLF_FILE_ERROR,line.gettoken_str(1));
        SetInnerString(NLF_FILE_ERROR_NOIGNORE,line.gettoken_str(2));
        SCRIPT_MSG(_T("FileErrorText: \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2));
      }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_FILE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif
    case TOK_BRANDINGTEXT:
      {
        int a = 1, trim = 0;
        while (line.gettoken_str(a)[0] == _T('/')) {
          if (!_tcsnicmp(line.gettoken_str(a),_T("/TRIM"),5)) {
            if (!_tcsicmp(line.gettoken_str(a)+5,_T("LEFT"))) trim = 1;
            else if (!_tcsicmp(line.gettoken_str(a)+5,_T("RIGHT"))) trim = 2;
            else if (!_tcsicmp(line.gettoken_str(a)+5,_T("CENTER"))) trim = 3;
            else PRINTHELP();
            a++;
          }
          else break;
        }
        if (line.getnumtokens()!=a+1 && !trim) PRINTHELP();
        if (line.getnumtokens()==a+1)
          SetInnerString(NLF_BRANDING,line.gettoken_str(a));
#ifdef _WIN32
        if (trim) try {
          init_res_editor();

          BYTE* dlg = res_editor->GetResource(RT_DIALOG, IDD_INST, NSIS_DEFAULT_LANG);
          CDialogTemplate td(dlg,build_unicode,uDefCodePage);
          res_editor->FreeResource(dlg);

          if (trim) {
            TCHAR str[512];
            if (line.getnumtokens()==a+1 && line.gettoken_str(a)[0])
              _tcscpy(str, line.gettoken_str(a));
            else
              wsprintf(str, _T("Nullsoft Install System %") NPRIs, NSIS_VERSION);

            short old_width = td.GetItem(IDC_VERSTR)->sWidth;
            switch (trim) {
              case 1: td.LTrimToString(IDC_VERSTR, str, 4); break;
              case 2: td.RTrimToString(IDC_VERSTR, str, 4); break;
              case 3: td.CTrimToString(IDC_VERSTR, str, 4); break;
            }
            if (td.GetItem(IDC_VERSTR)->sWidth > old_width)
              warning_fl(DW_ATTRIBUTE_OVERLONGSTRING, _T("BrandingText: \"%") NPRIs _T("\" is too long, trimming has expanded the label"), str);
          }
          DWORD dwSize;
          dlg = td.Save(dwSize);
          res_editor->UpdateResource(RT_DIALOG, IDD_INST, NSIS_DEFAULT_LANG, dlg, dwSize);
          td.FreeSavedTemplate(dlg);
        }
        catch (exception& err) {
          ERROR_MSG(_T("Error while trimming branding text control: %") NPRIs _T("\n"), CtoTStrParam(err.what()));
          return PS_ERROR;
        }
#else
        if (trim)
        {
          ERROR_MSG(_T("Error: BrandingText /TRIM* is disabled for non Win32 platforms.\n"));
          return PS_ERROR;
        }
#endif
        SCRIPT_MSG(_T("BrandingText: \"%") NPRIs _T("\"\n"),line.gettoken_str(a));
      }
    return PS_OK;
    case TOK_MISCBUTTONTEXT:
      {
        SetInnerString(NLF_BTN_BACK,line.gettoken_str(1));
        SetInnerString(NLF_BTN_NEXT,line.gettoken_str(2));
        SetInnerString(NLF_BTN_CANCEL,line.gettoken_str(3));
        SetInnerString(NLF_BTN_CLOSE,line.gettoken_str(4));
        SCRIPT_MSG(_T("MiscButtonText: back=\"%") NPRIs _T("\" next=\"%") NPRIs _T("\" cancel=\"%") NPRIs _T("\" close=\"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
      }
    return PS_OK;
    case TOK_SPACETEXTS:
      {
        if (!_tcsicmp(line.gettoken_str(1), _T("none"))) {
          no_space_texts=true;
          SCRIPT_MSG(_T("SpaceTexts: none\n"));
        }
        else {
          no_space_texts=false;
          SetInnerString(NLF_SPACE_REQ,line.gettoken_str(1));
          SetInnerString(NLF_SPACE_AVAIL,line.gettoken_str(2));
          SCRIPT_MSG(_T("SpaceTexts: required=\"%") NPRIs _T("\" available=\"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2));
        }
      }
    return PS_OK;
    case TOK_INSTBUTTONTEXT:
      {
        SetInnerString(NLF_BTN_INSTALL,line.gettoken_str(1));
        SCRIPT_MSG(_T("InstallButtonText: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      }
    return PS_OK;
    case TOK_DETAILSBUTTONTEXT:
      {
        if (!cur_page) {
          if (SetInnerString(NLF_BTN_DETAILS,line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
        }
        else {
          if (cur_page_type != PAGE_INSTFILES) {
            ERROR_MSG(_T("Error: DetailsButtonText can only be used inside PageEx instfiles.\n"));
            return PS_ERROR;
          }
          cur_page->parms[1] = add_string(line.gettoken_str(1));
        }
        SCRIPT_MSG(_T("DetailsButtonText: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      }
    return PS_OK;
    case TOK_COMPLETEDTEXT:
      {
        if (!cur_page) {
          if (SetInnerString(NLF_COMPLETED,line.gettoken_str(1)) == PS_WARNING)
            warning_fl(DW_INNERSTRING_MULTISETWASTE, _T("%") NPRIs _T(": specified multiple times, wasting space"),line.gettoken_str(0));
        }
        else {
          if (cur_page_type != PAGE_INSTFILES) {
            ERROR_MSG(_T("Error: CompletedText can only be used inside PageEx instfiles.\n"));
            return PS_ERROR;
          }
          cur_page->parms[2] = add_string(line.gettoken_str(1));
        }
        SCRIPT_MSG(_T("CompletedText: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      }
    return PS_OK;
    case TOK_UNINSTBUTTONTEXT:
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
      {
        SetInnerString(NLF_BTN_UNINSTALL,line.gettoken_str(1));
        SCRIPT_MSG(_T("UninstButtonText: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
      }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_UNINSTALL_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif

    // instructions
    ///////////////////////////////////////////////////////////////////////////////
    case TOK_NOP:
      SCRIPT_MSG(_T("Nop\n"));
      ent.which=EW_NOP;
    return add_entry(&ent);
    case TOK_GOTO:
      ent.which=EW_NOP;
      if (process_jump(line,1,&ent.offsets[0])) PRINTHELP()
      SCRIPT_MSG(_T("Goto: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_GETREGVIEW:
      return add_flag_instruction_entry(which_token, EW_GETFLAG, line, FLAG_OFFSET(alter_reg_view));
    case TOK_SETREGVIEW:
    {
      ent.which=EW_SETFLAG;
      ent.offsets[0]=FLAG_OFFSET(alter_reg_view);
      int k=line.gettoken_enum(1,_T("32\0") _T("64\0default\0lastused\0"));
      if (k == 0) ent.offsets[1]=add_intstring(is_target_64bit() ? KEY_WOW64_32KEY : 0); // 32
      else if (k == 1) ent.offsets[1]=add_intstring(is_target_64bit() ? 0 : KEY_WOW64_64KEY); // 64
      else if (k == 2) ent.offsets[1]=add_intstring(0); // default
      else if (k == 3) ent.offsets[2]=1; // last used
      else PRINTHELP()
      SCRIPT_MSG(_T("SetRegView: %") NPRIs _T("\n"),line.gettoken_str(1));
    }
    return add_entry(&ent);
    case TOK_IFALTREGVIEW:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(alter_reg_view), ~0); //new value mask - keep flag
    case TOK_GETSHELLVARCONTEXT:
      return add_flag_instruction_entry(which_token, EW_GETFLAG, line, FLAG_OFFSET(all_user_var));
    case TOK_SETSHELLVARCONTEXT: 
      ent.offsets[1] = line.gettoken_enum(1,_T("current\0all\0lastused\0"));
      if (ent.offsets[1] < 0 ) PRINTHELP()
      return add_flag_instruction_entry(which_token, EW_SETFLAG, line, FLAG_OFFSET(all_user_var), ent.offsets[1] != 2 ? add_intstring(ent.offsets[1]) : AFIE_LASTUSED);
    case TOK_IFSHELLVARCONTEXTALL:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(all_user_var), ~0); //new value mask - keep flag
    case TOK_RET:
      SCRIPT_MSG(_T("Return\n"));
      ent.which=EW_RET;
    return add_entry(&ent);
    case TOK_CALL:
      if (!line.gettoken_str(1)[0] || (line.gettoken_str(1)[0]==_T(':') && !line.gettoken_str(1)[1] )) PRINTHELP()
#ifdef NSIS_CONFIG_UNINSTALL_SUPPORT
      if (uninstall_mode && _tcsnicmp(line.gettoken_str(1),_T("un."),3)
          && (GetUserVarIndex(line,1) < 0) && line.gettoken_str(1)[0]!=_T(':'))
      {
        ERROR_MSG(_T("Call must be used with function names starting with \"un.\" in the uninstall section.\n"));
        PRINTHELP()
      }
      if (!uninstall_mode && !_tcsnicmp(line.gettoken_str(1),_T("un."),3))
      {
        ERROR_MSG(_T("Call must not be used with functions starting with \"un.\" in the non-uninstall sections.\n"));
        PRINTHELP()
      }
#endif
      ent.which=EW_CALL;
      ent.offsets[1]=0;
      {
        int v;
        if ((v=GetUserVarIndex(line, 1))>=0)
        {
          ent.offsets[0]=-v-2;
        }
        else
        {
          if (line.gettoken_str(1)[0] == _T(':'))
          {
            ent.offsets[1]=1;
            ent.offsets[0]=ns_label.add(line.gettoken_str(1)+1,0);
          }
          else ent.offsets[0]=ns_func.add(line.gettoken_str(1),0);
        }
      }
      SCRIPT_MSG(_T("Call \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_SETOUTPATH:
      {
        const TCHAR *op=line.gettoken_str(1), *cmdname=_T("SetOutPath");
        if (!_tcscmp(op,_T("-"))) op=_T("$INSTDIR");
        SCRIPT_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\"\n"),cmdname,op);
        ent.which=EW_CREATEDIR;
        ent.offsets[0]=add_string(op);
        ent.offsets[1]=1;
        DefineInnerLangString(NLF_OUTPUT_DIR);
      }
    return add_entry(&ent);
    case TOK_CREATEDIR:
      {
        const TCHAR *cmdname=_T("CreateDirectory");
        TCHAR out_path[NSIS_MAX_STRLEN], *p=line.gettoken_str(1);
        bool badpath=IsWindowsPathRelative(p) && _T('$') != *p; // ExeHead will have to deal with expanded $variables...
        if (badpath)
          ERROR_MSG(_T("%") NPRIs _T(": Relative paths not supported\n"),cmdname);
        else
        {
          my_strncpy(out_path,p,COUNTOF(out_path));
          p=CharPrev(out_path,out_path+_tcslen(out_path));
          if (IsAgnosticPathSeparator(*p)) *p=0; // remove trailing slash
        }
        if (badpath || !*out_path) PRINTHELP()
        SCRIPT_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\"\n"),cmdname,out_path);
        ent.which=EW_CREATEDIR;
        ent.offsets[0]=add_string(out_path);
        DefineInnerLangString(NLF_CREATE_DIR);
      }
    return add_entry(&ent);
    case TOK_EXEC:
    case TOK_EXECWAIT:
#ifdef NSIS_SUPPORT_EXECUTE
      ent.which=EW_EXECUTE;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[2]=0;
      if (which_token == TOK_EXECWAIT)
      {
        ent.offsets[2]=1;
        ent.offsets[1]=GetUserVarIndex(line, 2);
        if (line.gettoken_str(2)[0] && ent.offsets[1]<0) PRINTHELP()
      }
      SCRIPT_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\" (->%") NPRIs _T(")\n"),ent.offsets[2]?_T("ExecWait"):_T("Exec"),line.gettoken_str(1),line.gettoken_str(2));

      DefineInnerLangString(NLF_EXEC);
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_EXECUTE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_EXECUTE
    case TOK_EXECSHELL:
    case TOK_EXECSHELLWAIT:
#ifdef NSIS_SUPPORT_SHELLEXECUTE
    {
      UINT to=0, xflags=0;
      static const TCHAR*fn=_T("/INVOKEIDLIST\0/CONNECTNETDRV\0/DOENVSUBST\0/NOIDLIST\0/NOCONSOLE\0/NOZONECHECKS\0/WAITFORINPUTIDLE\0/LOGUSAGE\0/ASYNCOK\0/ALLOWERRORUI\0");
      static const UINT fv[]={  0x0000000C,    0x00000080,     0x00000200,  0x00001000,0x00008000, 0x00800000,    0x02000000,        0x04000000,0x100000, 0x0100     };
      for (int k;;) if ((k = line.gettoken_enum(to+1,fn)) < 0) { if (line.gettoken_str(to+1)[0]=='/') PRINTHELP(); break; } else xflags|=fv[k], to++;
      const TCHAR *verb=line.gettoken_str(to+1), *file=line.gettoken_str(to+2), *params=line.gettoken_str(to+3), *cnam=get_commandtoken_name(which_token);
      ent.which=EW_SHELLEXEC;
      ent.offsets[0]=add_string(verb), ent.offsets[1]=add_string(file);
      ent.offsets[2]=add_string(params), ent.offsets[3]=SW_SHOWNORMAL;
      ent.offsets[4]=SEE_MASK_FLAG_DDEWAIT|((xflags&0x0100)?0:SEE_MASK_FLAG_NO_UI)|xflags|(which_token==TOK_EXECSHELLWAIT ? SEE_MASK_NOCLOSEPROCESS : 0);
      if (line.getnumtokens()-to > 4)
      {
        int tab[8]={SW_SHOWDEFAULT,SW_SHOWNORMAL,SW_SHOWMAXIMIZED,SW_SHOWMINIMIZED,SW_HIDE,SW_SHOW,SW_SHOWNA,SW_SHOWMINNOACTIVE};
        int a=line.gettoken_enum(to+4,_T("SW_SHOWDEFAULT\0SW_SHOWNORMAL\0SW_SHOWMAXIMIZED\0SW_SHOWMINIMIZED\0SW_HIDE\0SW_SHOW\0SW_SHOWNA\0SW_SHOWMINNOACTIVE\0"));
        if (a >= 0) ent.offsets[3]=tab[a]; else PRINTHELP()
      }
      tstring detail=tstring(verb) + (*verb ? _T(" ") : _T("")) + tstring(file);
      ent.offsets[5]=add_string(detail.c_str());
      SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T(": \"%") NPRIs _T("\" \"%") NPRIs _T("\" %") NPRIs _T("\n"),cnam,verb,file,params,line.gettoken_str(to+4));
      DefineInnerLangString(NLF_EXEC_SHELL);
    }
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_SHELLEXECUTE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_SHELLEXECUTE
    case TOK_CALLINSTDLL:
    case TOK_REGDLL:
    case TOK_UNREGDLL:
#ifndef NSIS_SUPPORT_ACTIVEXREG
      ERROR_MSG(_T("%") NPRIs _T(": support not compiled in (NSIS_SUPPORT_ACTIVEXREG)\n"),line.gettoken_str(0));
      return PS_ERROR;
#else // NSIS_SUPPORT_ACTIVEXREG
      ent.which=EW_REGISTERDLL;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      if (which_token == TOK_UNREGDLL)
      {
        ent.offsets[1]=add_asciistring(_T("DllUnregisterServer"));
        ent.offsets[2]=DefineInnerLangString(NLF_UNREGISTERING);
      }
      else if (which_token == TOK_CALLINSTDLL)
      {
        int a = 2;
        if (!_tcsicmp(line.gettoken_str(a), _T("/NOUNLOAD"))) {
          ent.offsets[3]=1;
          a++;
        }
        if (a+1 != line.getnumtokens()) PRINTHELP();
        ent.offsets[1]=add_string(line.gettoken_str(a));
        if (!ent.offsets[1]) PRINTHELP()
        ent.offsets[2]=0;
      }
      else // Register DLL
      {
        ent.offsets[1] = add_string(line.gettoken_str(2));
        if (!ent.offsets[1]) ent.offsets[1]=add_asciistring(_T("DllRegisterServer"));
        ent.offsets[2]=DefineInnerLangString(NLF_REGISTERING);
      }

      SCRIPT_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\" %") NPRIs _T("\n"),line.gettoken_str(0),line.gettoken_str(1), line.gettoken_str(ent.offsets[3]?3:2));
      DefineInnerLangString(NLF_SYMBOL_NOT_FOUND);
      DefineInnerLangString(NLF_COULD_NOT_LOAD);
      DefineInnerLangString(NLF_NO_OLE);
      // not used anywhere - DefineInnerLangString(NLF_ERR_REG_DLL);
    return add_entry(&ent);
#endif //~ NSIS_SUPPORT_ACTIVEXREG
    case TOK_RENAME:
#ifdef NSIS_SUPPORT_RENAME
      {
        int a=1;
        ent.which=EW_RENAME;
        if (!_tcsicmp(line.gettoken_str(1),_T("/REBOOTOK")))
        {
          ent.offsets[2]=1;
          a++;
#ifndef NSIS_SUPPORT_MOVEONREBOOT
          ERROR_MSG(_T("Error: /REBOOTOK specified, NSIS_SUPPORT_MOVEONREBOOT not defined\n"));
          PRINTHELP()
#endif
        }
        else if (line.gettoken_str(1)[0]==_T('/'))
        {
          a=line.getnumtokens(); // cause usage to go here:
        }
        if (line.getnumtokens()!=a+2) PRINTHELP()
        ent.offsets[0]=add_string(line.gettoken_str(a));
        ent.offsets[1]=add_string(line.gettoken_str(a+1));
        tstring print = tstring(line.gettoken_str(a)) + _T("->") + tstring(line.gettoken_str(a+1));
        ent.offsets[3]=add_string(print.c_str());
        SCRIPT_MSG(_T("Rename: %") NPRIs _T("%") NPRIs _T("->%") NPRIs _T("\n"),ent.offsets[2]?_T("/REBOOTOK "):_T(""),line.gettoken_str(a),line.gettoken_str(a+1));

        DefineInnerLangString(NLF_RENAME);
#ifdef NSIS_SUPPORT_MOVEONREBOOT
        DefineInnerLangString(NLF_RENAME_ON_REBOOT);
#endif
      }
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_RENAME not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_RENAME
    case TOK_MESSAGEBOX:
#ifdef NSIS_SUPPORT_MESSAGEBOX
      {
        #define MBD(x) {x,_T(#x)},
        struct { int id; const TCHAR *str; } list[] = {
          MBD(MB_ABORTRETRYIGNORE)
          MBD(MB_OK)
          MBD(MB_OKCANCEL)
          MBD(MB_RETRYCANCEL)
          MBD(MB_YESNO)
          MBD(MB_YESNOCANCEL)
          MBD(MB_ICONEXCLAMATION)
          MBD(MB_ICONINFORMATION)
          MBD(MB_ICONQUESTION)
          MBD(MB_ICONSTOP)
          MBD(MB_USERICON)
          MBD(MB_TOPMOST)
          MBD(MB_SETFOREGROUND)
          MBD(MB_RIGHT)
          MBD(MB_RTLREADING)
          MBD(MB_DEFBUTTON1)
          MBD(MB_DEFBUTTON2)
          MBD(MB_DEFBUTTON3)
          MBD(MB_DEFBUTTON4)
        };
        #undef MBD
        int r=0, x;
        TCHAR *p=line.gettoken_str(1);
        while (*p)
        {
          TCHAR *np=p;
          while (*np && *np != _T('|')) np++;
          if (*np) *np++=0;
          for (x = 0 ; (size_t) x < COUNTOF(list) && _tcsicmp(list[x].str, p); x++);
          if ((size_t) x < COUNTOF(list))
            r |= list[x].id;
          else
            PRINTHELP()
          p=np;
        }
        ent.which=EW_MESSAGEBOX;
        ent.offsets[0]=r;
        ent.offsets[1]=add_string(line.gettoken_str(2));
        static const int rettab[] = { 0,IDABORT,IDCANCEL,IDIGNORE,IDNO,IDOK,IDRETRY,IDYES };
        const TCHAR *retstr=_T("0\0IDABORT\0IDCANCEL\0IDIGNORE\0IDNO\0IDOK\0IDRETRY\0IDYES\0");
        int a=3;
        if (line.getnumtokens() > 3)
        {
          if (!_tcsicmp(line.gettoken_str(3),_T("/SD")))
          {
            int k=line.gettoken_enum(4,retstr);
            if (k <= 0) PRINTHELP();
            ent.offsets[0] |= rettab[k]<<21;
            a=5;
          }
          else if (line.getnumtokens() > 7)
            PRINTHELP();

          if (line.getnumtokens() > a)
          {
            ent.offsets[2]=line.gettoken_enum(a,retstr);
            if (ent.offsets[2] < 0)
              PRINTHELP();
            ent.offsets[2] = rettab[ent.offsets[2]];
            if (process_jump(line,a+1,&ent.offsets[3]))
              PRINTHELP();
            if (line.getnumtokens() > a+2)
            {
              int v=line.gettoken_enum(a+2,retstr);
              if (v < 0)
                PRINTHELP();
              ent.offsets[4] = rettab[v];
              if (process_jump(line,a+3,&ent.offsets[5]))
                PRINTHELP();
            }
          }
        }
        SCRIPT_MSG(_T("MessageBox: %d: \"%") NPRIs _T("\""),r,line.gettoken_str(2));
        if (line.getnumtokens()>a+1) SCRIPT_MSG(_T(" (on %") NPRIs _T(" goto %") NPRIs _T(")"),line.gettoken_str(a),line.gettoken_str(a+1));
        SCRIPT_MSG(_T("\n"));
      }
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_MESSAGEBOX not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_MESSAGEBOX
    case TOK_CREATESHORTCUT:
#ifdef NSIS_SUPPORT_CREATESHORTCUT
    {
      const TCHAR *cmdnam = line.gettoken_str(0);
      ent.which=EW_CREATESHORTCUT;
      int noLnkWorkDir=0, s;
      if (!_tcsicmp(line.gettoken_str(1),_T("/NoWorkingDir"))) line.eattoken(), noLnkWorkDir++;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=add_string(line.gettoken_str(3));
      ent.offsets[3]=add_string(line.gettoken_str(4));
      ent.offsets[5]=add_string(line.gettoken_str(8));
      ent.offsets[4]=(line.gettoken_int(5,&s) << CS_II_SHIFT) & CS_II_MASK;
      if (!s || ent.offsets[4] < 0 || ent.offsets[4] > CS_II_MAX)
      {
        if (line.getnumtokens() > 5 && *line.gettoken_str(5))
        {
          ERROR_MSG(_T("CreateShortcut: cannot interpret icon index\n"));
          PRINTHELPEX(cmdnam)
        }
      }
      if (noLnkWorkDir) ent.offsets[4] |= CS_NWD;
      if (line.getnumtokens() > 6 && *line.gettoken_str(6))
      {
        const int tab[3]={SW_SHOWNORMAL,SW_SHOWMAXIMIZED,SW_SHOWMINNOACTIVE/*SW_SHOWMINIMIZED doesn't work*/};
        int a=line.gettoken_enum(6,_T("SW_SHOWNORMAL\0SW_SHOWMAXIMIZED\0SW_SHOWMINIMIZED\0"));
        if (a < 0 || (tab[a] << CS_SC_SHIFT) & ~CS_SC_MASK)
        {
          ERROR_MSG(_T("CreateShortcut: unknown show mode \"%") NPRIs _T("\"\n"),line.gettoken_str(6));
          PRINTHELPEX(cmdnam)
        }
        ent.offsets[4] |= tab[a] << CS_SC_SHIFT;
      }
      if (line.getnumtokens() > 7)
      {
        TCHAR *s=line.gettoken_str(7), b[255];
        for (unsigned int spos=0; (spos <= _tcslen(s)) && (spos <= 255); spos++)
          b[spos]=_totupper(*(s+spos));
        _tcscpy(s,b);
        if (*s)
        {
          int c=0;
          if (_tcsstr(s,_T("ALT|"))) ent.offsets[4] |= HOTKEYF_ALT << (CS_HK_SHIFT+8);
          if (_tcsstr(s,_T("CONTROL|"))) ent.offsets[4] |= HOTKEYF_CONTROL << (CS_HK_SHIFT+8);
          if (_tcsstr(s,_T("EXT|"))) ent.offsets[4] |= HOTKEYF_EXT << (CS_HK_SHIFT+8);
          if (_tcsstr(s,_T("SHIFT|"))) ent.offsets[4] |= HOTKEYF_SHIFT << (CS_HK_SHIFT+8);
          while (_tcsstr(s,_T("|")))
            s=_tcsstr(s,_T("|"))+1;
          if ((s[0] == _T('F')) && (s[1] >= _T('1') && s[1] <= _T('9')))
          {
            c=VK_F1-1+_ttoi(s+1);
            if (_ttoi(s+1) < 1 || _ttoi(s+1) > 24)
              warning_fl(DW_PARSE_LNK_HK, _T("CreateShortcut: F-key \"%") NPRIs _T("\" out of range"),s);
          }
          else if (((s[0] >= _T('A') && s[0] <= _T('Z')) || (s[0] >= _T('0') && s[0] <= _T('9'))) && !s[1])
            c=s[0];
          else
          {
            c=s[0];
            warning_fl(DW_PARSE_LNK_HK, _T("CreateShortcut: unrecognized hotkey \"%") NPRIs _T("\""),s);
          }
          ent.offsets[4] |= ((c) << CS_HK_SHIFT) & CS_HK_MASK;
        }
      }
      SCRIPT_MSG(_T("CreateShortcut: \"%") NPRIs _T("\"->\"%") NPRIs _T("\" %") NPRIs _T(" icon:%") NPRIs _T(",%d, nwd=%d, showmode=0x%X, hotkey=0x%X, comment=%") NPRIs _T("\n"),
        line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4),(ent.offsets[4]>>CS_II_SHIFT)&(CS_II_MASK>>CS_II_SHIFT),!!(ent.offsets[4]&CS_NWD),
        (ent.offsets[4]>>CS_SC_SHIFT)&(CS_SC_MASK>>CS_SC_SHIFT),(ent.offsets[4]>>CS_HK_SHIFT)&(CS_HK_MASK>>CS_HK_SHIFT),line.gettoken_str(8));

      DefineInnerLangString(NLF_CREATE_SHORTCUT);
      DefineInnerLangString(NLF_ERR_CREATING_SHORTCUT);
    }
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_CREATESHORTCUT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_CREATESHORTCUT
#ifdef NSIS_SUPPORT_HWNDS
    case TOK_FINDWINDOW:
      ent.which=EW_FINDWINDOW;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0) PRINTHELP()
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=add_string(line.gettoken_str(3));
      ent.offsets[3]=add_string(line.gettoken_str(4));
      ent.offsets[4]=add_string(line.gettoken_str(5));
      SCRIPT_MSG(_T("FindWindow: output=%") NPRIs _T(", class=\"%") NPRIs _T("\", text=\"%") NPRIs _T("\" hwndparent=\"%") NPRIs _T("\" hwndafter=\"%") NPRIs _T("\"\n"),
        line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4),line.gettoken_str(5));
    return add_entry(&ent);
    case TOK_SENDMESSAGE:
    {
      ent.which=EW_SENDMESSAGE;
      if (line.gettoken_str(1)[0] == _T('/') || line.gettoken_str(2)[0] == _T('/') ||
          line.gettoken_str(3)[0] == _T('/') || line.gettoken_str(4)[0] == _T('/'))
        PRINTHELP()

      SCRIPT_MSG(_T("SendMessage:"));
      int a=5;
      ent.offsets[0]=GetUserVarIndex(line,a);
      if (ent.offsets[0]>=0)
        SCRIPT_MSG(_T("(->%") NPRIs _T(")"),line.gettoken_str(a++));

      if (!_tcsncicmp(line.gettoken_str(a),_T("/TIMEOUT="),9))
      {
        ent.offsets[5]|=_ttoi(line.gettoken_str(a++)+9)<<2;
        SCRIPT_MSG(_T(" (timeout=%d)"),ent.offsets[5]>>2);
      }

      if (line.getnumtokens()>a)
        PRINTHELP()

      if (!_tcsncmp(line.gettoken_str(3),_T("STR:"),4))
      {
        ent.offsets[5]|=1;
        ent.offsets[3]=add_string(line.gettoken_str(3)+4);
      }
      else
        ent.offsets[3]=add_string(line.gettoken_str(3));
      if (!_tcsncmp(line.gettoken_str(4),_T("STR:"),4))
      {
        ent.offsets[5]|=2;
        ent.offsets[4]=add_string(line.gettoken_str(4)+4);
      }
      else
        ent.offsets[4]=add_string(line.gettoken_str(4));

      ent.offsets[1]=add_string(line.gettoken_str(1));
      ent.offsets[2]=add_string(line.gettoken_str(2));
      SCRIPT_MSG(_T("(%") NPRIs _T(",%") NPRIs _T(",%") NPRIs _T(",%") NPRIs _T(")\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
    }
    return add_entry(&ent);
    case TOK_ISWINDOW:
      ent.which=EW_ISWINDOW;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      if (process_jump(line,2,&ent.offsets[1])||
          process_jump(line,3,&ent.offsets[2])) PRINTHELP()
      SCRIPT_MSG(_T("IsWindow(%") NPRIs _T("): %") NPRIs _T(":%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
    case TOK_GETDLGITEM:
      ent.which=EW_GETDLGITEM;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0]<0) PRINTHELP();
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=add_string(line.gettoken_str(3));
      SCRIPT_MSG(_T("GetDlgItem: output=%") NPRIs _T(" dialog=%") NPRIs _T(" item=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
    case TOK_SETCTLCOLORS:
    {
      ent.which=EW_SETCTLCOLORS;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ctlcolors c={0, };
      TCHAR *p;
      int a = 2, ctok = line.getnumtokens();
      if (!_tcsicmp(line.gettoken_str(2),_T("/BRANDING"))) a+=1;
      if (!_tcsicmp(line.gettoken_str(2),_T("/RESET"))) { if (ctok != 3) return PS_ERROR; else a+=2; }
      if (a == 2 && ctok == 5) {
        ERROR_MSG(_T("Error: SetCtlColors expected 3 parameters, got 4\n"));
        return PS_ERROR;
      }
      if (!_tcsicmp(line.gettoken_str(a+1),_T("transparent"))) {
        c.flags|=CC_BKB, c.lbStyle=BS_NULL, c.bkmode=TRANSPARENT;
      }
      else { // Parse background color
        c.lbStyle=BS_SOLID, c.bkmode=OPAQUE;
        if (*(p=line.gettoken_str(a+1)))
          c.flags|=CC_BK|CC_BKB, c.bkc=ParseCtlColor(p, c.flags, CC_BK_SYS);
      }
      if (*(p=line.gettoken_str(a))) // Set text color?
        c.flags|=CC_TEXT, c.text=ParseCtlColor(p, c.flags, CC_TEXT_SYS);
      if (a == 3) { // Handle /BRANDING
        c.flags|=CC_BK|CC_BKB;
        c.lbStyle=BS_NULL;
        if (!*line.gettoken_str(a+1)) c.bkc=COLOR_BTNFACE, c.flags|=CC_BK_SYS;
        c.flags|=CC_TEXT;
        if (!*line.gettoken_str(a)) c.text=COLOR_BTNFACE, c.flags|=CC_TEXT_SYS;
        c.bkmode=OPAQUE;
      }
      if (a == 4) c.bkmode=OPAQUE, c.flags=0, c.bkb = 0; // Experimental and undocumented /RESET, a formal way of doing SetCtlColors $hCtl "" ""
      assert(sizeof(ctlcolors64) > sizeof(ctlcolors));
      int i, l=cur_ctlcolors->getlen()/sizeof(ctlcolors), pad=is_target_64bit()?sizeof(ctlcolors64)-sizeof(ctlcolors):0;
      for (i=0; i<l; i++)
        if (!memcmp((ctlcolors*)cur_ctlcolors->get()+i,&c,sizeof(ctlcolors))) {
          ent.offsets[1]=i*(sizeof(ctlcolors)+pad);
          break;
        }
      if (i>=l) ent.offsets[1]=cur_ctlcolors->add(&c,sizeof(ctlcolors))+(l*pad);
      SCRIPT_MSG(_T("SetCtlColors: hwnd=%") NPRIs _T(" %") NPRIs _T("text=%") NPRIs _T(" background=%") NPRIs _T("\n"),line.gettoken_str(1),a==2?_T(""):_T("/BRANDING "),line.gettoken_str(a),line.gettoken_str(a+1));
    }
    return add_entry(&ent);
    case TOK_CREATEFONT:
    {
      ent.which=EW_CREATEFONT;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0) PRINTHELP()
      ent.offsets[1]=add_string(line.gettoken_str(2));
      SCRIPT_MSG(_T("CreateFont: output=%") NPRIs _T(" \"%") NPRIs _T("\""),line.gettoken_str(1),line.gettoken_str(2));
      int height=0, weight=0, flags=0;
      for (int i = 3; i < line.getnumtokens(); i++) {
        TCHAR *tok=line.gettoken_str(i);
        if (tok[0]=='/') {
          if (!_tcsicmp(tok,_T("/ITALIC"))) {
            SCRIPT_MSG(_T(" /ITALIC"));
            flags|=1;
          }
          else if (!_tcsicmp(tok,_T("/UNDERLINE"))) {
            SCRIPT_MSG(_T(" /UNDERLINE"));
            flags|=2;
          }
          else if (!_tcsicmp(tok,_T("/STRIKE"))) {
            SCRIPT_MSG(_T(" /STRIKE"));
            flags|=4;
          }
          else {
            SCRIPT_MSG(_T("\n"));
            PRINTHELP();
          }
        }
        else {
          if (!height) {
            SCRIPT_MSG(_T(" height=%") NPRIs,tok);
            height=add_string(tok);
          }
          else if (!weight) {
            SCRIPT_MSG(_T(" weight=%") NPRIs,tok);
            weight=add_string(tok);
          }
          else {
            SCRIPT_MSG(_T("\n"));
            PRINTHELP();
          }
        }
        ent.offsets[2]=height;
        ent.offsets[3]=weight;
        ent.offsets[4]=flags;
      }
      SCRIPT_MSG(_T("\n"));
    }
    return add_entry(&ent);
    case TOK_ENABLEWINDOW:
      ent.which=EW_SHOWWINDOW;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[3]=1;
      SCRIPT_MSG(_T("EnableWindow: handle=%") NPRIs _T(" enable=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SHOWWINDOW:
      ent.which=EW_SHOWWINDOW;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=add_string(line.gettoken_str(2));
      SCRIPT_MSG(_T("ShowWindow: handle=%") NPRIs _T(" show state=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_HIDEWINDOW:
      ent.which=EW_SHOWWINDOW;
      ent.offsets[0]=add_asciistring(_T("$HWNDPARENT"));
      ent.offsets[1]=add_asciistring(_T("0")/*SW_HIDE*/);
      ent.offsets[2]=1;
      SCRIPT_MSG(_T("HideWindow\n"));
    return add_entry(&ent);
    case TOK_BRINGTOFRONT:
    {
      int ret;
      ent.which=EW_SHOWWINDOW;
      ent.offsets[0]=add_asciistring(_T("$HWNDPARENT"));
      ent.offsets[1]=add_asciistring(_T("5")/*SW_SHOW*/);
      if ((ret = add_entry(&ent)) != PS_OK) return ret;
      ent.which=EW_BRINGTOFRONT;
      ent.offsets[0]=0;
      ent.offsets[1]=0;
      SCRIPT_MSG(_T("BringToFront\n"));
    }
    return add_entry(&ent);
#else
    case TOK_GETDLGITEM:
    case TOK_SETCTLCOLORS:
    case TOK_SHOWWINDOW:
    case TOK_BRINGTOFRONT:
    case TOK_CREATEFONT:
    case TOK_HIDEWINDOW:
    case TOK_ENABLEWINDOW:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_ENHANCEDUI_SUPPORT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_ENHANCEDUI_SUPPORT
#else //! NSIS_SUPPORT_HWNDS
    case TOK_ISWINDOW:
    case TOK_SENDMESSAGE:
    case TOK_FINDWINDOW:
    case TOK_GETDLGITEM:
    case TOK_SETCTLCOLORS:
    case TOK_SHOWWINDOW:
    case TOK_ENABLEWINDOW:
    case TOK_CREATEFONT:
    case TOK_HIDEWINDOW:
    case TOK_BRINGTOFRONT:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_HWNDS not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_HWNDS
    case TOK_DELETE:
#ifdef NSIS_SUPPORT_DELETE
      {
        int a=1;
        ent.which=EW_DELETEFILE;
        if (!_tcsicmp(line.gettoken_str(a),_T("/REBOOTOK")))
        {
          a++;
          ent.offsets[1]=DEL_REBOOT;
#ifndef NSIS_SUPPORT_MOVEONREBOOT
          ERROR_MSG(_T("Error: /REBOOTOK specified, NSIS_SUPPORT_MOVEONREBOOT not defined\n"));
          PRINTHELP()
#endif
        }
        else if (line.gettoken_str(1)[0]==_T('/'))
        {
          a=line.getnumtokens();
        }
        if (line.getnumtokens() != a+1) PRINTHELP()
        ent.offsets[0]=add_string(line.gettoken_str(a));
        SCRIPT_MSG(_T("Delete: %") NPRIs _T("\"%") NPRIs _T("\"\n"),ent.offsets[1]?_T("/REBOOTOK "):_T(""),line.gettoken_str(a));

        DefineInnerLangString(NLF_DEL_FILE);
#ifdef NSIS_SUPPORT_MOVEONREBOOT
        DefineInnerLangString(NLF_DEL_ON_REBOOT);
#endif
      }
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_DELETE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_DELETE
    case TOK_RMDIR:
#ifdef NSIS_SUPPORT_RMDIR
      {
        int a=1;
        ent.which=EW_RMDIR;
        ent.offsets[1]=DEL_DIR;
        while (line.gettoken_str(a)[0]==_T('/'))
        {
          if (!_tcsicmp(line.gettoken_str(a),_T("/r")))
          {
            if (a == 3) PRINTHELP();
            a++;
            ent.offsets[1]|=DEL_RECURSE;
          }
          else if (!_tcsicmp(line.gettoken_str(a),_T("/REBOOTOK")))
          {
            if (a == 3) PRINTHELP();
            a++;
            ent.offsets[1]|=DEL_REBOOT;
          }
          else PRINTHELP();
        }
        if (a < line.getnumtokens() - 1) PRINTHELP();
        ent.offsets[0]=add_string(line.gettoken_str(a));
        SCRIPT_MSG(_T("RMDir: "));
        if (a>1)
          SCRIPT_MSG(_T("%") NPRIs _T(" "),line.gettoken_str(1));
        if (a>2)
          SCRIPT_MSG(_T("%") NPRIs _T(" "),line.gettoken_str(2));
        SCRIPT_MSG(_T("\"%") NPRIs _T("\"\n"),line.gettoken_str(a));

        DefineInnerLangString(NLF_REMOVE_DIR);
        DefineInnerLangString(NLF_DEL_FILE);
#ifdef NSIS_SUPPORT_MOVEONREBOOT
        DefineInnerLangString(NLF_DEL_ON_REBOOT);
#endif
      }
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_RMDIR not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_RMDIR
    case TOK_RESERVEFILE:
    case TOK_FILE:
#ifdef NSIS_SUPPORT_FILE
      {
        set<tstring> excluded;
        int a=1,attrib=0;
        bool fatal=true,rec=false,reserveplugin=false;
        if (!_tcsicmp(line.gettoken_str(a),_T("/nonfatal")))
          fatal=false, a++;

        if (which_token == TOK_RESERVEFILE && !_tcsicmp(line.gettoken_str(a),_T("/plugin")))
          reserveplugin=true, a++;

        if (which_token == TOK_FILE && !_tcsicmp(line.gettoken_str(a),_T("/a")))
        {
#ifdef _WIN32
          attrib=1;
#else
          warning_fl(DW_UNSUPP_STORE_FILE_ATT, _T("%") NPRIs _T("File /a is disabled for non Win32 platforms."),(which_token == TOK_FILE)?_T(""):_T("Reserve"));
#endif
          a++;
        }
        if (!reserveplugin && !_tcsicmp(line.gettoken_str(a),_T("/r")))
        {
          rec=true, a++;
        }
        else if (which_token == TOK_FILE && !_tcsnicmp(line.gettoken_str(a),_T("/oname="),7))
        {
          TCHAR *on=line.gettoken_str(a)+7;
          a++;
          if (!*on||line.getnumtokens()!=a+1||_tcsstr(on,_T("*")) || _tcsstr(on,_T("?"))) PRINTHELP()

          if (on[0]==_T('"'))
          {
            ERROR_MSG(_T("%") NPRIs _T("File: output name must not begin with a quote, use \"/oname=name with spaces\".\n"),(which_token == TOK_FILE)?_T(""):_T("Reserve"),line.gettoken_str(a));
            PRINTHELP();
          }

          int tf=0;
          TCHAR *fn = line.gettoken_str(a);
          PATH_CONVERT(fn);
          int v=do_add_file(fn, attrib, 0, &tf, on);
          if (v != PS_OK) return v;
          if (tf > 1) PRINTHELP()
          if (!tf)
          {
            if (fatal)
            {
              ERROR_MSG(_T("%") NPRIs _T("File: \"%") NPRIs _T("\" -> no files found.\n"),(which_token == TOK_FILE)?_T(""):_T("Reserve"),line.gettoken_str(a));
              PRINTHELP()
            }
            else
            {
              warning_fl(DW_FILE_NONFATAL_NOT_FOUND, _T("%") NPRIs _T("File: \"%") NPRIs _T("\" -> no files found"),(which_token == TOK_FILE)?_T(""):_T("Reserve"),line.gettoken_str(a));

              // workaround for bug #1299100: add a nop opcode so relative jumps will work as expected
              add_entry_direct(EW_NOP);
            }
          }
          return PS_OK;
        }
        if (!_tcsnicmp(line.gettoken_str(a),_T("/x"),2))
        {
          while (!_tcsnicmp(line.gettoken_str(a),_T("/x"),2))
          {
            a++;

            if (line.getnumtokens() < a+1) PRINTHELP()

            excluded.insert(line.gettoken_str(a));
            a++;
          }
        }
#ifdef _WIN32
        if (line.gettoken_str(a)[0] == _T('/')) PRINTHELP()
#endif
        if (line.getnumtokens()<a+1) PRINTHELP()
        while (a < line.getnumtokens())
        {
#ifdef _WIN32
          if (line.gettoken_str(a)[0]==_T('/')) PRINTHELP()
#endif
          TCHAR buf[32];
          TCHAR *t=line.gettoken_str(a++);
          if (t[0] && CharNext(t)[0] == _T(':') && CharNext(t)[1] == _T('\\') && !CharNext(t)[2])
          {
            _tcscpy(buf,_T("X:\\*.*"));
            buf[0]=t[0];
            t=buf;
          }
          tstring pluginfullpath;
          if (reserveplugin && get_file_name(t)==t)
          {
            if (!m_pPlugins || !m_pPlugins->FindDllPath(t, pluginfullpath))
            {
              pluginfullpath = definedlist.find(_T("NSISDIR"));
              pluginfullpath += tstring(PLATFORM_PATH_SEPARATOR_STR) + _T("Plugins");
              pluginfullpath += tstring(PLATFORM_PATH_SEPARATOR_STR) + get_target_suffix();
              pluginfullpath += tstring(PLATFORM_PATH_SEPARATOR_STR) + t;
            }
            t = (TCHAR*) pluginfullpath.c_str();
          }
          int tf=0;
          TCHAR *fn = my_convert(t);
          int v=do_add_file(fn, attrib, rec, &tf, NULL, which_token == TOK_FILE, NULL, excluded);
          my_convert_free(fn);
          if (v != PS_OK) return v;
          if (!tf)
          {
            if (fatal)
            {
              ERROR_MSG(_T("%") NPRIs _T("File: \"%") NPRIs _T("\" -> no files found.\n"),(which_token == TOK_FILE)?_T(""):_T("Reserve"),t);
              PRINTHELP();
            }
            else
            {
              warning_fl(DW_FILE_NONFATAL_NOT_FOUND, _T("%") NPRIs _T("File: \"%") NPRIs _T("\" -> no files found."),(which_token == TOK_FILE)?_T(""):_T("Reserve"),t);
            }
          }
        }
      }
    return PS_OK;
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_FILE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_FILE
#ifdef NSIS_SUPPORT_COPYFILES
    case TOK_COPYFILES:
      {
        ent.which=EW_COPYFILES;
        ent.offsets[2]=FOF_NOCONFIRMATION|FOF_NOCONFIRMMKDIR|FOF_NOERRORUI|FOF_SIMPLEPROGRESS;
        int a=1;
        for (int x = 0; x < 2; x++)
        {
          if (!_tcsicmp(line.gettoken_str(a),_T("/SILENT")))
          {
            a++;
            ent.offsets[2]&=~FOF_SIMPLEPROGRESS;
            ent.offsets[2]|=FOF_SILENT;
          }
          else if (!_tcsicmp(line.gettoken_str(a),_T("/FILESONLY")))
          {
            a++;
            ent.offsets[2]|=FOF_FILESONLY;
          }
          else if (line.gettoken_str(a)[0]==_T('/')) PRINTHELP()
          else break;
        }
        if (line.getnumtokens() < a+2) PRINTHELP()
        ent.offsets[0]=add_string(line.gettoken_str(a));
        ent.offsets[1]=add_string(line.gettoken_str(a+1));
        tstring copy_to = tstring(_T("$(^CopyTo)")) + line.gettoken_str(a+1);
        ent.offsets[3]=add_string(copy_to.c_str());
        int succ, size_kb=line.gettoken_int(a+2,&succ);
        if (!succ && line.gettoken_str(a+2)[0]) PRINTHELP()
        section_add_size_kb(size_kb);
        SCRIPT_MSG(_T("CopyFiles: %") NPRIs _T("\"%") NPRIs _T("\" -> \"%") NPRIs _T("\", size=%iKB\n"),ent.offsets[2]&FOF_SILENT?_T("(silent) "):_T(""), line.gettoken_str(a),line.gettoken_str(a+1),size_kb);
        DefineInnerLangString(NLF_COPY_FAILED);
        DefineInnerLangString(NLF_COPY_TO);
      }
    return add_entry(&ent);
#else
    case TOK_COPYFILES:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_COPYFILES not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_COPYFILES
    case TOK_SETFILEATTRIBUTES:
      {
        #define MBD(x) {x,_T(#x)},
        struct { int id; const TCHAR *str; } list[] = {
          MBD(FILE_ATTRIBUTE_NORMAL) {FILE_ATTRIBUTE_NORMAL,_T("NORMAL")}, // Short alias
          MBD(FILE_ATTRIBUTE_ARCHIVE) {FILE_ATTRIBUTE_ARCHIVE,_T("ARCHIVE")},
          MBD(FILE_ATTRIBUTE_HIDDEN) {FILE_ATTRIBUTE_HIDDEN,_T("HIDDEN")},
          MBD(FILE_ATTRIBUTE_OFFLINE) {FILE_ATTRIBUTE_OFFLINE,_T("OFFLINE")},
          MBD(FILE_ATTRIBUTE_READONLY) {FILE_ATTRIBUTE_READONLY,_T("READONLY")},
          MBD(FILE_ATTRIBUTE_SYSTEM) {FILE_ATTRIBUTE_SYSTEM,_T("SYSTEM")},
          MBD(FILE_ATTRIBUTE_TEMPORARY) {FILE_ATTRIBUTE_TEMPORARY,_T("TEMPORARY")},
          MBD(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) {FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,_T("NOTINDEXED")},
          {FILE_ATTRIBUTE_NORMAL,_T("0")},
        };
        #undef MBD
        int r=0, x;
        TCHAR *p=line.gettoken_str(2);
        while (*p)
        {
          TCHAR *np=p;
          while (*np && *np != _T('|')) np++;
          if (*np) *np++=0;
          for (x = 0 ; (unsigned) x < COUNTOF(list) && _tcsicmp(list[x].str,p); x++);

          if ((unsigned) x < COUNTOF(list))
            r |= list[x].id;
          else
            PRINTHELP()
          p=np;
        }
        ent.which=EW_SETFILEATTRIBUTES;
        ent.offsets[0]=add_string(line.gettoken_str(1));
        ent.offsets[1]=r;
      }
    return add_entry(&ent);
    case TOK_SLEEP:
      {
        ent.which=EW_SLEEP;
        ent.offsets[0]=add_string(line.gettoken_str(1));
        SCRIPT_MSG(_T("Sleep: %") NPRIs _T(" ms\n"),line.gettoken_str(1));
      }
    return add_entry(&ent);
    case TOK_IFFILEEXISTS:
      ent.which=EW_IFFILEEXISTS;
      ent.offsets[0] = add_string(line.gettoken_str(1));
      if (process_jump(line,2,&ent.offsets[1]) ||
          process_jump(line,3,&ent.offsets[2])) PRINTHELP()
      SCRIPT_MSG(_T("IfFileExists: \"%") NPRIs _T("\" ? %") NPRIs _T(" : %") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
    case TOK_QUIT:
      ent.which=EW_QUIT;
      SCRIPT_MSG(_T("Quit\n"));
    return add_entry(&ent);
    case TOK_ABORT:
      ent.which=EW_ABORT;
      ent.offsets[0] = add_string(line.gettoken_str(1));
      SCRIPT_MSG(_T("Abort: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_SETDETAILSVIEW:
      {
        int v=line.gettoken_enum(1,_T("hide\0show\0"));
        if (v < 0) PRINTHELP()
        ent.which=EW_CHDETAILSVIEW;
        ent.offsets[0] = v?SW_SHOWNA:SW_HIDE;
        ent.offsets[1] = v?SW_HIDE:SW_SHOWNA;
        SCRIPT_MSG(_T("SetDetailsView: %") NPRIs _T("\n"),line.gettoken_str(1));
      }
    return add_entry(&ent);
    case TOK_SETDETAILSPRINT:
    {
      ent.which=EW_SETFLAG;
      ent.offsets[0]=FLAG_OFFSET(status_update);
      int k=line.gettoken_enum(1,_T("both\0textonly\0listonly\0none\0lastused\0"));
      if (k < 0) PRINTHELP()
      if (k == 4)
        ent.offsets[2]=1; // lastused
      else
        ent.offsets[1]=add_intstring(k*2); // both=0, textonly=2, listonly=4, none=6
      SCRIPT_MSG(_T("SetDetailsPrint: %") NPRIs _T("\n"),line.gettoken_str(1));
    }
    return add_entry(&ent);
    case TOK_SETAUTOCLOSE:
    {
      ent.which=EW_SETFLAG;
      ent.offsets[0]=FLAG_OFFSET(autoclose);
      int k=line.gettoken_enum(1,_T("false\0true\0"));
      if (k < 0) PRINTHELP()
      ent.offsets[1]=add_intstring(k);
      SCRIPT_MSG(_T("SetAutoClose: %") NPRIs _T("\n"),line.gettoken_str(1));
    }
    return add_entry(&ent);
    case TOK_IFERRORS:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(exec_error), 0); //new value mask - clean error
    case TOK_IFABORT:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(abort), ~0); //new value mask - keep flag
    case TOK_CLEARERRORS:
      return add_flag_instruction_entry(which_token, EW_SETFLAG, line, FLAG_OFFSET(exec_error), add_intstring(0));
    case TOK_SETERRORS:
      return add_flag_instruction_entry(which_token, EW_SETFLAG, line, FLAG_OFFSET(exec_error), add_intstring(1));
    case TOK_SETERRORLEVEL:
      return add_flag_instruction_entry(which_token, EW_SETFLAG, line, FLAG_OFFSET(errlvl), add_string(line.gettoken_str(1)));
    case TOK_GETERRORLEVEL:
      return add_flag_instruction_entry(which_token, EW_GETFLAG, line, FLAG_OFFSET(errlvl));
#ifdef NSIS_SUPPORT_STROPTS
    case TOK_STRLEN:
      ent.which=EW_STRLEN;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      ent.offsets[1]=add_string(line.gettoken_str(2));
      if (ent.offsets[0] < 0) PRINTHELP()
      SCRIPT_MSG(_T("StrLen %") NPRIs _T(" \"%") NPRIs _T("\"\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_STRCPY:
    case TOK_UNSAFESTRCPY:
      {
        ent.which = EW_ASSIGNVAR;
        const TCHAR* msgprefix = _T("");
        int idx = -1;
        if (TOK_STRCPY == which_token)
          idx = GetUserVarIndex(line, 1);
        else
          idx = GetUnsafeUserVarIndex(line, 1), msgprefix = _T("Unsafe");
        if (idx < 0) PRINTHELP()
        ent.offsets[0]=idx; // Destination variable
        ent.offsets[1]=add_string(line.gettoken_str(2)); // Source string
        ent.offsets[2]=add_string(line.gettoken_str(3)); // Optional MaxLen
        ent.offsets[3]=add_string(line.gettoken_str(4)); // Optional StartOffset
        SCRIPT_MSG(_T("%") NPRIs _T("StrCpy %") NPRIs _T(" \"%") NPRIs _T("\" (%") NPRIs _T(") (%") NPRIs _T(")\n"),
          msgprefix,line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
        return add_entry(&ent);
      }
    case TOK_GETFUNCTIONADDR:
      ent.which=EW_GETFUNCTIONADDR;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      ent.offsets[1]=ns_func.add(line.gettoken_str(2),0);
      ent.offsets[2]=0;
      ent.offsets[3]=0;
      if (ent.offsets[0] < 0) PRINTHELP()
      SCRIPT_MSG(_T("GetFunctionAddress: %") NPRIs _T(" %") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_GETLABELADDR:
      ent.which=EW_GETLABELADDR;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0 || process_jump(line,2,&ent.offsets[1])) PRINTHELP()
      ent.offsets[2]=0;
      ent.offsets[3]=0;
      SCRIPT_MSG(_T("GetLabelAddress: %") NPRIs _T(" %") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_GETCURRENTADDR:
      ent.which=EW_ASSIGNVAR;
      if ((ent.offsets[0]=GetUserVarIndex(line, 1)) < 0) PRINTHELP()
      ent.offsets[1]=add_intstring(1+(cur_header->blocks[NB_ENTRIES].num));
      ent.offsets[2]=ent.offsets[3]=0;
      SCRIPT_MSG(_T("GetCurrentAddress: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_STRCMP:
    case TOK_STRCMPS:
      ent.which=EW_STRCMP;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[4]=which_token == TOK_STRCMPS;
      if (process_jump(line,3,&ent.offsets[2]) ||
          process_jump(line,4,&ent.offsets[3])) PRINTHELP()
      SCRIPT_MSG(_T("%") NPRIs _T(" \"%") NPRIs _T("\" \"%") NPRIs _T("\" equal=%") NPRIs _T(", nonequal=%") NPRIs _T("\n"),line.gettoken_str(0),line.gettoken_str(1),line.gettoken_str(2), line.gettoken_str(3),line.gettoken_str(4));
    return add_entry(&ent);
    case TOK_GETDLLVERSIONLOCAL:
      {
        const TCHAR*cmdname=_T("GetDLLVersionLocal");
        DWORD low, high, prod = line.gettoken_enum(1, _T("/ProductVersion\0")) == 0 ? 2 : 0;
        if (prod) line.eattoken();
        if (!GetDLLVersion(line.gettoken_str(1),high,low,!!prod))
        {
          ERROR_MSG(_T("%") NPRIs _T(": error reading version info from \"%") NPRIs _T("\"\n"),cmdname,line.gettoken_str(1));
          return PS_ERROR;
        }
        ent.which=EW_ASSIGNVAR;
        if ((ent.offsets[0]=GetUserVarIndex(line, 2)) < 0) PRINTHELP()
        ent.offsets[1]=add_intstring(high);
        ent.offsets[2]=ent.offsets[3]=0;
        if (PS_OK != add_entry(&ent)) return PS_ERROR;
        if ((ent.offsets[0]=GetUserVarIndex(line, 3)) < 0) PRINTHELP()
        ent.offsets[1]=add_intstring(low);
        ent.offsets[2]=ent.offsets[3]=0;
        SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T(" (%u,%u)->(%") NPRIs _T(",%") NPRIs _T(")\n"),
          cmdname,line.gettoken_str(1),high,low,line.gettoken_str(2),line.gettoken_str(3));
      }
    return add_entry(&ent);
    case TOK_GETFILETIMELOCAL:
      {
        TCHAR buf[129];
        DWORD high=0,low=0;
#ifdef _WIN32
        int flag=0;
        HANDLE hFile=CreateFile(line.gettoken_str(1),0,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
          FILETIME ft;
          if (GetFileTime(hFile,NULL,NULL,&ft))
            flag=1, high=ft.dwHighDateTime, low=ft.dwLowDateTime;
          CloseHandle(hFile);
        }
        if (!flag)
        {
          ERROR_MSG(_T("GetFileTimeLocal: error reading date from \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
          return PS_ERROR;
        }
#else
        struct stat st;
        if (_tstat(line.gettoken_str(1), &st))
        {
          ERROR_MSG(_T("GetFileTimeLocal: error reading date from \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
          return PS_ERROR;
        }
        unsigned long long ll = (st.st_mtime * 10000000LL) + 116444736000000000LL;
        high = (DWORD) (ll >> 32), low = (DWORD) ll;
#endif
        ent.which=EW_ASSIGNVAR;
        if ((ent.offsets[0]=GetUserVarIndex(line, 2)) < 0) PRINTHELP()
        wsprintf(buf,_T("%u"),high);
        ent.offsets[1]=add_string(buf);
        ent.offsets[2]=ent.offsets[3]=0;
        if (PS_OK != add_entry(&ent)) return PS_ERROR;
        if ((ent.offsets[0]=GetUserVarIndex(line, 3)) < 0) PRINTHELP()
        wsprintf(buf,_T("%u"),low);
        ent.offsets[1]=add_string(buf);
        ent.offsets[2]=ent.offsets[3]=0;
        SCRIPT_MSG(_T("GetFileTimeLocal: %") NPRIs _T(" (%u,%u)->(%") NPRIs _T(",%") NPRIs _T(")\n"),
          line.gettoken_str(1),high,low,line.gettoken_str(2),line.gettoken_str(3));
      }
    return add_entry(&ent);

#else
    case TOK_GETDLLVERSIONLOCAL:
    case TOK_GETFILETIMELOCAL:
    case TOK_GETFUNCTIONADDR:
    case TOK_GETLABELADDR:
    case TOK_GETCURRENTADDR:
    case TOK_STRLEN:
    case TOK_STRCPY:
    case TOK_STRCMP:
    case TOK_STRCMPS:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_STROPTS not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_STROPTS
#ifdef NSIS_SUPPORT_INIFILES
    case TOK_DELETEINISEC:
    case TOK_DELETEINISTR:
      {
        const TCHAR *vname=_T(""), *space=_T("");
        ent.which=EW_WRITEINI;
        ent.offsets[0]=add_string(line.gettoken_str(2)); // section name
        if (line.getnumtokens() > 3)
        {
          vname=line.gettoken_str(3);
          ent.offsets[1]=add_string(vname); // value name
          space=_T(" ");
        }
        else ent.offsets[1]=0;
        ent.offsets[2]=0;
        ent.offsets[3]=add_string(line.gettoken_str(1));
        SCRIPT_MSG(_T("DeleteINI%") NPRIs _T(": [%") NPRIs _T("] %") NPRIs _T("%") NPRIs _T("in %") NPRIs _T("\n"),*vname?_T("Str"):_T("Sec"),
          line.gettoken_str(2),vname,space,line.gettoken_str(1));
      }
    return add_entry(&ent);
    case TOK_FLUSHINI:
      ent.which=EW_WRITEINI;
      ent.offsets[3]=add_string(line.gettoken_str(1));
      SCRIPT_MSG(_T("FlushINI: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_WRITEINISTR:
      ent.which=EW_WRITEINI;
      ent.offsets[0]=add_string(line.gettoken_str(2));
      ent.offsets[1]=add_string(line.gettoken_str(3));
      ent.offsets[2]=add_string(line.gettoken_str(4));
      ent.offsets[3]=add_string(line.gettoken_str(1));
      ent.offsets[4]=1; // write
      SCRIPT_MSG(_T("WriteINIStr: [%") NPRIs _T("] %") NPRIs _T("=%") NPRIs _T(" in %") NPRIs _T("\n"),
        line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_READINISTR:
      ent.which=EW_READINISTR;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0) PRINTHELP()
      ent.offsets[1]=add_string(line.gettoken_str(3));
      ent.offsets[2]=add_string(line.gettoken_str(4));
      ent.offsets[3]=add_string(line.gettoken_str(2));
      SCRIPT_MSG(_T("ReadINIStr %") NPRIs _T(" [%") NPRIs _T("]:%") NPRIs _T(" from %") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(3),line.gettoken_str(4),line.gettoken_str(2));
    return add_entry(&ent);
#else
    case TOK_DELETEINISEC:
    case TOK_DELETEINISTR:
    case TOK_FLUSHINI:
    case TOK_WRITEINISTR:
    case TOK_READINISTR:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_INIFILES not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_INIFILES
    case TOK_DETAILPRINT:
      ent.which=EW_UPDATETEXT;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=0;
      SCRIPT_MSG(_T("DetailPrint: \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
    return add_entry(&ent);
#ifdef NSIS_SUPPORT_FNUTIL
    case TOK_GETTEMPFILENAME:
      ent.which=EW_GETTEMPFILENAME;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (line.getnumtokens() == 3)
        ent.offsets[1]=add_string(line.gettoken_str(2));
      else
        ent.offsets[1]=add_asciistring(_T("$TEMP"));
      if (ent.offsets[0]<0) PRINTHELP()
      SCRIPT_MSG(_T("GetTempFileName -> %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_GETFULLPATHNAME:
      {
        int a=0;
        ent.which=EW_GETFULLPATHNAME;
        if (line.getnumtokens()==4 && !_tcsicmp(line.gettoken_str(1),_T("/SHORT"))) a++;
        else if (line.getnumtokens()==4 || *line.gettoken_str(1)==_T('/')) PRINTHELP()
        ent.offsets[0]=add_string(line.gettoken_str(2+a));
        ent.offsets[1]=GetUserVarIndex(line, 1+a);
        ent.offsets[2]=!a;
        if (ent.offsets[1]<0) PRINTHELP()
        SCRIPT_MSG(_T("GetFullPathName: %") NPRIs _T("->%") NPRIs _T(" (%d)\n"),
          line.gettoken_str(2+a),line.gettoken_str(1+a),a?_T("sfn"):_T("lfn"));
      }
    return add_entry(&ent);
    case TOK_GETKNOWNFOLDERPATH:
      ent.which=EW_GETOSINFO;
      ent.offsets[1]=GetUserVarIndex(line, 1);
      ent.offsets[2]=add_string(line.gettoken_str(2));
      ent.offsets[3]=GETOSINFO_KNOWNFOLDER;
      SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("->%") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(2), line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_GETWINVER:
      {
        int k = line.gettoken_enum(2, _T("major\0minor\0build\0servicepack\0product\0ntddimajmin\0")), cb = 0, ofs = 0;
        switch(k)
        {
        case 0: cb = 1, ofs = ABI_OSINFOOFFSET + FIELD_OFFSET(osinfo, WVMaj); break;
        case 1: cb = 1, ofs = ABI_OSINFOOFFSET + FIELD_OFFSET(osinfo, WVMin); break;
        case 2: cb = 4, ofs = ABI_OSINFOOFFSET + FIELD_OFFSET(osinfo, WVBuild); break;
        case 3: cb = 1, ofs = ABI_OSINFOOFFSET + FIELD_OFFSET(osinfo, WVSP); break;
        case 4: cb = 1, ofs = ABI_OSINFOOFFSET + FIELD_OFFSET(osinfo, WVProd); break;
        case 5: cb = 2, ofs = ABI_OSINFOOFFSET + FIELD_OFFSET(osinfo, WVMin); break;
        default: PRINTHELP();
        }
        ent.which=EW_GETOSINFO;
        ent.offsets[1]=GetUserVarIndex(line, 1);
        ent.offsets[2]=add_intstring(ABI_OSINFOADDRESS);
        ent.offsets[3]=GETOSINFO_READMEMORY;
        ent.offsets[4]=add_intstring((cb << 0) | (ofs << 24));
        SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("=%") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(1), line.gettoken_str(2));
      }
      return add_entry(&ent);
    case TOK_READMEMORY:
      ent.which=EW_GETOSINFO;
      ent.offsets[1]=GetUserVarIndex(line, 1);
      ent.offsets[2]=add_string(line.gettoken_str(2));
      ent.offsets[3]=GETOSINFO_READMEMORY;
      ent.offsets[4]=add_string(line.gettoken_str(3));
      SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("=*%") NPRIs _T("\n"), get_commandtoken_name(which_token), line.gettoken_str(1), line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SEARCHPATH:
      ent.which=EW_SEARCHPATH;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0) PRINTHELP()
      ent.offsets[1]=add_string(line.gettoken_str(2));
      SCRIPT_MSG(_T("SearchPath %") NPRIs _T(" %") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
#else
    case TOK_SEARCHPATH:
    case TOK_GETTEMPFILENAME:
    case TOK_GETFULLPATHNAME:
    case TOK_GETKNOWNFOLDERPATH:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_FNUTIL not defined.\n"),  line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_SUPPORT_FNUTIL
    case TOK_GETDLLVERSION:
#ifdef NSIS_SUPPORT_GETDLLVERSION
      ent.which=EW_GETDLLVERSION;
      if ((ent.offsets[3]=line.gettoken_enum(1, _T("/ProductVersion\0")) == 0 ? 2 : 0)) line.eattoken();
      ent.offsets[0]=GetUserVarIndex(line, 2);
      ent.offsets[1]=GetUserVarIndex(line, 3);
      ent.offsets[2]=add_string(line.gettoken_str(1));
      if (ent.offsets[0]<0 || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("GetDLLVersion: %") NPRIs _T("->%") NPRIs _T(",%") NPRIs _T("\n"),
        line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_GETDLLVERSION not defined.\n"),  line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_SUPPORT_GETDLLVERSION
    case TOK_GETFILETIME:
#ifdef NSIS_SUPPORT_GETFILETIME
      ent.which=EW_GETFILETIME;
      ent.offsets[0]=GetUserVarIndex(line, 2);
      ent.offsets[1]=GetUserVarIndex(line, 3);
      ent.offsets[2]=add_string(line.gettoken_str(1));
      if (ent.offsets[0]<0 || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("GetFileTime: %") NPRIs _T("->%") NPRIs _T(",%") NPRIs _T("\n"),
        line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_GETFILETIME not defined.\n"),  line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_SUPPORT_GETFILETIME
#ifdef NSIS_SUPPORT_INTOPTS
    case TOK_INTOP: case TOK_INTPTROP:
      {
        const TCHAR *val1=line.gettoken_str(2), *opstr=0, *val2=0, *cmdname=get_commandtoken_name(which_token);
        int t64 = is_target_64bit(), res;
        ent.which=EW_INTOP;
        ent.offsets[0]=GetUserVarIndex(line, 1);
        ent.offsets[3]=line.gettoken_enum(3,_T("+\0-\0*\0/\0|\0&\0^\0!\0||\0&&\0%\0<<\0>>\0>>>\0~\0"));
        if (ent.offsets[0] < 0 || ent.offsets[3] < 0 || ((ent.offsets[3] == 7 || ent.offsets[3] == 14) && line.getnumtokens() > 4))
          PRINTHELP()
        if (ent.offsets[3] != 7 && ent.offsets[3] != 14) val2=line.gettoken_str(4);
        if (ent.offsets[3] == 14) val2=t64?_T("0xFFFFFFFFFFFFFFFF"):_T("0xFFFFFFFF"), ent.offsets[3]=6, opstr = _T("^"); // ~ using ^
        if (TOK_INTPTROP == which_token && t64)
        {
          res = doParse(2, _T("System::Int64Op %") NPRIs _T(" %") NPRIs _T(" %") NPRIs _T("\n"), val1, opstr ? opstr : line.gettoken_str(3), val2 ? val2 : _T(""));
          if (res != PS_OK) return res;
          ent.which=EW_PUSHPOP, ent.offsets[1]=1, ent.offsets[3]=0; // Pop $result
        }
        else
        {
          ent.offsets[1]=add_string(val1);
          if (val2) ent.offsets[2]=add_string(val2);
        }
        SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("=%") NPRIs _T("%") NPRIs _T("%") NPRIs _T("\n"),cmdname,line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
      }
    return add_entry(&ent);
    case TOK_INTFMT: case TOK_INT64FMT:
      ent.which=EW_INTFMT;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0]<0) PRINTHELP()
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=add_string(line.gettoken_str(3));
      ent.offsets[3]=which_token == TOK_INT64FMT;
      if (ent.offsets[3] && !is_target_64bit()) return (ERROR_MSG(_T("%") NPRIns _T("\n"), "Instruction only supported by 64-bit targets!"), PS_ERROR);
      SCRIPT_MSG(_T("IntFmt: %") NPRIs _T("->%") NPRIs _T(" (fmt:%") NPRIs _T(")\n"),line.gettoken_str(3),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_INTCMP: case TOK_INTCMPU: case TOK_INT64CMP: case TOK_INT64CMPU: case TOK_INTPTRCMP: case TOK_INTPTRCMPU:
      {
        int t64 = is_target_64bit(), o32 = TOK_INTCMP == which_token || TOK_INTCMPU == which_token, o64 = TOK_INT64CMP == which_token || TOK_INT64CMPU == which_token;
        if (!t64 && o64) return (ERROR_MSG(_T("%") NPRIns _T("\n"), "Instruction only supported by 64-bit targets!"), PS_ERROR);
        ent.which=EW_INTCMP;
        ent.offsets[0]=add_string(line.gettoken_str(1));
        ent.offsets[1]=add_string(line.gettoken_str(2));
        ent.offsets[5]=(which_token == TOK_INTCMPU || which_token == TOK_INT64CMPU || which_token == TOK_INTPTRCMPU) | (t64 && !o32 ? 0x8000 : 0);
        if (process_jump(line,3,&ent.offsets[2]) | process_jump(line,4,&ent.offsets[3]) | process_jump(line,5,&ent.offsets[4]))
          PRINTHELP()
        SCRIPT_MSG(_T("%") NPRIs _T(" %") NPRIs _T(":%") NPRIs _T(" equal=%") NPRIs _T(", < %") NPRIs _T(", > %") NPRIs _T("\n"),line.gettoken_str(0),
          line.gettoken_str(1),line.gettoken_str(2), line.gettoken_str(3),line.gettoken_str(4),line.gettoken_str(5));
        return add_entry(&ent);
      }
#else
    case TOK_INTOP: case TOK_INTPTROP:
    case TOK_INTFMT: case TOK_INT64FMT:
    case TOK_INTCMP: case TOK_INTCMPU: case TOK_INT64CMP: case TOK_INT64CMPU: case TOK_INTPTRCMP: case TOK_INTPTRCMPU:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_INTOPTS not defined.\n"),  line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_SUPPORT_INTOPTS
#ifdef NSIS_SUPPORT_REGISTRYFUNCTIONS
    case TOK_READREGSTR:
    case TOK_READREGDWORD:
      {
        ent.which=EW_READREGSTR;
        ent.offsets[0]=GetUserVarIndex(line, 1);
        HKEY hRK = ParseRegRootKey(line,2);
        if (ent.offsets[0] == -1 || INVALIDREGROOT == hRK) PRINTHELP()
        ent.offsets[1]=REGROOTKEYTOINT(hRK);
        ent.offsets[2]=add_string(line.gettoken_str(3));
        ent.offsets[3]=add_string(line.gettoken_str(4));
        if (which_token == TOK_READREGDWORD) ent.offsets[4]=1;
        else ent.offsets[4]=0;
        if (line.gettoken_str(3)[0] == _T('\\'))
          warning_fl(DW_PARSE_REGPATHPREFIX, _T("%") NPRIs _T(": registry path name begins with \'\\\', may cause problems"),line.gettoken_str(0));

        SCRIPT_MSG(_T("%") NPRIs _T(" %") NPRIs _T(" %") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("\n"),line.gettoken_str(0),
          line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
      }
    return add_entry(&ent);
    case TOK_DELETEREGVALUE:
    case TOK_DELETEREGKEY:
      {
        int a=1, iskeyop;
        if ((iskeyop = which_token == TOK_DELETEREGKEY))
        {
          for (;; ++a)
          {
            switch(line.gettoken_enum(a,_T("/ifempty\0/ifnosubkeys\0/ifnovalues\0")))
            {
            case 0: ent.offsets[4]|=((DELREGKEY_ONLYIFNOSUBKEYS|DELREGKEY_ONLYIFNOVALUES)<<DELREGKEYFLAGSSHIFT); continue;
            case 1: ent.offsets[4]|=((DELREGKEY_ONLYIFNOSUBKEYS                         )<<DELREGKEYFLAGSSHIFT); continue;
            case 2: ent.offsets[4]|=((                          DELREGKEY_ONLYIFNOVALUES)<<DELREGKEYFLAGSSHIFT); continue;
            }
            break;
          }
          if (line.gettoken_str(a+2)[0]) PRINTHELP()
        }
        HKEY hRK=ParseRegRootKey(line,a);
        if (INVALIDREGROOT == hRK) PRINTHELP()
        ent.which=EW_DELREG;
        ent.offsets[1]=REGROOTKEYTOINTEX(hRK, iskeyop); // DELETEREGKEY needs the REGSAM view bits in parm4 and NOT in HKEY while DELETEREGVALUE needs them in HKEY.
        ent.offsets[2]=add_string(line.gettoken_str(a+1));
        ent.offsets[3]=iskeyop ? 0 : add_string(line.gettoken_str(a+2));
        ent.offsets[4]|=iskeyop ? DELREG_KEY|(REGROOTVIEWTOSAMVIEW(hRK)<<DELREGKEYFLAGSSHIFT) : DELREG_VALUE;
        if (line.gettoken_str(a+1)[0] == _T('\\'))
          warning_fl(DW_PARSE_REGPATHPREFIX, _T("%") NPRIs _T(": registry path name begins with \'\\\', may cause problems"),line.gettoken_str(0));
        if (iskeyop)
          SCRIPT_MSG(_T("DeleteRegKey: %") NPRIs _T("\\%") NPRIs _T("\n"),line.gettoken_str(a),line.gettoken_str(a+1));
        else
          SCRIPT_MSG(_T("DeleteRegValue: %") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("\n"),line.gettoken_str(a),line.gettoken_str(a+1),line.gettoken_str(a+2));
      }
    return add_entry(&ent);
    case TOK_WRITEREGSTR: case TOK_WRITEREGEXPANDSTR:
    case TOK_WRITEREGBIN: case TOK_WRITEREGNONE:
    case TOK_WRITEREGMULTISZ:
    case TOK_WRITEREGDWORD:
      {
        const TCHAR*cmdname=get_commandtoken_name(which_token);
        int reg5=0==line.gettoken_enum(1,_T("/REGEDIT5\0")), multisz=which_token == TOK_WRITEREGMULTISZ;
        if (reg5) line.eattoken();
        HKEY hRK=ParseRegRootKey(line,1);
        if (INVALIDREGROOT == hRK || reg5 != multisz) PRINTHELPEX(cmdname); // WriteRegMultiStr only supports the /REGEDIT5 serialized format right now but we really should allow variables at some point.
        ent.which=EW_WRITEREG;
        ent.offsets[0]=REGROOTKEYTOINT(hRK);
        ent.offsets[1]=add_string(line.gettoken_str(2));
        if (line.gettoken_str(2)[0] == _T('\\'))
          warning_fl(DW_PARSE_REGPATHPREFIX, _T("%") NPRIs _T(": registry path name begins with \'\\\', may cause problems"),cmdname);
        ent.offsets[2]=add_string(line.gettoken_str(3));
        if (which_token == TOK_WRITEREGSTR || which_token == TOK_WRITEREGEXPANDSTR)
        {
          SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("=%") NPRIs _T("\n"),
            cmdname,line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
          ent.offsets[3]=add_string(line.gettoken_str(4));
          ent.offsets[4]=ent.offsets[5]=REG_SZ;
          if (which_token == TOK_WRITEREGEXPANDSTR)
            ent.offsets[5]=REG_EXPAND_SZ;
        }
        if (which_token == TOK_WRITEREGBIN || multisz || which_token == TOK_WRITEREGNONE)
        {
          char data[3*NSIS_MAX_STRLEN];
          int data_len=line.gettoken_binstrdata(4, data, sizeof(data)), none=which_token == TOK_WRITEREGNONE;
          if (data_len < 0)
          {
            if (data_len == -2) PRINTHELPEX(cmdname);
            ERROR_MSG(_T("%") NPRIs _T(": %d bytes of data exceeded\n"),cmdname,sizeof(data));
            return PS_ERROR;
          }
          if (multisz && (data_len < 4 || 0 != read_ptr_as<UINT32>(&data[data_len-4]))) PRINTHELPEX(cmdname); // Must end with 4 zero bytes
          SCRIPT_MSG(_T("%") NPRIs _T(": %") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("=%") NPRIs _T("\n"),
            cmdname,line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
          if (multisz && !build_unicode) for (int p1=0, p2=p1; p1 < data_len; data_len--) data[p1++]=data[p2], p2+=2; // BUGBUG: Should convert each string from UTF-16 to DBCS but only exehead knows the codepage, limited to ASCII for now.
          ent.offsets[3]=add_db_data(data,data_len);
          if (ent.offsets[3] < 0) return PS_ERROR;
          ent.offsets[4]=REG_BINARY, ent.offsets[5]=none?REG_NONE:multisz?REG_MULTI_SZ:REG_BINARY;
        }
        if (which_token == TOK_WRITEREGDWORD)
        {
          ent.offsets[3]=add_string(line.gettoken_str(4));
          ent.offsets[4]=ent.offsets[5]=REG_DWORD;

          SCRIPT_MSG(_T("WriteRegDWORD: %") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("=%") NPRIs _T("\n"),
            line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
        }
      }
    return add_entry(&ent);
    case TOK_ENUMREGKEY:
    case TOK_ENUMREGVAL:
      {
        ent.which=EW_REGENUM;
        ent.offsets[0]=GetUserVarIndex(line, 1);
        HKEY hRK=ParseRegRootKey(line,2);
        if (ent.offsets[0] == -1 || INVALIDREGROOT == hRK) PRINTHELP()
        ent.offsets[1]=REGROOTKEYTOINT(hRK);
        ent.offsets[2]=add_string(line.gettoken_str(3));
        ent.offsets[3]=add_string(line.gettoken_str(4));
        ent.offsets[4]=which_token == TOK_ENUMREGKEY;
        if (line.gettoken_str(3)[0] == _T('\\')) warning_fl(DW_PARSE_REGPATHPREFIX, _T("%") NPRIs _T(": registry path name begins with \'\\\', may cause problems"),line.gettoken_str(0));
        SCRIPT_MSG(_T("%") NPRIs _T(" %") NPRIs _T(" %") NPRIs _T("\\%") NPRIs _T("\\%") NPRIs _T("\n"),which_token == TOK_ENUMREGKEY ? _T("EnumRegKey") : _T("EnumRegValue"),
          line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(4));
      }
    return add_entry(&ent);
#else
    case TOK_READREGSTR:
    case TOK_READREGDWORD:
    case TOK_DELETEREGVALUE:
    case TOK_DELETEREGKEY:
    case TOK_WRITEREGSTR:
    case TOK_WRITEREGEXPANDSTR:
    case TOK_WRITEREGBIN:
    case TOK_WRITEREGMULTISZ:
    case TOK_WRITEREGDWORD:
    case TOK_ENUMREGKEY:
    case TOK_ENUMREGVAL:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_REGISTRYFUNCTIONS not defined.\n"),  line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_SUPPORT_REGISTRYFUNCTIONS
#ifdef NSIS_SUPPORT_STACK
    case TOK_EXCH:
      {
        int swapitem=1;
        int save=GetUserVarIndex(line, 1);
        ent.which=EW_PUSHPOP;
        if (line.gettoken_str(1)[0] && save<0)
        {
          int succ=0;
          swapitem=line.gettoken_int(1,&succ);
          if (!succ || swapitem <= 0) PRINTHELP()
        }
        if (save>=0)
        {
          SCRIPT_MSG(_T("Exch(%") NPRIs _T(",0)\n"),line.gettoken_str(1));
          ent.offsets[0]=add_string(line.gettoken_str(1));
          ent.offsets[1]=0;
          ent.offsets[2]=0;
          add_entry(&ent);
        }
        else SCRIPT_MSG(_T("Exch(st(%d),0)\n"),swapitem);

        ent.offsets[0]=0;
        ent.offsets[1]=0;
        ent.offsets[2]=swapitem;

        if (save>=0)
        {
          add_entry(&ent);
          ent.offsets[0]=save;
          ent.offsets[1]=1;
          ent.offsets[2]=0;
        }

        DefineInnerLangString(NLF_INST_CORRUPTED);
      }
    return add_entry(&ent);
    case TOK_PUSH:
      ent.which=EW_PUSHPOP;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=0;
      SCRIPT_MSG(_T("Push: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_POP:
      ent.which=EW_PUSHPOP;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      ent.offsets[1]=1;
      if (ent.offsets[0] < 0) PRINTHELP()
      SCRIPT_MSG(_T("Pop: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
#else
    case TOK_POP:
    case TOK_PUSH:
    case TOK_EXCH:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_STACK not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_STACK
#ifdef NSIS_SUPPORT_ENVIRONMENT
    case TOK_READENVSTR:
      ent.which=EW_READENVSTR;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      {
        TCHAR str[NSIS_MAX_STRLEN];
        _tcscpy(str, _T("%"));
        _tcscat(str, line.gettoken_str(2));
        _tcscat(str, _T("%"));
        ent.offsets[1]=add_string(str);
        if (ent.offsets[0] < 0 || _tcslen(line.gettoken_str(2))<1) PRINTHELP()
      }
      ent.offsets[2]=1;
      SCRIPT_MSG(_T("ReadEnvStr: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(2),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_EXPANDENVSTRS:
      ent.which=EW_READENVSTR;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=0;
      if (ent.offsets[0] < 0) PRINTHELP()
      SCRIPT_MSG(_T("ExpandEnvStrings: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(2),line.gettoken_str(1));
    return add_entry(&ent);
#else
    case TOK_EXPANDENVSTRS:
    case TOK_READENVSTR:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_ENVIRONMENT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_ENVIRONMENT
#ifdef NSIS_SUPPORT_FINDFIRST
    case TOK_FINDFIRST:
      ent.which=EW_FINDFIRST;
      ent.offsets[0]=GetUserVarIndex(line, 2); // out
      ent.offsets[1]=GetUserVarIndex(line, 1); // handleout
      ent.offsets[2]=add_string(line.gettoken_str(3)); // filespec
      if (ent.offsets[0] < 0 || ent.offsets[1] < 0) PRINTHELP()
      SCRIPT_MSG(_T("FindFirst: spec=\"%") NPRIs _T("\" handle=%") NPRIs _T(" output=%") NPRIs _T("\n"),line.gettoken_str(3),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_FINDNEXT:
      ent.which=EW_FINDNEXT;
      ent.offsets[0]=GetUserVarIndex(line, 2);
      ent.offsets[1]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0 || ent.offsets[1] < 0) PRINTHELP()
      SCRIPT_MSG(_T("FindNext: handle=%") NPRIs _T(" output=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_FINDCLOSE:
      ent.which=EW_FINDCLOSE;
      ent.offsets[0]=GetUserVarIndex(line, 1);
      if (ent.offsets[0] < 0) PRINTHELP()
      SCRIPT_MSG(_T("FindClose: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
#else
    case TOK_FINDCLOSE:
    case TOK_FINDNEXT:
    case TOK_FINDFIRST:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_FINDFIRST not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_FINDFIRST
#ifdef NSIS_SUPPORT_FILEFUNCTIONS
    case TOK_FILEOPEN:
      {
        ent.which=EW_FOPEN;
        ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
        ent.offsets[3]=add_string(line.gettoken_str(2));
        ent.offsets[1]=0; //openmode
        if (!_tcsicmp(line.gettoken_str(3),_T("r")))
        {
          ent.offsets[1]=GENERIC_READ;
          ent.offsets[2]=OPEN_EXISTING;
        }
        else if (!_tcsicmp(line.gettoken_str(3),_T("w")))
        {
          ent.offsets[1]=GENERIC_WRITE;
          ent.offsets[2]=CREATE_ALWAYS;
        }
        else if (!_tcsicmp(line.gettoken_str(3),_T("a")))
        {
          ent.offsets[1]=GENERIC_WRITE|GENERIC_READ;
          ent.offsets[2]=OPEN_ALWAYS;
        }

        if (ent.offsets[0] < 0 || !ent.offsets[1]) PRINTHELP()
      }
      SCRIPT_MSG(_T("FileOpen: %") NPRIs _T(" as %") NPRIs _T(" -> %") NPRIs _T("\n"),line.gettoken_str(2),line.gettoken_str(3),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_FILECLOSE:
      ent.which=EW_FCLOSE;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      if (ent.offsets[0] < 0) PRINTHELP()
      SCRIPT_MSG(_T("FileClose: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_FILEREAD:
      ent.which=EW_FGETS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=GetUserVarIndex(line, 2); // output string
      if (line.gettoken_str(3)[0])
        ent.offsets[2]=add_string(line.gettoken_str(3));
      else
        ent.offsets[2]=add_intstring(NSIS_MAX_STRLEN-1);
      if (ent.offsets[0]<0 || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileRead: %") NPRIs _T("->%") NPRIs _T(" (max:%") NPRIs _T(")\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
    case TOK_FILEWRITE:
      ent.which=EW_FPUTS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=add_string(line.gettoken_str(2));
      if (ent.offsets[0]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileWrite: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(2),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_FILEREADBYTE:
      ent.which=EW_FGETS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=GetUserVarIndex(line, 2); // output string
      ent.offsets[2]=add_asciistring(_T("1"));
      ent.offsets[3]=1;
      if (ent.offsets[0]<0 || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileReadByte: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_FILEWRITEBYTE:
      ent.which=EW_FPUTS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=1;
      if (ent.offsets[0]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileWriteByte: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(2),line.gettoken_str(1));
    return add_entry(&ent);
#ifdef _UNICODE
    case TOK_FILEREADUTF16LE:
      if (!build_unicode)
      {
        ERROR_MSG(_T("Error: %") NPRIs _T(" is only available when building a Unicode installer\n"),  line.gettoken_str(0));
        return PS_ERROR;
      }
      ent.which=EW_FGETWS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=GetUserVarIndex(line, 2); // output string
      if (line.gettoken_str(3)[0])
        ent.offsets[2]=add_string(line.gettoken_str(3));
      else
        ent.offsets[2]=add_intstring(NSIS_MAX_STRLEN-1);
      if (ent.offsets[0]<0 || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileReadUTF16LE: %") NPRIs _T("->%") NPRIs _T(" (max:%") NPRIs _T(")\n"),line.gettoken_str(1),line.gettoken_str(2),line.gettoken_str(3));
    return add_entry(&ent);
    case TOK_FILEWRITEUTF16LE:
      if (!build_unicode)
      {
        ERROR_MSG(_T("Error: %") NPRIs _T(" is only available when building a Unicode installer\n"),  line.gettoken_str(0));
        return PS_ERROR;
      }
      {
        UINT bom=0, swofs=0;
        if (!_tcsicmp(_T("/BOM"),line.gettoken_str(swofs+1))) ++bom, ++swofs;
        if (!_tcsicmp(_T("/NoBOM"),line.gettoken_str(swofs+1))) bom = 0, ++swofs; // Undocumented switch
        ent.which=EW_FPUTWS;
        ent.offsets[0]=GetUserVarIndex(line, swofs+1); // file handle
        ent.offsets[1]=add_string(line.gettoken_str(swofs+2));
        ent.offsets[3]=bom;
        if (ent.offsets[0]<0 || line.getnumtokens()-swofs != 3) PRINTHELP()
        SCRIPT_MSG(_T("FileWriteUTF16LE: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(swofs+2),line.gettoken_str(swofs+1));
      }
    return add_entry(&ent);
    case TOK_FILEREADWORD:
      if (!build_unicode)
      {
        ERROR_MSG(_T("Error: %") NPRIs _T(" is only available when building a Unicode installer\n"),  line.gettoken_str(0));
        return PS_ERROR;
      }
      ent.which=EW_FGETWS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=GetUserVarIndex(line, 2); // output string
      ent.offsets[2]=add_asciistring(_T("1"));
      ent.offsets[3]=1;
      if (ent.offsets[0]<0 || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileReadWord: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_FILEWRITEWORD:
      if (!build_unicode)
      {
        ERROR_MSG(_T("Error: %") NPRIs _T(" is only available when building a Unicode installer\n"),  line.gettoken_str(0));
        return PS_ERROR;
      }
      ent.which=EW_FPUTWS;
      ent.offsets[0]=GetUserVarIndex(line, 1); // file handle
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=1;
      if (ent.offsets[0]<0) PRINTHELP()
      SCRIPT_MSG(_T("FileWriteWord: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(2),line.gettoken_str(1));
    return add_entry(&ent);
#endif //~ _UNICODE
    case TOK_FILESEEK:
      {
        const TCHAR *modestr;
        int tab[3]={FILE_BEGIN,FILE_CURRENT,FILE_END};
        int mode=line.gettoken_enum(3,_T("SET\0CUR\0END\0"));
        ent.which=EW_FSEEK;
        ent.offsets[0]=GetUserVarIndex(line, 1);
        ent.offsets[1]=GetUserVarIndex(line, 4);
        ent.offsets[2]=add_string(line.gettoken_str(2));

        if (mode<0 && !line.gettoken_str(3)[0])
        {
          mode=0;
          modestr=_T("SET");
        }
        else modestr=line.gettoken_str(3);

        if (mode<0 || ent.offsets[0] < 0 || (ent.offsets[1]<0 && line.gettoken_str(4)[0])) PRINTHELP()
        ent.offsets[3]=tab[mode];
        SCRIPT_MSG(_T("FileSeek: fp=%") NPRIs _T(", ofs=%") NPRIs _T(", mode=%") NPRIs _T(", output=%") NPRIs _T("\n"),
          line.gettoken_str(1),
          line.gettoken_str(2),
          modestr,
          line.gettoken_str(4));
      }

    return add_entry(&ent);
#else
    case TOK_FILEOPEN:
    case TOK_FILECLOSE:
    case TOK_FILESEEK:
    case TOK_FILEREAD:
    case TOK_FILEWRITE:
    case TOK_FILEREADBYTE:
    case TOK_FILEWRITEBYTE:
#ifdef _UNICODE
    case TOK_FILEREADUTF16LE:
    case TOK_FILEWRITEUTF16LE:
    case TOK_FILEREADWORD:
    case TOK_FILEWRITEWORD:
#endif
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_FILEFUNCTIONS not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_FILEFUNCTIONS
#ifdef NSIS_SUPPORT_REBOOT
    case TOK_REBOOT:
    {
      int ret = add_entry_direct(EW_REBOOT, 0xbadf00d);
      if (ret != PS_OK) return ret;

      ret = add_entry_direct(EW_QUIT);
      if (ret != PS_OK) return ret;

      SCRIPT_MSG(_T("Reboot!\n"));

      DefineInnerLangString(NLF_INST_CORRUPTED);
    }
    return PS_OK;
    case TOK_IFREBOOTFLAG:
      return add_flag_instruction_entry(which_token, EW_IFFLAG, line, FLAG_OFFSET(exec_reboot), ~0); //new value mask - keep flag
    case TOK_SETREBOOTFLAG:
      ent.offsets[1] = line.gettoken_enum(1,_T("false\0true\0"));
      if (ent.offsets[1] < 0) PRINTHELP()
      return add_flag_instruction_entry(which_token, EW_SETFLAG, line, FLAG_OFFSET(exec_reboot), add_intstring(ent.offsets[1]));
#else
    case TOK_REBOOT:
    case TOK_IFREBOOTFLAG:
    case TOK_SETREBOOTFLAG:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_REBOOT not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_SUPPORT_REBOOT
#ifdef NSIS_CONFIG_LOG
    case TOK_LOGSET:
      ent.which=EW_LOG;
      ent.offsets[0]=1;
      ent.offsets[1]=line.gettoken_enum(1,_T("off\0on\0"));
      if (ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("LogSet: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_LOGTEXT:
      ent.which=EW_LOG;
      ent.offsets[0]=0;
      ent.offsets[1]=add_string(line.gettoken_str(1));
      SCRIPT_MSG(_T("LogText \"%") NPRIs _T("\"\n"),line.gettoken_str(1));
    return add_entry(&ent);
#else
    case TOK_LOGSET:
    case TOK_LOGTEXT:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_LOG not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_LOG
#ifdef NSIS_CONFIG_COMPONENTPAGE
    case TOK_SECTIONSETTEXT:
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[2]=SECTION_FIELD_SET(name_ptr);
      ent.offsets[4]=add_string(line.gettoken_str(2));
      if (!IsIntOrUserVar(line,1)) PRINTHELP()
      SCRIPT_MSG(_T("SectionSetText: %") NPRIs _T("=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SECTIONGETTEXT:
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=GetUserVarIndex(line, 2);
      ent.offsets[2]=SECTION_FIELD_GET(name_ptr);
      if (!IsIntOrUserVar(line,1) || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("SectionGetText: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SECTIONSETFLAGS:
    {
      int num, conv;
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=(num=line.gettoken_intx(2,&conv), conv) ? add_intstring(num) : add_string(line.gettoken_str(2));
      ent.offsets[2]=SECTION_FIELD_SET(flags);
      ent.offsets[3]=1;
      if (!IsIntOrUserVar(line,1) || (!conv && !IsIntOrUserVar(line,2))) PRINTHELP()
      SCRIPT_MSG(_T("SectionSetFlags: %") NPRIs _T("=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
      return add_entry(&ent);
    }
    case TOK_SECTIONGETFLAGS:
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=GetUserVarIndex(line, 2);
      ent.offsets[2]=SECTION_FIELD_GET(flags);
      if (!IsIntOrUserVar(line,1) || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("SectionGetFlags: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_INSTTYPESETTEXT:
      ent.which=EW_INSTTYPESET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=1;
      SCRIPT_MSG(_T("InstTypeSetText: %") NPRIs _T("=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_INSTTYPEGETTEXT:
      ent.which=EW_INSTTYPESET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=GetUserVarIndex(line, 2);
      ent.offsets[2]=0;
      if (!IsIntOrUserVar(line,1) || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("InstTypeGetText: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SECTIONSETINSTTYPES:
    {
      int num, conv;
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=(num=line.gettoken_intx(2,&conv), conv) ? add_intstring(num) : add_string(line.gettoken_str(2));
      ent.offsets[2]=SECTION_FIELD_SET(install_types);
      if (!IsIntOrUserVar(line,1) || (!conv && !IsIntOrUserVar(line,2))) PRINTHELP()
      SCRIPT_MSG(_T("SectionSetInstTypes: %") NPRIs _T("=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
      return add_entry(&ent);
    }
    case TOK_SECTIONGETINSTTYPES:
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=GetUserVarIndex(line, 2);
      ent.offsets[2]=SECTION_FIELD_GET(install_types);
      if (!IsIntOrUserVar(line,1) || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("SectionGetInstTypes: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SECTIONSETSIZE:
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=add_string(line.gettoken_str(2));
      ent.offsets[2]=SECTION_FIELD_SET(size_kb);
      SCRIPT_MSG(_T("SectionSetSize: %") NPRIs _T("=%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SECTIONGETSIZE:
      ent.which=EW_SECTIONSET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=GetUserVarIndex(line, 2);
      ent.offsets[2]=SECTION_FIELD_GET(size_kb);
      if (!IsIntOrUserVar(line,1) || ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("SectionGetSize: %") NPRIs _T("->%") NPRIs _T("\n"),line.gettoken_str(1),line.gettoken_str(2));
    return add_entry(&ent);
    case TOK_SETCURINSTTYPE:
      ent.which=EW_INSTTYPESET;
      ent.offsets[0]=add_string(line.gettoken_str(1));
      ent.offsets[1]=0;
      ent.offsets[2]=1;
      ent.offsets[3]=1;
      SCRIPT_MSG(_T("SetCurInstType: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
    case TOK_GETCURINSTTYPE:
      ent.which=EW_INSTTYPESET;
      ent.offsets[0]=0;
      ent.offsets[1]=GetUserVarIndex(line,1);
      ent.offsets[2]=0;
      ent.offsets[3]=1;
      if (ent.offsets[1]<0) PRINTHELP()
      SCRIPT_MSG(_T("GetCurInstType: %") NPRIs _T("\n"),line.gettoken_str(1));
    return add_entry(&ent);
#else
    case TOK_SECTIONSETTEXT:
    case TOK_SECTIONGETTEXT:
    case TOK_SECTIONSETFLAGS:
    case TOK_SECTIONGETFLAGS:
    case TOK_SECTIONSETSIZE:
    case TOK_SECTIONGETSIZE:
    case TOK_SECTIONSETINSTTYPES:
    case TOK_SECTIONGETINSTTYPES:
    case TOK_SETCURINSTTYPE:
    case TOK_GETCURINSTTYPE:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_COMPONENTPAGE not defined.\n"),  line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_COMPONENTPAGE
#ifdef NSIS_CONFIG_ENHANCEDUI_SUPPORT
    case TOK_SETBRANDINGIMAGE:
    {
      SCRIPT_MSG(_T("SetBrandingImage: "));
      ent.which=EW_LOADANDSETIMAGE;
      for (int i = 1; i < line.getnumtokens(); i++)
        if (!_tcsnicmp(line.gettoken_str(i),_T("/IMGID="),7)) {
          ent.offsets[2]=_ttoi(line.gettoken_str(i)+7);
          SCRIPT_MSG(_T("/IMGID=%d "),ent.offsets[2]);
        }
        else if (!_tcsicmp(line.gettoken_str(i),_T("/RESIZETOFIT"))) {
          ent.offsets[3]=LASIF_FITCTLW|LASIF_FITCTLH;
          SCRIPT_MSG(_T("/RESIZETOFIT "));
        }
        else if (!ent.offsets[1]) {
          ent.offsets[1]=add_string(line.gettoken_str(i));
          SCRIPT_MSG(_T("\"%") NPRIs _T("\" "), line.gettoken_str(i));
        }
        else {
          SCRIPT_MSG(_T("\n"));
          PRINTHELP();
        }
      SCRIPT_MSG(_T("\n"));
      if (!ent.offsets[2])
      {
        ent.offsets[2]=branding_image_id;
        if (!branding_image_found) {
          ERROR_MSG(_T("\nError: no branding image found in chosen UI!\n"));
          return PS_ERROR;
        }
      }
      ent.offsets[0]=-1; // Outvar
      ent.offsets[3]|=LASIF_LR_LOADFROMFILE|LASIF_STRID;
    }
    return add_entry(&ent);
    case TOK_LOADANDSETIMAGE:
    {
      ent.which=EW_LOADANDSETIMAGE;
      int tidx = 1, conv = 1, fail = 0;
      unsigned int flags = LASIF_HWND, lrflagsin;
      for (; tidx < line.getnumtokens(); tidx++)
      {
        if (!_tcsicmp(line.gettoken_str(tidx),_T("/EXERESOURCE"))) flags |= LASIF_EXERES;
        else if (!_tcsicmp(line.gettoken_str(tidx),_T("/STRINGID"))) flags |= LASIF_STRID;
        else if (!_tcsicmp(line.gettoken_str(tidx),_T("/RESIZETOFIT"))) flags |= LASIF_FITCTLW|LASIF_FITCTLH;
        else if (!_tcsicmp(line.gettoken_str(tidx),_T("/RESIZETOFITWIDTH"))) flags |= LASIF_FITCTLW;
        else if (!_tcsicmp(line.gettoken_str(tidx),_T("/RESIZETOFITHEIGHT"))) flags |= LASIF_FITCTLH;
        else if (!_tcsicmp(line.gettoken_str(tidx),_T("/GETDLGITEM"))) flags &= ~LASIF_HWND; // Reuses TOK_SETBRANDINGIMAGE functionality
        else { if (line.gettoken_str(tidx)[0] == '/') ++fail; break; }
      }
      ent.offsets[2] = (flags & LASIF_HWND) ? add_string(line.gettoken_str(tidx+0)) : line.gettoken_int(tidx+0, &conv); fail |= !conv; // HWND/CtrlId
      flags |= (line.gettoken_int(tidx+1, &conv) & LASIM_IMAGE); fail |= !conv; // IMAGE_*
      flags |= ((lrflagsin = line.gettoken_intx(tidx+2, &conv)) & LASIM_LR); fail |= !conv; // LR_*
      ent.offsets[1] = (flags & LASIF_STRID) ? add_string(line.gettoken_str(tidx+3)) : line.gettoken_int(tidx+3, &conv); fail |= !conv; // Image path/resid
      ent.offsets[3] = flags; // Packed flags, IMAGE_* and LR_*
      ent.offsets[0] = GetUserVarIndex(line, tidx+4); // Outvar
      SCRIPT_MSG(_T("LoadAndSetImage %") NPRIs _T(" %#x \"%") NPRIs _T("\""), line.gettoken_str(tidx+0), flags, line.gettoken_str(tidx+3));
      if (ent.offsets[0] >= 0) SCRIPT_MSG(_T(" ->%") NPRIs _T(""), line.gettoken_str(tidx+4));
      SCRIPT_MSG(_T("\n"));
      if ((lrflagsin & LASIM_LR) != lrflagsin) warning_fl(DW_PARSE_NUMBEROUTOFSPEC, _T("Out of spec: \"%") NPRIs _T("\""), line.gettoken_str(tidx+2));
      if (fail) PRINTHELP();
    }
    return add_entry(&ent);
#else
    case TOK_SETBRANDINGIMAGE:
    case TOK_LOADANDSETIMAGE:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_ENHANCEDUI_SUPPORT not defined.\n"),line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_CONFIG_ENHANCEDUI_SUPPORT
    case TOK_DEFVAR:
    {
      int a=1;
      if (!_tcsicmp(line.gettoken_str(1),_T("/GLOBAL")))
        a++;
      else if (line.getnumtokens() == 3)
        PRINTHELP();

      if (build_cursection)
      {
        if (a==1)
        {
          ERROR_MSG(_T("Var: currently, only global variables can be defined.\n"));
          PRINTHELP();
        }
      }
      SCRIPT_MSG(_T("Var: \"%") NPRIs _T("\"\n"),line.gettoken_str(a));
      return DeclaredUserVar(line.gettoken_str(a));
    }
    return PS_OK;
#ifdef NSIS_SUPPORT_VERSION_INFO
    case TOK_VI_ADDKEY:
    {
      LANGID LangID=0;
      int a = 1;
      // Allow people to force Neutral (if /LANG=* is not present it uses the default)
      const bool forceneutrallang = !_tcsicmp(line.gettoken_str(a),_T("/LANG=0"));

      if (!_tcsnicmp(line.gettoken_str(a),_T("/LANG="),6))
        LangID=ParseLangId(line.gettoken_str(a++)+6);
      if (line.getnumtokens()!=a+2) PRINTHELP();
      TCHAR *pKey = line.gettoken_str(a);
      TCHAR *pValue = line.gettoken_str(a+1);
      if ( !(*pKey) )
      {
         ERROR_MSG(_T("Error: empty name for version info key!\n"));
         return PS_ERROR;
      }
      else
      {
        SCRIPT_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\" \"%") NPRIs _T("\"\n"), line.gettoken_str(0), line.gettoken_str(a), line.gettoken_str(a+1));
        const bool allowdeflangfallback = a <= 1 && !forceneutrallang;
        if ( a > 1 && 0 == LangID && !forceneutrallang)
        {
          ERROR_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\" is not a valid language code!\n"),line.gettoken_str(0), line.gettoken_str(1));
          return PS_ERROR;
        }

        unsigned int codepage;
        // We rely on GetLangNameAndCPForVersionResource to update LangID if required
        const TCHAR *lang_name = GetLangNameAndCPForVersionResource(LangID, &codepage, allowdeflangfallback);

        if ( rVersionInfo.SetKeyValue(LangID, codepage, pKey, pValue) )
        {
          ERROR_MSG(_T("%") NPRIs _T(": \"%") NPRIs _T("\" \"%04d-%") NPRIs _T("\" already defined!\n"),line.gettoken_str(0), line.gettoken_str(2), LangID, lang_name);
          return PS_ERROR;
        }

        return PS_OK;
      }
    }
      case TOK_VI_SETPRODUCTVERSION:
      case TOK_VI_SETFILEVERSION:
      // Probably not required, but this code retains the <= 2.46 behaviour and
      // does not fail on bad product version number here, it "validates" in CEXEBuild::AddVersionInfo()
      //
      // It is ok for us to use rVersionInfo as storage since VIProductVersion is required by VIAddVersionKey 
      {
        const bool settingFileVer = TOK_VI_SETFILEVERSION == which_token;
        const unsigned int reuseFlag = settingFileVer ? 4 : 1;
        if (reuseFlag & version_fixedflags) 
        {
          ERROR_MSG(_T("Error: %") NPRIs _T(" already defined!\n"), line.gettoken_str(0));
          return PS_ERROR;
        }
        version_fixedflags |= reuseFlag;
        int imm, iml, ilm, ill;
        const bool validInput = _stscanf(line.gettoken_str(1), _T("%d.%d.%d.%d"), &imm, &iml, &ilm, &ill) == 4;
        if (settingFileVer)
        {
          if (!validInput) 
          {
            ERROR_MSG(_T("Error: invalid %") NPRIs _T(" format, should be X.X.X.X\n"),line.gettoken_str(0));
            return PS_ERROR;
          }
          rVersionInfo.SetFileVersion(MAKELONG(iml, imm),MAKELONG(ill, ilm));
        }
        else
        {
          if (validInput) 
          {
            version_fixedflags |= 2;
            rVersionInfo.SetProductVersion(MAKELONG(iml, imm),MAKELONG(ill, ilm));
            // FileVersion defaults to ProductVersion
            if (!(4 & version_fixedflags)) rVersionInfo.SetFileVersion(MAKELONG(iml, imm),MAKELONG(ill, ilm));
          }
        }
      }
      return PS_OK;
#else
    case TOK_VI_ADDKEY:
    case TOK_VI_SETPRODUCTVERSION:
    case TOK_VI_SETFILEVERSION:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_SUPPORT_VERSION_INFO not defined.\n"),line.gettoken_str(0));
      return PS_ERROR;
#endif //~ NSIS_SUPPORT_VERSION_INFO
    case TOK_LOCKWINDOW:
#ifdef NSIS_LOCKWINDOW_SUPPORT
      SCRIPT_MSG(_T("LockWindow: lock state=%d\n"),line.gettoken_str(1));
      ent.which=EW_LOCKWINDOW;
      ent.offsets[0]=line.gettoken_enum(1,_T("on\0off\0"));
      if (ent.offsets[0] == -1) PRINTHELP();
    return add_entry(&ent);
#else
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_LOCKWINDOW_SUPPORT not defined.\n"),line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_LOCKWINDOW_SUPPORT

    // end of instructions
    ///////////////////////////////////////////////////////////////////////////////

#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
    case TOK_PLUGINDIR:
    {
      CEXEBuild::TARGETTYPE tt = m_target_type;
      int numtok = line.getnumtokens() - 1;
      TCHAR *path = line.gettoken_str(numtok);
      const TCHAR *cmdnam = get_commandtoken_name(which_token), *arcstr = 0;
      if (2 == numtok)
      {
        arcstr = line.gettoken_str(--numtok);
        if (_T('/') != *arcstr || CEXEBuild::TARGET_UNKNOWN == (tt = get_target_type(++arcstr)))
        {
          print_bad_targettype_parameter(cmdnam, _T("/"));
          return PS_ERROR;
        }
      }
      if (1 == numtok && *path)
      {
        const TCHAR *fmtstr = _T("%") NPRIs _T(": \"%") NPRIs _T("\"%") NPRIs _T("%") NPRIs _T("%") NPRIs _T("\n");
        SCRIPT_MSG(fmtstr, cmdnam, path, arcstr ? _T(" (") : _T(""), arcstr ? arcstr : _T(""), arcstr ? _T(")") : _T(""));
        PATH_CONVERT(path);
        m_plugins[tt].AddPluginsDir(path, is_targettype_64bit(tt), !!display_script);
        return PS_OK;
      }
    }
    PRINTHELP();
    case TOK__PLUGINCOMMAND:
    {
      tstring command, dllPath;

      if (!m_pPlugins->GetCommandInfo(line.gettoken_str(0), command, dllPath))
      {
        ERROR_MSG(_T("Plugin command %") NPRIs _T(" conflicts with a plugin in another directory!\n"),command.c_str());
        return PS_ERROR;
      }

      tstring dllName = get_file_name(dllPath);
      int data_handle = m_pPlugins->GetDllDataHandle(!!uninstall_mode, command), ret;

      if (uninstall_mode) uninst_plugin_used = true; else plugin_used = true;

      // Initialize $PLUGINSDIR
      ent.which=EW_CALL;
      ent.offsets[0]=ns_func.add(uninstall_mode?_T("un.Initialize_____Plugins"):_T("Initialize_____Plugins"),0);
      if ((ret=add_entry(&ent)) != PS_OK) return ret;

      TCHAR tempDLL[NSIS_MAX_STRLEN];
      wsprintf(tempDLL, _T("$PLUGINSDIR\\%") NPRIs, dllName.c_str()); // DLL name on the end-users machine

      // Add the DLL to the installer
      if (data_handle == -1)
      {
        int files_added=0;
        const int old_build_allowskipfiles=build_allowskipfiles;
        build_allowskipfiles=1; // on
        const int old_build_overwrite=build_overwrite;
        build_overwrite=1; // off
        const int old_build_datesave=build_datesave;
        build_datesave=0; // off
        
        // Jim Park: While the code looks as if the same DLL is added multiple
        // times for each command in the DLL, this is actually not the case
        // because of CEXEBuild::datablock_optimize() that tries to discover
        // duplicates and reuse them.
        ret=do_add_file(dllPath.c_str(),0,0,&files_added,tempDLL,2,&data_handle); // 2 means no size add
        if (ret != PS_OK || files_added == 0) {
          return files_added ? ret : PS_ERROR;
        }
        m_pPlugins->SetDllDataHandle(!!uninstall_mode, command, data_handle);
        build_overwrite=old_build_overwrite;
        build_datesave=old_build_datesave;
        build_allowskipfiles=old_build_allowskipfiles;
      }
      else
      {
        ent.which=EW_EXTRACTFILE;

        DefineInnerLangString(NLF_SKIPPED);
        DefineInnerLangString(NLF_ERR_DECOMPRESSING);
        DefineInnerLangString(NLF_ERR_WRITING);
        DefineInnerLangString(NLF_EXTRACT);
        DefineInnerLangString(NLF_CANT_WRITE);

        ent.offsets[0]=1; // overwrite off
        ent.offsets[0]|=(MB_RETRYCANCEL|MB_ICONSTOP|(IDCANCEL<<21))<<3;
        ent.offsets[1]=add_string(tempDLL);
        ent.offsets[2]=data_handle;
        ent.offsets[3]=0xffffffff;
        ent.offsets[4]=0xffffffff;
        ent.offsets[5]=DefineInnerLangString(NLF_FILE_ERROR);
        if ((ret=add_entry(&ent)) != PS_OK) return ret;
      }

      // SetDetailsPrint lastused (special)
      ret=add_entry_direct(EW_SETFLAG, FLAG_OFFSET(status_update), 0, 1, -1);
      if (ret != PS_OK) return ret;

      // Call the DLL
      tstring funcname = get_string_suffix(command, _T("::"));
      SCRIPT_MSG(_T("Plugin command: %") NPRIs,funcname.c_str());

      int i = 1;
      int nounload = 0;
      if (!_tcsicmp(line.gettoken_str(i), _T("/NOUNLOAD"))) {
        i++, nounload++;
      }

      // First push dll args
      int parmst=i; // we push 'em in reverse order
      int nounloadmisused=0;
      for (; i < line.getnumtokens(); i++) {
        int w=parmst + (line.getnumtokens()-i - 1);
        ent.which=EW_PUSHPOP;
        ent.offsets[0]=add_string(line.gettoken_str(w));
        if (!_tcsicmp(line.gettoken_str(w), _T("/NOUNLOAD"))) nounloadmisused=1;
        ent.offsets[1]=0;
        ent.offsets[2]=0;
        if ((ret=add_entry(&ent)) != PS_OK) return ret;
        SCRIPT_MSG(_T(" %") NPRIs,line.gettoken_str(i));
      }
      SCRIPT_MSG(_T("\n"));
      if (nounloadmisused)
        warning_fl(DW_PLUGIN_NOUNLOAD_PLACEMENT, _T("/NOUNLOAD must come first before any plugin parameter. Unless the plugin you are trying to use has a parameter /NOUNLOAD, you are doing something wrong"));

      // next, call it
      ent.which=EW_REGISTERDLL;
      ent.offsets[0]=add_string(tempDLL);
      ent.offsets[1]=add_string(funcname.c_str());
      ent.offsets[2]=0;
      ent.offsets[3]=nounload|build_plugin_unload;
      ent.offsets[4]=1;
      if ((ret=add_entry(&ent)) != PS_OK) return ret;

      DefineInnerLangString(NLF_SYMBOL_NOT_FOUND);
      DefineInnerLangString(NLF_COULD_NOT_LOAD);
      DefineInnerLangString(NLF_NO_OLE);
      // not used anywhere: DefineInnerLangString(NLF_ERR_REG_DLL);
      return PS_OK;
    }
    case TOK_INITPLUGINSDIR:
    {
      int ret;
      SCRIPT_MSG(_T("%") NPRIs _T("\n"),line.gettoken_str(0));
      if (uninstall_mode) uninst_plugin_used = true;
      else plugin_used = true;
      // Call [un.]Initialize_____Plugins
      ent.which=EW_CALL;
      ent.offsets[0]=ns_func.add(uninstall_mode?_T("un.Initialize_____Plugins"):_T("Initialize_____Plugins"),0);
      if ((ret=add_entry(&ent)) != PS_OK) return ret;
      // SetDetailsPrint lastused
      ret=add_entry_direct(EW_SETFLAG, FLAG_OFFSET(status_update), 0, 1);
      if (ret != PS_OK) return ret;
    }
    return PS_OK;
#else
    case TOK_PLUGINDIR:
    case TOK__PLUGINCOMMAND:
    case TOK_INITPLUGINSDIR:
      ERROR_MSG(_T("Error: %") NPRIs _T(" specified, NSIS_CONFIG_PLUGIN_SUPPORT not defined.\n"),line.gettoken_str(0));
    return PS_ERROR;
#endif //~ NSIS_CONFIG_PLUGIN_SUPPORT

    default:
      break;
  }
  ERROR_MSG(_T("Invalid command \"%") NPRIs _T("\"\n"),line.gettoken_str(0));
  return PS_ERROR;
}

#ifdef NSIS_SUPPORT_FILE
int CEXEBuild::do_add_file(const TCHAR *lgss, int attrib, int recurse, int *total_files, const TCHAR *name_override, int generatecode, int *data_handle, const set<tstring>& excluded, const tstring& basedir, bool dir_created)
{
  assert(!name_override || !recurse);

  tstring dir = get_dir_name(lgss), spec;

  if (dir == lgss)
    dir = _T("."), spec = lgss;
  else
    spec = tstring(lgss).substr(dir.length() + 1, tstring::npos);

  if (spec == _T("")) spec = _T("*");

  if (basedir == _T("")) {
    dir_created = true;
    if (recurse) {
      // save $OUTDIR into $_OUTDIR [StrCpy $_OUTDIR $OUTDIR]
      if (add_entry_direct(EW_ASSIGNVAR, m_UserVarNames.get(_T("_OUTDIR")), add_asciistring(_T("$OUTDIR"))) != PS_OK) {
        return PS_ERROR;
      }
    }
  }

  boost::scoped_ptr<dir_reader> dr( new_dir_reader() );
  dr->exclude(excluded);
  dr->read(dir);

  // add files in the current directory
  for (dir_reader::iterator files_itr = dr->files().begin();
       files_itr != dr->files().end();
       files_itr++)
  {
    if (!dir_reader::matches(*files_itr, spec))
      continue;

    if (!dir_created && generatecode) {
      SCRIPT_MSG(_T("%") NPRIs _T("File: Descending to: \"%") NPRIs _T("\"\n"), generatecode ? _T("") : _T("Reserve"), dir.c_str());

      if (do_add_file_create_dir(dir, basedir, attrib) != PS_OK) {
        return PS_ERROR;
      }

      dir_created = true;
    }

    if (add_file(dir, *files_itr, attrib, name_override, generatecode, data_handle) != PS_OK) {
      return PS_ERROR;
    }

    (*total_files)++;
  }

  if (!recurse) return PS_OK;
  // recurse into directories
  for (dir_reader::iterator dirs_itr = dr->dirs().begin();
       dirs_itr != dr->dirs().end();
       dirs_itr++)
  {
    tstring new_dir;
    bool created = false;

    if (basedir == _T(""))
      new_dir = *dirs_itr;
    else
      new_dir = basedir + _T('\\') + *dirs_itr;

    tstring new_spec = dir + PLATFORM_PATH_SEPARATOR_STR + *dirs_itr + PLATFORM_PATH_SEPARATOR_STR;

    if (!dir_reader::matches(*dirs_itr, spec)) {
      new_spec += spec;
    } else if (generatecode) {
      // always create directories that match

      SCRIPT_MSG(_T("%") NPRIs _T("File: Descending to: \"%") NPRIs _T("\"\n"), generatecode ? _T("") : _T("Reserve"), new_spec.c_str());

      if (do_add_file_create_dir(dir + _T('\\') + *dirs_itr, new_dir, attrib) != PS_OK) {
        return PS_ERROR;
      }

      created = true;
    }

    const TCHAR *new_spec_c = new_spec.c_str();
    int res = do_add_file(new_spec_c, attrib, 1, total_files, NULL, generatecode, NULL, excluded, new_dir, created);
    if (res != PS_OK) {
      return PS_ERROR;
    }
  }

  if (basedir == _T("")) {
    SCRIPT_MSG(_T("%") NPRIs _T("File: Returning to: \"%") NPRIs _T("\"\n"), generatecode ? _T("") : _T("Reserve"), dir.c_str());

    // restore $OUTDIR from $_OUTDIR [SetOutPath $_OUTDIR]
    if (add_entry_direct(EW_CREATEDIR, add_asciistring(_T("$_OUTDIR")), 1) != PS_OK) {
      return PS_ERROR;
    }
  }

  return PS_OK;
}

int CEXEBuild::add_file(const tstring& dir, const tstring& file, int attrib, const TCHAR *name_override, int generatecode, int *data_handle) {
  tstring newfn_s = dir + PLATFORM_PATH_SEPARATOR_C + file;
  const TCHAR *newfn = newfn_s.c_str();
  const TCHAR *filename = file.c_str();
  MMapFile mmap;
  UINT64 filesize;

#ifdef _WIN32
  HANDLE hFile = mmap.openfilehelper(newfn, filesize);
  if (hFile == INVALID_HANDLE_VALUE)
  {
    ERROR_MSG(_T("%") NPRIs _T("File: failed opening file \"%") NPRIs _T("\"\n"),generatecode?_T(""):_T("Reserve"),newfn);
    return PS_ERROR;
  }
  MANAGE_WITH(hFile, CloseHandle);
#else
  FILE *hFile = mmap.openfilehelper(newfn, filesize);
  if (!hFile)
  {
    ERROR_MSG(_T("%") NPRIs _T("File: failed opening file \"%") NPRIs _T("\"\n"),generatecode?_T(""):_T("Reserve"),newfn);
    return PS_ERROR;
  }
  MANAGE_WITH(hFile, fclose);
  const int fd = fileno(hFile);
#endif
  if (!mmap.setfile(hFile, filesize) && filesize)
  {
    ERROR_MSG(_T("%") NPRIs _T("File: failed creating mmap of \"%") NPRIs _T("\"\n"),generatecode?_T(""):_T("Reserve"),newfn);
    return PS_ERROR;
  }
  DWORD len = (DWORD) filesize;
  if (len != filesize) len = 0xffffffffUL - 1024; // truncate_cast but as large as possible

  if (generatecode&1)
    section_add_size_kb((len+1023)/1024);
  if (name_override) SCRIPT_MSG(_T("%") NPRIs _T("File: \"%") NPRIs _T("\"->\"%") NPRIs _T("\""),generatecode?_T(""):_T("Reserve"),filename,name_override);
  else SCRIPT_MSG(_T("%") NPRIs _T("File: \"%") NPRIs _T("\""),generatecode?_T(""):_T("Reserve"),filename);
  if (!build_compress_whole)
    if (build_compress) SCRIPT_MSG(_T(" [compress]"));
  fflush(stdout);
  TCHAR buf[1024];
  int last_build_datablock_used=getcurdbsize();
  entry ent={0,};
  if (generatecode)
  {
    ent.which=EW_EXTRACTFILE;

    DefineInnerLangString(NLF_SKIPPED);
    DefineInnerLangString(NLF_ERR_DECOMPRESSING);
    DefineInnerLangString(NLF_ERR_WRITING);
    DefineInnerLangString(NLF_EXTRACT);
    DefineInnerLangString(NLF_CANT_WRITE);

    ent.offsets[0]=build_overwrite;
    if (name_override)
    {
      ent.offsets[1]=add_string(name_override);
    }
    else
    {
      const TCHAR *i=filename;
      TCHAR *o=buf;
      while (*i)
      {
        const TCHAR c=*i++;
        *o++=c;
        if (c == _T('$')) *o++=_T('$'); // BUGBUG: We could overflow buf here
      }
      *o=0;
      ent.offsets[1]=add_string(buf);
    }
  }
  ent.offsets[2]=add_db_data(&mmap);

  mmap.clear();

  if (ent.offsets[2] < 0)
    return PS_ERROR;

  if (data_handle)
    *data_handle=ent.offsets[2];

  {
    DWORD s=getcurdbsize()-last_build_datablock_used;
    if (s) s-=4;
    if (s != len)
      SCRIPT_MSG(_T(" %u/%u bytes\n"),s,len);
    else
      SCRIPT_MSG(_T(" %u bytes\n"),len);
  }

  if (generatecode)
  {
    if (build_datesave || build_overwrite>=0x3 /*ifnewer or ifdiff*/)
    {
#ifdef _WIN32
      FILETIME ft;
      if (GetFileTime(hFile,NULL,NULL,&ft))
      {
        PULONGLONG fti = (PULONGLONG) &ft;
        *fti -= *fti % 20000000; // FAT write time has a resolution of 2 seconds
        ent.offsets[3]=ft.dwLowDateTime, ent.offsets[4]=ft.dwHighDateTime;
      }
#else
      struct stat st;
      if (!fstat(fd, &st))
      {
        unsigned long long ll = (st.st_mtime * 10000000LL) + 116444736000000000LL;
        ll -= ll % 20000000; // FAT write time has a resolution of 2 seconds
        ent.offsets[3] = (int) ll, ent.offsets[4] = (int) (ll >> 32);
      }
#endif
      else
      {
        ERROR_MSG(_T("%") NPRIs _T("File: failed getting file date from \"%") NPRIs _T("\"\n"),generatecode?_T(""):_T("Reserve"),newfn);
        return PS_ERROR;
      }
    }
    else
    {
      ent.offsets[3]=0xffffffff, ent.offsets[4]=0xffffffff;
    }

    // overwrite flag can be 0, 1, 2 or 3. in all cases, 2 bits
    int mb = 0;
    if (build_allowskipfiles)
    {
      mb = MB_ABORTRETRYIGNORE | MB_ICONSTOP;
      mb |= IDIGNORE << 21; // default for silent installers
    }
    else
    {
      mb = MB_RETRYCANCEL | MB_ICONSTOP;
      mb |= IDCANCEL << 21; // default for silent installers
    }
    ent.offsets[0] |= mb << 3;
    ent.offsets[5] = DefineInnerLangString(build_allowskipfiles ? NLF_FILE_ERROR : NLF_FILE_ERROR_NOIGNORE);
  }

  if (generatecode)
  {
    int a=add_entry(&ent);
    if (a != PS_OK)
      return a;
    if (attrib)
    {
#ifdef _WIN32
      ent.which=EW_SETFILEATTRIBUTES;
      // $OUTDIR is the working directory
      ent.offsets[0]=add_string(name_override?name_override:buf);
      ent.offsets[1]=GetFileAttributes(newfn);
      ent.offsets[2]=0;
      ent.offsets[3]=0;
      ent.offsets[4]=0;
      ent.offsets[5]=0;

      if (INVALID_FILE_ATTRIBUTES != (DWORD)ent.offsets[1])
      {
        a=add_entry(&ent);
        if (a != PS_OK)
          return a;
      }
#endif
    }
  }
  return PS_OK;
}

int CEXEBuild::do_add_file_create_dir(const tstring& local_dir, const tstring& dir, int attrib) {
  tstring outdir_s = _T("$_OUTDIR\\") + dir;

  tstring::size_type pos = 1;
  pos = outdir_s.find(_T('$'), pos);
  while (pos != tstring::npos) {
    outdir_s = outdir_s.insert(pos, _T("$"));
    pos = outdir_s.find(_T('$'), pos + 2);
  }

  int outdir = add_string(outdir_s.c_str());
  if (add_entry_direct(EW_CREATEDIR, outdir, 1) != PS_OK) {
    return PS_ERROR;
  }

#ifdef _WIN32
  if (attrib) {
    int ndc = add_asciistring(_T("."));
    DWORD attr = GetFileAttributes(local_dir.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
      if (add_entry_direct(EW_SETFILEATTRIBUTES, ndc, attr) != PS_OK)
      {
        return PS_ERROR;
      }
    }
  }
#endif
  return PS_OK;
}
#endif
