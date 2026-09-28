/*
 * build.h
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
 * Unicode support by Jim Park -- 08/10/2007
 */

#ifndef _BUILD_H_
#define _BUILD_H_

#include "strlist.h"
#include "lineparse.h"
#include "lang.h"
#include "ResourceEditor.h"
#include "ResourceVersionInfo.h"
#include "uservars.h"
#include "ShConstants.h"
#include "mmap.h"
#include "manifest.h"
#include "icon.h"
#include <memory.h>
#include "utf.h"

#include "exehead/fileform.h"
#include "exehead/config.h"

#include "tstring.h"
#include <set>
#include <map>

#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
// Added by Sunil Kamath 11 June 2003
#  include <time.h>
#  include <sys/stat.h>
#endif

#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
// Changed by Amir Szekely 31st July 2002
#include "compressor.h"
#include "czlib.h"
#include "cbzip2.h"
#include "clzma.h"
#include "czstd.h"
#endif //~ NSIS_CONFIG_COMPRESSION_SUPPORT

#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
#  include "Plugins.h"
#endif //~ NSIS_CONFIG_PLUGIN_SUPPORT

// 1000..4999 Errors
// 5000..5999 Important generic warnings
// 6000..6999 Script warnings
// 7000..7499 Recovered from bad input etc. warnings
// 7500..7999 Discouraged usage warnings (allocated high to low to reserve as much space as possible for more bad input codes)
// 8000..8999 Generic warnings
// 9000..9999 Breaking our and/or MS guidelines warnings
typedef enum {
  //DE_OOM = 1000?, // Generic out of memory
  //DE_MMAP = 1001?,
  //DE_STUB_SECTION = 1100?,
  //DE_PP_VERBOSE_BAD_LEVEL = 4000?,
  //DW_STALE_TEMP = 5020?, TODO: There is currently no way to disable this
  DW_PACKHDR_RETNONZERO = 5021,
  DW_BADFORMAT_EXTERNAL_FILE = 5040,
  DW_UNSUPP_STORE_FILE_ATT = 5050,
  //DW_CMDLINE_UNSUPP = 5200?,
  //DW_CMDLINE_UNSUPP_WIN = 5201?,
  DW_CMDLINE_UNSUPP_NIX = 5202,
  DW_CMDLINE_BAD_INPUTENC = 5210, // reserved ..5229
  DW_VAR_IGNORED_UNKNOWN = 6000, // reserved ..6009
  DW_VAR_NOREF = 6001,
  DW_UNUSED_FUNCTION = 6010, // reserved ..6019
  DW_UNUSED_GLOBALLABEL = 6011,
  DW_UNUSED_LABEL = 6012,
  DW_UNCODE_WITHOUT_UNEXE = 6020,
  DW_INNERSTRING_MULTISETWASTE = 6029,
  DW_STRING_MULTISETWASTE = DW_INNERSTRING_MULTISETWASTE,
  DW_LANGSTRING_MULTISETWASTE = 6030, // reserved ..6049
  DW_LANGSTRING_NOTSETINLANG = 6040,
  DW_LANGSTRING_SILENTLICENSE = 6049,
  DW_COMMENT_NEWLINE = 6050, // reserved ..6079
  DW_PLUGIN_NOUNLOAD_PLACEMENT = 6080, // reserved ..6099
  DW_PP_PRAGMA_UNKNOWN = 6100, // reserved ..6199
  DW_PP_PRAGMA_INVALID = 6101,
  DW_PP_DELFILE_DELERROR = 6149,
  DW_PP_DELFILE_NOMATCH = DW_PP_DELFILE_DELERROR,
  DW_PP_VERBOSE_POP_EMPTY_STACK = 6150,
  //DW_PP_VERBOSE_BAD_LEVEL = 6151?, // 2.x failed to issue a warning. 3.x currently aborts with hard error.
  DW_PP_UNDEF_UNDEFINED = 6155,
  DW_INCLUDE_NONFATAL_NOT_FOUND = 7000, // reserved ..7009
  DW_FILE_NONFATAL_NOT_FOUND = 7010, // reserved ..7019
  DW_LANGSTRING_OVERLONGLENGTH = 7020, // reserved ..7024
  DW_BAD_LANGID = 7025, // reserved ..7029
  DW_NLF_OLDVERSION = 7030, // reserved ..7049
  DW_NLF_SYSCP = 7031,
  DW_NLF_UNSUPPORTED_CP = 7032,
  DW_NLF_NOT_PREFERRED_ENC = 7033,
  DW_LICENSE_EMPTY = 7050,
  DW_ATTRIBUTE_OVERLONGSTRING = 7060,
  DW_PARSE_BADNUMBER = 7070,
  DW_PARSE_NUMBEROUTOFSPEC = 7071,
  DW_PARSE_LNK_HK = 7075,
  DW_GENERIC_DEPRECATED = 7998,
  DW_PARSE_REGPATHPREFIX = 7999,
  DW_INSTFILESPAGE_NOT_USED = 8000, // reserved ..8019
  DW_COMP_FINAL = 8020, // reserved ..8059
  DW_COMP_WHOLE_IGNORE_OFF = 8021,
  DW_COMP_LEVEL_IGNORE = 8025,
  DW_COMP_DICT_IGNORE = 8026,
  DW_COMP_DICTWHOLE = 8030,
  DW_CMDLINE_HIGHPRIORITY = 8499,
  DW_INSECURE_OUTFILENAME = 9000,
  DW_VI_MISSINGSTDKEY = 9100,

  DIAGCODE_INTERNAL_HIDEDIAGCODE = 9999,
  DIAGCODE_INTERNAL_FIRST = 1000, DIAGCODE_INTERNAL_LAST = 9999
} DIAGCODE;


