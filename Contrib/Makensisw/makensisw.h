/*
  Copyright (c) 2002 Robert Rainwater
  Contributors: Justin Frankel, Fritz Elfert, Amir Szekely, Sunil Kamath, Joost Verburg

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/
#ifndef MAKENSIS_H
#define MAKENSIS_H

#define _WIN32_IE 0x0400
#include "../../Source/Platform.h"
#include <windows.h>
#include <commctrl.h>
#include "utils.h"
#define _RICHEDIT_VER 0x0200
#include <richedit.h>
#undef _RICHEDIT_VER



#define DpiAwarePerMonitor2() ( FALSE ) // Not yet
#define DpiAwarePerMonitor() ( FALSE )
#define SupportsWNT4() ( sizeof(void*) == 4 && !DpiAwarePerMonitor() ) // NT4 does not support the MultiMon API
#define SupportsW9X() ( sizeof(TCHAR) == 1 )
#define SupportsW95() ( FALSE && SupportsW9X() && !DpiAwarePerMonitor() )
#define SupportsW2000() ( sizeof(void*) == 4 )
#define SupportsRTLUI() ( FALSE ) // UI is English only

static inline bool IsWin9598ME() { return SupportsW9X() && (int) GetVersion() < 0; }
static inline bool IsWin95() { return SupportsW95() && (GetVersion() & (0x8000FFFF & ~0x0300)) == 0x80000004; }

// Defines
#define NSIS_URL     "https://nsis.sourceforge.io/"
#define NSIS_FORUM_URL "https://forums.winamp.com/forum/developer-center/nsis-discussion"
#define NSIS_UC_URL  "https://nsis.sourceforge.io/update.php?version="
#define NSIS_DL_URL  "https://nsis.sourceforge.io/download/"
#ifdef UNICODE
#define USAGE        _T("Usage:\r\n\r\n \x2022 File \x203a Load Script...\r\n \x2022 Drag the .nsi file into this window\r\n \x2022 Right click the .nsi file and choose \"Compile NSIS Script\"\r\n")
#else
#define USAGE        _T("Usage:\r\n\r\n - File | Load Script...\r\n - Drag the .nsi file into this window\r\n - Right click the .nsi file and choose \"Compile NSIS Script\"\r\n")
#endif
#define COPYRIGHT    _T("Copyright (C) 2002 Robert Rainwater")
#define CONTRIB      _T("Fritz Elfert, Justin Frankel, Amir Szekely, Sunil Kamath, Joost Verburg, Anders Kjersem")
#define DONATE       _T("<A>Donate</A> to support NSIS development.")
#define DONATEURL    { 'h'|x,'t','t'|x,'p',':'|x,'/'|x,'/'|x,'n'|x,'s'|x,'i'|x,'s'|x,'.'|x,'s'|x,'f'|x,'.','n'|x,'e'|x,'t'|x,'/'|x,'r','/'|x,'D'|x,'o'|x,'n'|x,'a'|x,'t'|x,'e'|x,'\0' } // "Encrypted"
#define DOCURL       "https://nsis.sourceforge.io/Docs/"
#define LOCALDOCS    _T("\\NSIS.chm")
#define ERRBOXTITLE  0 //_T("Error")
#define NSISERROR    _T("Unable to initialize MakeNSIS. Please verify that makensis.exe is in the same directory as makensisw.exe.")
#define DLGERROR     _T("Unable to initialize MakeNSISW.")
#define SYMBOLSERROR _T("Symbol cannot contain whitespace characters")
#define MULTIDROPERROR _T("Dropping more than one script at a time is not supported")
#define NSISUPDATEPROMPT _T("Running NSIS Update will close MakeNSISW.\nContinue?")
#define REGSEC       HKEY_CURRENT_USER
#define REGSECDEF    HKEY_LOCAL_MACHINE
#define REGKEY       _T("Software\\NSIS")
#define REGLOC       _T("MakeNSISWPlacement")
#define REGVERBOSITY _T("MakeNSISWVerbosity")
#define REGCOMPRESSOR _T("MakeNSISWCompressor")
#define REGCOLORIZE  _T("MakeNSISWColorize")
#define REGSYMSUBKEY _T("Symbols")
#define REGMRUSUBKEY _T("MRU")
#define EXENAME      _T("makensis.exe")
#define MAX_STRING   256
#define TIMEOUT      100
#define MINWIDTH     400
#define MINHEIGHT    220
#define COMPRESSOR_MESSAGE _T("\n\nThe %s compressor created the smallest installer (%d bytes).")
#define RESTORED_COMPRESSOR_MESSAGE _T("\n\nThe %s compressor created the smallest installer (%d bytes).")
#define EXE_HEADER_COMPRESSOR_STAT _T("EXE header size:")
#define TOTAL_SIZE_COMPRESSOR_STAT _T("Total size:")
#define LOAD_SYMBOL_SET_DLG_NAME _T("Load Symbol Definitions Set")
#define SAVE_SYMBOL_SET_DLG_NAME _T("Save Symbol Definitions Set")
#define LOAD_BUTTON_TEXT _T("Load")
#define SAVE_BUTTON_TEXT _T("Save")
#define LOAD_SYMBOL_SET_MESSAGE _T("Please select a name for the Symbol Definitions Set to load.")
#define SAVE_SYMBOL_SET_MESSAGE _T("Please enter or select a name for the Symbol Definitions Set to save.")
#define TESTBTN_TIPTEXT _T("Test the generated installer")
#define CLOSEBTN_TIPTEXT _T("Close MakeNSISW")

#define WM_MAKENSIS_PROCESSCOMPLETE (WM_USER+1001)
#define WM_MAKENSIS_LOADSYMBOLSET (WM_USER+1002)
#define WM_MAKENSIS_SAVESYMBOLSET (WM_USER+1003)
#define WM_MAKENSIS_UPDATEUISTATE (WM_USER+1004)
#define WM_MAKENSIS_FREEZEEDITOR (WM_USER+1005)
#define TID_CONFIGURECLOSEORABORT 1

namespace MakensisAPI {
  extern const TCHAR* SigintEventNameFmt;
  extern const TCHAR* SigintEventNameLegacy;

  enum datatransfer_e {
    NOTIFY_SCRIPT,
    NOTIFY_WARNING,
    NOTIFY_ERROR,
    NOTIFY_OUTPUT,
    PROMPT_FILEPATH
  };
  enum sndmsg_e {
    QUERYHOST = WM_APP
  };
  enum QUERYHOST_e {
    QH_OUTPUTCHARSET = 1,
    QH_ENABLESTDERR,
    QH_SUPPORTEDVERSION
  };
  typedef struct {
    unsigned char Platform;
    unsigned char Reserved;
    TCHAR Path[1];
  } PROMPT_FILEPATH_DATA;
}

typedef enum {
  COMPRESSOR_NONE_SELECTED = -1,
  COMPRESSOR_SCRIPT = 0,
  COMPRESSOR_ZLIB,
  COMPRESSOR_ZLIB_SOLID,
  COMPRESSOR_BZIP2,
  COMPRESSOR_BZIP2_SOLID,
  COMPRESSOR_ZSTD,
  COMPRESSOR_ZSTD_SOLID,
  COMPRESSOR_LZMA,
  COMPRESSOR_LZMA_SOLID,
  COMPRESSOR_BEST,
} NCOMPRESSOR;

#ifdef MAKENSISW_CPP
const TCHAR *compressor_names[] = {_T(""),
                            _T("zlib"),
                            _T("/SOLID zlib"),
                            _T("bzip2"),
                            _T("/SOLID bzip2"),
                            _T("zstd"),
                            _T("/SOLID zstd"),
                            _T("lzma"),
                            _T("/SOLID lzma"),
                            _T("Best")};
const TCHAR *compressor_display_names[] = {_T("Defined in Script/Compiler Default"),
                            _T("ZLIB"),
                            _T("ZLIB (solid)"),
                            _T("BZIP2"),
                            _T("BZIP2 (solid)"),
                            _T("ZSTD"),
                            _T("ZSTD (solid)"),
                            _T("LZMA"),
                            _T("LZMA (solid)"),
                            _T("Best Compressor")};
const WORD compressor_commands[] = {IDM_COMPRESSOR_SCRIPT,
                              IDM_ZLIB,
                              IDM_ZLIB_SOLID,
                              IDM_BZIP2,
                              IDM_BZIP2_SOLID,
                              IDM_ZSTD,
                              IDM_ZSTD_SOLID,
                              IDM_LZMA,
                              IDM_LZMA_SOLID,
                              IDM_BEST};
#endif

#ifdef TOOLBAR_CPP
int compressor_bitmaps[] = {IDB_COMPRESSOR_SCRIPT,
                            IDB_COMPRESSOR_ZLIB,
                            IDB_COMPRESSOR_ZLIB,
                            IDB_COMPRESSOR_BZIP2,
                            IDB_COMPRESSOR_BZIP2,
                            IDB_COMPRESSOR_ZSTD,
                            IDB_COMPRESSOR_ZSTD,
                            IDB_COMPRESSOR_LZMA,
                            IDB_COMPRESSOR_LZMA,
                            IDB_COMPRESSOR_BEST};
int compressor_strings[] = {IDS_SCRIPT,
                            IDS_ZLIB,
                            IDS_ZLIB_SOLID,
                            IDS_BZIP2,
                            IDS_BZIP2_SOLID,
                            IDS_ZSTD,
                            IDS_ZSTD_SOLID,
                            IDS_LZMA,
                            IDS_LZMA_SOLID,
                            IDS_BEST};
#endif

// Extern Variables

extern const TCHAR* NSISW_VERSION;
extern void* g_ModalDlgData;

DWORD WINAPI MakeNSISProc(LPVOID TreadParam);
INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct {
  enum { TID_HEADER = 1 };
  HFONT hHeaderFont, hFont, hBoldFont;
  INT AnimPos, AnimDir;
  UINT AnimSpeed, FinalHeaderPos;
} ABOUTDLGDATA;
INT_PTR ShowAboutDialog(HWND hwndOwner);

INT_PTR CALLBACK SettingsProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
typedef struct {
  void*pOldMDD;
  BOOL LoadingMode;
} SYMSETDLGDATA;
INT_PTR ShowSymbolSetDialog(HWND hwndOwner, BOOL LoadingSet);

INT_PTR CALLBACK CompressorProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void           SetScript(const TCHAR *script, bool clearArgs = true);
void           CompileNSISScript();
TCHAR*         BuildSymbols();
void           SetCompressor(NCOMPRESSOR);
void           RestoreSymbols();
void           SaveSymbols();
void           FreeSymbolSet(TCHAR **);
void           DeleteSymbolSet(const TCHAR *);
TCHAR**        LoadSymbolSet(const TCHAR *);
void           SaveSymbolSet(const TCHAR *, TCHAR **);
void           RestoreMRUList();
void           SaveMRUList();

typedef struct NSISScriptData {
  TCHAR *script;
  HGLOBAL script_cmd_args;
  TCHAR *compile_command;
  TCHAR *output_exe;
  TCHAR *input_script;
  TCHAR *branding;
  char  *brandingv;
  TCHAR **symbols;
  int retcode;
  unsigned char verbosity;
  DWORD logLength;
  DWORD warnings;
  HINSTANCE hInstance;
  HWND hwnd;
  HWND logwnd;
  HMENU menu;
  HMENU fileSubmenu;
  HMENU editSubmenu;
  HMENU toolsSubmenu;
  HANDLE thread;
  HANDLE sigint_event;
  HANDLE sigint_event_legacy;
  HWND focused_hwnd;
  void*pLogTextDoc;
  NCOMPRESSOR default_compressor;
  NCOMPRESSOR compressor;
  LPCTSTR compressor_name;
  TCHAR compressor_stats[512];
  LPCTSTR best_compressor_name;
  int recompile_test;
  WORD log_zoom;
} NSCRIPTDATA;

extern NSCRIPTDATA g_sdata;

typedef struct ResizeData {
  RECT resizeRect;
  RECT griprect;
  int dx;
  int dy;
  UINT bottompanelsize;
} NRESIZEDATA;

typedef struct FindReplaceDialog {
  FINDREPLACE fr;
  UINT uFindReplaceMsg;
  HWND hwndFind;
} NFINDREPLACE;

typedef struct ToolTipStruct {
  HWND tip;
  HWND tip_p;
  HHOOK hook;
} NTOOLTIP;


extern INT_PTR ShowWndSpy(HWND hOwner);
extern INT_PTR ShowLookupDialog(HWND hOwner);

#endif