#define PS_OK 0
#define PS_EOF 1
#define PS_ERROR 50
#define PS_WARNING 100

// token placement
#define TP_SEC    1
#define TP_FUNC   2
#define TP_CODE   (TP_SEC | TP_FUNC)
#define TP_GLOBAL 4
#define TP_PAGEEX 8
#define TP_PG     (TP_GLOBAL | TP_PAGEEX)
#define TP_ALL    (TP_CODE | TP_PG)

namespace MakensisAPI {
  extern const TCHAR* SigintEventNameFmt;
  extern const TCHAR* SigintEventNameLegacy;

  enum datatransfer_e {
    NOTIFY_SCRIPT, // Compiler -> Host: main nsi file(s)
    NOTIFY_WARNING,
    NOTIFY_ERROR,
    NOTIFY_OUTPUT, // Compiler -> Host: Generated .exe file
    PROMPT_FILEPATH // [0x03006000] Compiler -> Host -> Compiler
  };
#ifdef _WIN32
  enum sndmsg_e {
    QUERYHOST = WM_APP // [0x03000000] QUERYHOST_e in wParam. MUST return 0 for unknown QUERYHOST_e values!
  };
#endif
  enum QUERYHOST_e {
    QH_OUTPUTCHARSET = 1, // [0x03000000] return (wincodepage+1) or 0 for default (This encoding is used by stdout, stderr and the notify messages)
    QH_ENABLESTDERR, // [0x03001000] return 1 to output error messages to stderr or 0 to output error messages to stdout
    QH_SUPPORTEDVERSION, // [0x03006000] The host must return new highest makensis compiler version it supports
  };
  typedef struct {
    unsigned char Platform; // Bitness OR'ed with sizeof(TCHAR) of the compiler
    unsigned char Reserved;
    TCHAR Path[1];
  } PROMPT_FILEPATH_DATA;
}

#define PAGE_CUSTOM 0
#define PAGE_LICENSE 1
#define PAGE_COMPONENTS 2
#define PAGE_DIRECTORY 3
#define PAGE_INSTFILES 4
#define PAGE_UNINSTCONFIRM 5
#define PAGE_COMPLETED 6

#define FLAG_OFFSET(flag) (FIELD_OFFSET(exec_flags_t, flag)/sizeof(int))

class DiagState {
  template<class C, class K, class V> void insert_or_assign(C&c, const K&k, V val)
  {
    typename C::value_type item(k, val);
    std::pair<NSIS_CXX_TYPENAME C::iterator, bool> ret = c.insert(item);
    if (!ret.second) ret.first->second = val;
  }
  template<class C, class K> typename STL::mapped_type<C>::type get_paired_value(const C&c, const K&k, typename STL::mapped_type<C>::type defval) const
  {
    typename C::const_iterator it = c.find(k);
    return c.end() == it ? defval : it->second;
  }
  template<class T> int get_code_state(T t, int def) const { return get_paired_value(m_Warnings, static_cast<unsigned short>(t), def); }
public:
  typedef enum { wunspecified = -1, wdisabled = 0, wwarning, wenabled, werror } WARNSTATE;
  DiagState() : m_pStack(0), m_FallbackState(get_default_state()) { assert(DIAGCODE_INTERNAL_LAST <= 0xffff); }
  ~DiagState() { delete m_pStack; }
  static WARNSTATE get_default_state(DIAGCODE n = (DIAGCODE) 0) { return wenabled; } // All warnings currently default to enabled
  void def(DIAGCODE n) { insert_or_assign(m_Warnings, static_cast<unsigned short>(n), get_default_state(n)); }
  void enable(DIAGCODE n) { insert_or_assign(m_Warnings, static_cast<unsigned short>(n), wenabled); }
  void disable(DIAGCODE n) { insert_or_assign(m_Warnings, static_cast<unsigned short>(n), wdisabled); }
  void warning(DIAGCODE n) { insert_or_assign(m_Warnings, static_cast<unsigned short>(n), wwarning); } // Always !warning
  void error(DIAGCODE n) { insert_or_assign(m_Warnings, static_cast<unsigned short>(n), werror); } // Always !error
  bool is_disabled(DIAGCODE n) const { return get_code_state(n, m_FallbackState) == wdisabled; }
  bool is_error(DIAGCODE n) const { int s = get_code_state(n, m_FallbackState); return s == werror; }
  void push();
  bool pop();
  void set_all(WARNSTATE wm) { m_Warnings.clear(); m_FallbackState = wm; }
  void set_warning_as_error() { set_all(werror); }
  static bool is_valid_code(unsigned int n) { return n >= DIAGCODE_INTERNAL_FIRST && n <= DIAGCODE_INTERNAL_LAST; }
protected:
  DiagState *m_pStack;
  signed char m_FallbackState; // A fallback state so we don't have to fill the m_Warnings map with values for codes that are not explicitly set by the user
  std::map<unsigned short, signed char> m_Warnings;
};

class CEXEBuild {
  public:
    CEXEBuild(signed char pponly, bool warnaserror);
    void initialize(const TCHAR *makensis_path);
    ~CEXEBuild();

    enum {
      MAX_LINELENGTH = 16384, // NSI/NSH line limit, in TCHARs (including \0)
      MAX_MACRORECURSION = 50
    };
    static const TCHAR* get_commandlinecode_filename() { return _T("<command line>"); }
    static const TCHAR* get_memorycode_filename() { return _T("<memory>"); }

    void warning(DIAGCODE dc, const TCHAR *s, ...); // to add a warning to the compiler's warning list.
    void warning_fl(DIAGCODE dc, const TCHAR *s, ...); // warning with file name and line number
    void ERROR_MSG(const TCHAR *s, ...) const;
    void SCRIPT_MSG(const TCHAR *s, ...) const;
    void INFO_MSG(const TCHAR *s, ...) const;

    typedef enum {
      TARGETFIRST,
      TARGET_X86ANSI = TARGETFIRST,
      TARGET_X86UNICODE,
      TARGET_AMD64, // Always Unicode
      TARGET_ARM64, // Always Unicode
      TARGET_UNKNOWN,
      TARGETCOUNT = (TARGET_UNKNOWN-TARGETFIRST)
    } TARGETTYPE;
    TARGETTYPE m_target_type;
    TARGETTYPE get_target_type(const TCHAR*s) const;
    bool m_previous_x86_unicode;
    const TCHAR* get_target_suffix(CEXEBuild::TARGETTYPE tt, const TCHAR*defval = _T("?")) const;
    const TCHAR* get_target_suffix() const { return get_target_suffix(m_target_type); }
    static bool is_targettype_64bit(TARGETTYPE tt) { return TARGET_AMD64 == tt || TARGET_ARM64 == tt; }
    bool is_target_64bit() const { return is_targettype_64bit(m_target_type); }
    void print_bad_targettype_parameter(const TCHAR*cmdname, const TCHAR*prefix = _T("")) const;
    unsigned int get_header_size() const { return (unsigned int)sizeof(header) + (is_target_64bit() ? (4 * BLOCKS_NUM) : 0); }

    void set_default_output_filename(const tstring& filename);
    const TCHAR* get_output_filename() const { return build_output_filename; }

    // process a script (you can process as many scripts as you want,
    // it is as if they are concatenated)
    int process_script(NIStream&Strm, const TCHAR *filename);
    enum PROCESSLINEFLAGS { PLF_MACRO = 0x01, PLF_VIRTUALFILE = 0x02 };
    int process_oneline(const TCHAR *line, const TCHAR *curfilename, int lineptr, unsigned int plflags);
    
    // you only get to call write_output once, so use it wisely.
    int write_output(void);

    void print_help(const TCHAR *commandname=NULL);
    bool print_cmdhelp(const TCHAR *commandname, bool cmdhelp=false);

    DefineList definedlist; // List of identifiers marked as "defined" like
                            // C++ macro definitions such as _UNICODE.
    void define(const TCHAR *p, const TCHAR *v=_T("")); // to add a defined thing.
    signed char preprocessonly; // > 0 = safe, < 0 = unsafe

    int get_verbosity() const;
    void set_verbosity(int lvl);
    bool display_errors;
    bool display_script;
    bool display_warnings;
    bool display_info;

    int linecnt;
    const TCHAR *curfilename;
    NStreamLineReader* curlinereader;

    HWND notify_hwnd;
    void notify(MakensisAPI::datatransfer_e code, const TCHAR *data) const;
    #ifdef _WIN32
    typedef bool (CALLBACK*HOSTAPIREQUESTDATAPROC)(void*cookie, HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
    #else
    typedef bool (*HOSTAPIREQUESTDATAPROC)(void*cookie);
    #endif
    bool hostapi_request_data(MakensisAPI::datatransfer_e operation, UINT minver, HOSTAPIREQUESTDATAPROC proc, void*cookie, const void* input, size_t inputsize) const;
    bool prompt_for_output_path(TCHAR*path, UINT pathcap) const;

  private:
    int check_write_output_errors() const;
    int prepare_uninstaller();
    int pack_exe_header();

    int set_compressor(const tstring& compressor, const bool solid);
    int load_stub();
    int update_exehead(const tstring& file, size_t *size=NULL);
    void update_exehead(const unsigned char *new_exehead, size_t new_size);

    // tokens.cpp
    bool is_ppbranch_token(const TCHAR *s);
    bool is_pp_token(int tkid);
    bool is_unsafe_pp_token(int tkid);
    int get_commandtoken(const TCHAR *s, int *np, int *op, int *pos);
    const TCHAR* get_commandtoken_name(int tok);

    /**
     * Returns the current "state" by looking at whether it is in a
     * section/function/pagex or global.
     * @return TP_FUNC, TP_SEC, TP_PAGEEX, TP_GLOBAL.
     */
    int GetCurrentTokenPlace();
    int IsTokenPlacedRight(int pos, const TCHAR *tok);

    // script.cpp
#ifdef NSIS_SUPPORT_STANDARD_PREDEFINES
  // Added by Sunil Kamath 11 June 2003
    TCHAR* set_file_predefine(const TCHAR *);
    void restore_file_predefine(TCHAR *);
    TCHAR* set_timestamp_predefine(const TCHAR *);
    void restore_timestamp_predefine(TCHAR *);
    TCHAR* set_line_predefine(int, BOOL);
    void restore_line_predefine(TCHAR *);
    void set_date_time_predefines();
    void del_date_time_predefines();
#endif
    int parseScript();
    int includeScript(const TCHAR *f, NStreamEncoding&enc);
    int includeScriptLines(const TCHAR *start, const TCHAR *end, const TCHAR *name);
    TCHAR* GetMacro(const TCHAR *macroname, TCHAR**macroend = 0);
    TCHAR* GetMacro(size_t idx);
    bool MacroExists(const TCHAR *macroname) { return !!GetMacro(macroname); }
    LANGID ParseLangIdParameter(const LineParser&line, int token);
    int LoadLicenseFile(const TCHAR *file, TCHAR** pdata, const TCHAR *cmdname, WORD AnsiCP);
#ifdef NSIS_FIX_DEFINES_IN_STRINGS
    void ps_addtoline(const TCHAR *str, GrowBuf &linedata, StringList &hist, bool bIgnoreDefines = false);
#else
    void ps_addtoline(const TCHAR *str, GrowBuf &linedata, StringList &hist);
#endif
    int doParse(int verbosity, const TCHAR *fmt, ...);
    int doParse(const TCHAR *str);
    int doCommand(int which_token, LineParser &line);
    TCHAR m_templinebuf[MAX_LINELENGTH]; // Buffer used by parseScript() & doCommand(), not recursion safe!
    int add_flag_instruction_entry(int which_token, int opcode, LineParser &line, int offset, int data = 0);

    int do_add_file(const TCHAR *lgss, int attrib, int recurse, int *total_files, const TCHAR 
      *name_override=0, int generatecode=1, int *data_handle=0, 
      const std::set<tstring>& excluded=std::set<tstring>(), 
      const tstring& basedir=tstring(_T("")), bool dir_created=false);
    int add_file(const tstring& dir, const tstring& file, int attrib, const TCHAR 
      *name_override, int generatecode, int *data_handle);
    int do_add_file_create_dir(const tstring& local_dir, const tstring& dir, int attrib=0);

    GrowBuf m_linebuild; // used for concatenating lines

    // used by doParse to do preprocessing
    struct ifblock
    {
      int hasexeced;
      int elseused;
      int ignore;
      int inherited_ignore;
    } *cur_ifblock;

    TinyGrowBuf build_preprocessor_data;

    void start_ifblock();
    void end_ifblock();
    int num_ifblock();

    int last_line_had_slash;
    bool inside_comment;
    int multiple_entries_instruction;  // 1 (true) or 0 (false)

    int pp_boolifyexpression(LineParser&line, int &result, bool allow_logicneg = true, int ignore_last_tokens = 0);
    int pp_assert(LineParser&line);
    int pp_macro(LineParser&line);
    int pp_macroundef(LineParser&line);
    int pp_insertmacro(LineParser&line);
    int pp_tempfile(LineParser&line);
    int pp_delfile(LineParser&line);
    int pp_appendfile(LineParser&line);
    int pp_appendmemfile(LineParser&line);
    int pp_getversionhelper(const TCHAR *cmdname, const TCHAR *path, const TCHAR *basesymname, DWORD high, DWORD low, DWORD flags);
    int pp_getversion(int which_token, LineParser&line);
    int pp_searchreplacestring(LineParser&line);
    int pp_searchparsestring(LineParser&line);
    DefineList *searchParseString(const TCHAR *source_string, LineParser&line, int parmOffs, bool ignCase, bool noErrors, UINT*failParam = 0);
    int pp_verbose(LineParser&line);
    int pp_define(LineParser&line);
    int pp_undef(LineParser&line);
    int pp_packhdr(LineParser&line);
    int pp_finalize(int which_token, LineParser&line);
    int pp_execute(int which_token, LineParser&line);
    int pp_addincludedir(LineParser&line);
    int pp_include(LineParser&line);
    int pp_cd(LineParser&line);
    int pp_pragma(LineParser&line);

    // build.cpp functions used mostly by script.cpp
    int set_target_architecture_data();
    int change_target_architecture(TARGETTYPE tt);
    void set_code_type_predefines(const TCHAR *value = NULL);
    int getcurdbsize();
    int add_section(const TCHAR *secname, const TCHAR *defname, int expand=0);
    int section_end();
    int add_function(const TCHAR *funname);
    int function_end();
    void section_add_size_kb(int kb);
    int section_add_flags(int flags);
    int section_add_install_type(int inst_type);
    int add_page(int type);
    int page_end();
    int add_label(const TCHAR *name);
    int add_entry(const entry *ent);
    int add_entry_direct(int which, int o0=0, int o1=0, int o2=0, int o3=0, int o4=0, int o5=0);
    int add_db_data(IMMap *map); // returns offset
    int add_db_data(const char *data, int length); // returns offset
    int add_db_data(const char *data, size_t length) { assert(length <= 0x7FFFFFFF); return add_db_data(data, (int)length); }
    int add_data(const char *data, int length, IGrowBuf *dblock); // returns offset
    int add_string(const TCHAR *string, int process=1, UINT codepage=-2); // returns offset (in string table)
    int add_asciistring(const TCHAR *string, int process=1); // For hardcoded 7bit/ASCII strings
    int add_intstring(const int i); // returns offset in stringblock

    int preprocess_string(TCHAR *out, const TCHAR *in, WORD codepage=CP_ACP);
    void init_shellconstantvalues();
    int parse_pragma(LineParser &line);

#ifdef NSIS_CONFIG_PLUGIN_SUPPORT
    int add_plugins_dir_initializer(void);
    int initialize_default_plugins(bool newtargetarc = false);
    Plugins m_plugins[TARGETCOUNT], *m_pPlugins;
    bool plugin_used;
    bool uninst_plugin_used;
    int build_plugin_unload; // TOK_SETPLUGINUNLOAD
#endif //NSIS_CONFIG_PLUGIN_SUPPORT

    // build.cpp functions used mostly within build.cpp
    int datablock_optimize(int start_offset, int first_int);
    bool datablock_finddata(IMMap&mmap, int mmstart, int size, int*ofs);
    void printline(int l);
    int process_jump(LineParser &line, int wt, int *offs);

    int AddVersionInfo();
    int ProcessPages();
    void PrepareInstTypes();
    void AddStandardStrings();
    void PrepareHeaders(IGrowBuf *hdrbuf);
    int SetVarsSection();
    int SetManifest();
    int UpdatePEHeader();

    int resolve_jump_int(const TCHAR *fn, int *a, int offs, int start, int end);
    int resolve_call_int(const TCHAR *fn, const TCHAR *str, int fptr, int *ofs);
    int resolve_instruction(const TCHAR *fn, const TCHAR *str, entry *w, int offs, int start, int end);

    int resolve_coderefs(const TCHAR *str);
    int uninstall_generate();

    void print_warnings();
    void warninghelper(DIAGCODE dc, bool fl, const TCHAR *fmt, va_list args);
    DiagState diagstate;
    bool changed_target;

    /** Are we defining an uninstall version of the code?
     * @param un Use like a boolean to define whether in uninstall mode.
     */
    void set_uninstall_mode(int un);

    // lang.cpp functions and variables
    void InitLangTables();

    /**
     * This function gets a LanguageTable structure for the specified language
     * via LANGID.  If create == true, it will create a new LanguageTable if
     * the appropriate one cannot be found.  If lang is LANG_NEUTRAL (0), then
     * it will get the LanguageTable of the last used language or more
     * correctly, the last Language ID that generated a valid return value
     * (not NULL).
     *
     * @param lang [in/out] Language ID reference.  If LANG_NEUTRAL, it gets
     * set to thelast used language ID.
     * @param create Create a new LanguageTable?  Default = true.
     * @return Appropriate LanguageTable* if exists, otherwise NULL.
     */
    LanguageTable *GetLangTable(LANGID &lang, bool create = true);

    /**
      * Get the language name as a TCHAR* and the code page value via an
      * out parameter.  It will look for a LanguageTable to get the values.
      * If not found, then it will set the codepage to English for ANSI
      * or Unicode for Unicode version of NSIS.  The language name is looked
      * up via the LanguageTable if it exists, otherwise, it returns "???" except 
      * a hardcoded check for 1033 (english). It really should fall back to 
      * calling GetLocaleInfo() with the LANGID to get the string.
      *
      * This function is not thread-safe!  For a thread-safe version, the
      * parameter must include the buffer to write to.
      *
      * @param lang The language ID
      * @param codepage [out] The code page referred to by the language ID.
      * @return The language string in English.
      */
    const TCHAR *GetLangNameAndCP(LANGID lang, unsigned int *codepage = NULL);
    const TCHAR *GetLangNameAndCPForVersionResource(LANGID &lang, unsigned int *codepage = NULL, bool deflangfallback = true);

    int DefineLangString(const TCHAR *name, int process=-1);
    int DefineInnerLangString(int id, int process=-1);

    /**
     * A LangString is a string variable that varies in value depending on what
     * language is being used.  This function sets the string value for the
     * variable 'name' for a given language ID.
     * 
     * @return If the language id, the variable name or string is invalid, it will
     * return a PS_ERROR.  If this function call is overwriting a set user string,
     * this will return a PS_WARNING.
     */
    int SetLangString(const TCHAR *name, LANGID lang, const TCHAR *str, BOOL LicenseData);
    int SetLangString(const TCHAR *name, LANGID lang, const TCHAR *str);

    /**
     * Sets the user string to the specific NLF_STRINGS id.
     *
     * @return If the id is invalid or the string is not valid, it will return
     * a PS_ERROR.  If this function call is overwriting a set user string,
     * this will return a PS_WARNING.
     */
    int SetInnerString(int id, const TCHAR *str);

    int GenerateLangTable(LanguageTable *lt, int num_lang_tables);
    int GenerateLangTables();
    void FillLanguageTable(LanguageTable *table);
    int HasUserDefined(int id) {
      const TCHAR *us = UserInnerStrings.get(id);
      return us && *us;
    };

    LanguageTable *LoadLangFile(TCHAR *filename);
    void DeleteLangTable(LanguageTable *table);

    NLFRef NLFRefs[NLF_STRINGS];
    bool keep_ref;
    StringsArray UserInnerStrings;
    bool defcodepage_set;
    GrowBuf lang_tables;
    LANGID last_used_lang;
    LangStringList build_langstrings;

    int build_langstring_num, ubuild_langstring_num;
    TCHAR build_font[1024];
    int build_font_size;

    unsigned int uDefCodePage;

    // pages stuff
    int license_res_id;
    page *cur_page;  // Current page we are defining, NULL if not.
    int cur_page_type;
    int enable_last_page_cancel, uenable_last_page_cancel;

    int disable_window_icon;

    // User variables stuff
    int GetUnsafeUserVarIndex(LineParser &line, int token);
    int GetUserVarIndex(LineParser &line, int token);
    // Added by ramon 3 jun 2003
    UserVarsStringList m_UserVarNames;
    int m_iBaseVarsNum;
    int DeclaredUserVar(const TCHAR *VarName);
    void VerifyDeclaredUserVarRefs(UserVarsStringList *pVarsStringList);
    bool IsIntOrUserVar(const LineParser &line, int token) const;
    static bool IsVarPrefix(const TCHAR*s) { return *s++ == _T('$') && *s > ' '; }

    ConstantsStringList m_ShellConstants;

    // a whole bunch O data.

    tstring stubs_dir;
    tstring stub_filename;

#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
    ICompressor *compressor;
    CZlib zlib_compressor;
    CBzip2 bzip2_compressor;
    CLZMA lzma_compressor;
    CZstd zstd_compressor;
#endif
    bool build_compressor_set;
    bool build_compressor_final;
    bool build_compress_whole;
    int build_compress;
    int build_compress_level;
    int build_compress_dict_size;

    bool no_space_texts;
    bool build_unicode;// generate installer with unicode exehead?
    bool build_lockedunicodetarget;
    class writer_target_info mk_writer_target_info();

    bool has_called_write_output;

    struct postbuild_cmd
    {
      struct postbuild_cmd*next;
      int cmpop, cmpval;
      TCHAR cmd[1];
      void delete_all();
      static postbuild_cmd* make(const TCHAR *cmdstr, int cmpop, int cmpval);
    } *postbuild_cmds, *postubuild_cmds;
    int run_postbuild_cmds(const postbuild_cmd *cmds, const TCHAR *templatearg_pc1, const TCHAR* commandname);
    int check_external_exitcode(int exitcode, int op, int val);

    TCHAR build_packname[1024], build_packcmd[1024];
    int build_overwrite, build_last_overwrite, build_crcchk,
        build_datesave, build_optimize_datablock,
        build_allowskipfiles; // Added by ramon 23 May 2003

    header build_header, build_uninst, *cur_header;
    int uninstall_mode; // Are we in uninstall mode?  Acts like a bool.
    UINT32 uninstall_size,uninstall_size_full;
    int uninstaller_writes_used;

    TCHAR build_output_filename[1024];

    int build_include_depth;

    // Added by ramon 6 jun 2003
#ifdef NSIS_SUPPORT_VERSION_INFO
    CResourceVersionInfo rVersionInfo;
    unsigned int version_fixedflags;
#endif

    int sectiongroup_open_cnt;
    FastStringList m_warnings;
    const TCHAR* m_currentmacroname;
    GrowBuf m_macros;

    UINT64 db_opt_save, db_opt_save_u, db_full_size, db_full_size_u;
    int db_comp_save, db_comp_save_u;

    FastStringList include_dirs;

    StringList ns_func; // function namespace
    StringList ns_label; // label namespace

    int build_cursection_isfunc; // Are we in the middle of func definition?
    section *build_cursection;   // The section we are defining, NULL if not in section.
                                 // This could be a function or a section.

    // The ubuild prefixed objects / variables are for the uninstall versions
    // of the code.  The cur prefix objects are what the current objects that
    // need to be referenced should be.  What is pointed to by the cur* objects
    // are determined by whether or not we are in uninstall mode or not.
    TinyGrowBuf build_sections, ubuild_sections, *cur_sections;
    GrowBuf build_entries,ubuild_entries, *cur_entries;
    GrowBuf build_instruction_entry_map,ubuild_instruction_entry_map, *cur_instruction_entry_map;
    TinyGrowBuf build_functions, ubuild_functions, *cur_functions;
    TinyGrowBuf build_labels, ubuild_labels, *cur_labels;
    ExeHeadStringList build_strlist, ubuild_strlist, *cur_strlist;
    GrowBuf build_langtables, ubuild_langtables, *cur_langtables;
    TinyGrowBuf build_pages, ubuild_pages, *cur_pages;
    TinyGrowBuf build_ctlcolors, ubuild_ctlcolors, *cur_ctlcolors;

    // don't forget to update the cache after updating the datablock
    // see datablock_optimize for an example
    MMapBuf build_datablock, ubuild_datablock;
    TinyGrowBuf build_datablock_cache, ubuild_datablock_cache;
    IGrowBuf *cur_datablock, *cur_datablock_cache;
    struct cached_db_size
    {
      int first_int; // size | (compressed ? 0x80000000 : 0)
      int start_offset;
    };

    int build_filebuflen;

    TinyGrowBuf verbose_stack;

    unsigned char *m_exehead;
    size_t m_exehead_size;
    size_t m_exehead_original_size;

    bool branding_image_found;
    WORD branding_image_id;

    IconGroup installer_icon;
    IconGroup uninstaller_icon;
    size_t m_unicon_size;

#ifdef NSIS_SUPPORT_BGBG
    LOGFONT bg_font;
    LOGFONT bg_default_font;
#endif

#ifdef NSIS_CONFIG_COMPRESSION_SUPPORT
    int deflateToFile(FILE *fp, char *buf, int len); // len==0 to flush
#endif

    WORD PEDllCharacteristics, PESubsysVerMaj, PESubsysVerMin;
    unsigned int manifest_flags;
    manifest::comctl manifest_comctl;
    manifest::exec_level manifest_exec_level;
    manifest::dpiaware manifest_dpiaware;
    tstring manifest_dpiawareness;
    manifest::longpathaware manifest_lpaware;
    manifest::SupportedOSList manifest_sosl;
    tstring manifest_maxversiontested;

    CResourceEditor *res_editor;
    void init_res_editor();
    void close_res_editor();
};

#endif //_BUILD_H_
