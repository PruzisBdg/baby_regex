/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex for embedded stuff.
|
| This is the ca. 1980 Thompson non-back-tracking virtual-machine regex, found
| in the Plan9 library and early greps.
|
| The program compiles the search expression into a non-deterministic finite
| automaton (NFA) and executes the multiple paths through the NFA on the input in
| lockstep. A match is the longest of those paths which first exhaust the regex.
| (more than one path may exhaust the regex simultaneously)
|
| So there's no backtracking; any path which fails on the current input char is
| terminated while remaining paths continue (to the next char). No backtracking
| eliminates exponential blowup with multiple alternates.
|
| Thompson's insight was that the width of the execution tree (the number of
| simultaneous threads) can never be more than than number of compiled regex
| operators. This because any operator can produce at most one additional
| branch/thread AND because a regex, by definition, can be executed on a DFA.
| So once the regex is compiled, the amount of memory needed to run it is
| known/bounded.
|
| Corner cases:
|  - Empty string is no match.
|
|
|  Public:
|     RegexLT_Init()
|     RegexLT_Compile()
|     RegexLT_MatchProg()
|     RegexLT_Match()
|     RegexLT_Replace()
|     RegexLT_ReplaceProg()
|     RegexLT_PrintMatchList()
|     RegexLT_PrintMatchList_OnOneLine()
|     RegexLT_FreeMatches()
|     RegexLT_RtnStr()
|
--------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "libs_support.h"
#include "arith.h"
#include "util.h"
#include "regexlt_private.h"

#define _PrintableCharMax 0x7E   // Tilde
#define _PrintableCharMin 0x20

#define _C8bag_AddAll(m) C8bag_AddRange((m), _PrintableCharMin, _PrintableCharMax)

// Private to RegexLT_'.
#define dbgPrint           regexlt_dbgPrint
#define classParser_Init   regexlt_classParser_Init
#define classParser_AddDef regexlt_classParser_AddDef
#define classParser_AddCh  regexlt_classParser_AddCh
#define getCharClassByKey  regexlt_getCharClassByKey
#define parseRepeat        regexlt_parseRepeat
#define compileRegex       regexlt_compileRegex
#define runCompiledRegex   regexlt_runCompiledRegex
#define printProgram       regexlt_printProgram
#define getMemMultiple     regexlt_getMemMultiple
#define safeFree           regexlt_safeFree
#define safeFreeList       regexlt_safeFreeList


#define TAB 0x09
#define CR  0x0D
#define LF  0x0A

// User must configures regex with RegexLT_Init().
PUBLIC RegexLT_S_Cfg const *regexlt_cfg = NULL;

/* ----------------------------- newMatchList ------------------------------ */


PUBLIC RegexLT_S_MatchList * newMatchList(U8 len)
{
   RegexLT_S_Match *ms;
   RegexLT_S_MatchList *ml;

   S_TryMalloc toMalloc[] = {                         // Memory which the compiler needs
      { (void**)&ms,    (U16)len * sizeof(RegexLT_S_Match)  },
      { (void**)&ml,    sizeof(RegexLT_S_MatchList)         } };

   if( getMemMultiple(toMalloc, RECORDS_IN(toMalloc)) == FALSE)   // Oops!?
   {
      return NULL;                   // Some malloc() error.
   }
   else
   {
      ml->matches = ms;
      ml->listSize = len;
      ml->put = 0;
      return ml;
   }
}

/* ---------------------------------------------- inputOK --------------------------------------

   A sfae check of the length of ;inStr'; mus not be longer than 'maxLen'.
*/
typedef struct { BOOL ok; U16 len; } S_StrChkRtns;

PRIVATE S_StrChkRtns inputOK(C8 const *inStr, U16 maxLen)
{
   for(U16 c = 0; c <= maxLen; c++, inStr++) {                 // Thru 'inStr' up to 'maxLen'
      if(*inStr == '\0') {                                     // Found end?
         S_StrChkRtns r0 = {.ok = TRUE, .len = c};             // then 'inStr' is OK and length is this...
         return r0; }}                                         // ...which is the result we return.

   S_StrChkRtns r1 = {.ok = FALSE, .len = 0}; return r1;       // Else 'inStr' is longer than 'maxLen'. result .ok = FALSE.
}


/* -------------------------------- RegexLT_Init --------------------------------------

   Supply a malloc() and free(). Note 'getMem() MUST zero the memory block it supplies.
*/
PUBLIC void RegexLT_Init(RegexLT_S_Cfg const *cfg) { regexlt_cfg = cfg; }


/* ---------------------------------------- RegexLT_Compile ------------------------------------------------

   Compile 'regexStr' returning a Program in 'progV'. If success, returns 'E_RegexRtn_OK'
   (and a valid Program); otherwise an error code and 'progV' <- NULL.
*/
PUBLIC T_RegexRtn RegexLT_Compile(C8 const *regexStr, void **progV)
{
   if(regexlt_cfg == NULL)                                  // User did not supply a cfg with RegexLT_Init().
      { return E_RegexRtn_BadCfg; }                         // then go no further.

   S_RegexStats stats = regexlt_prescan(regexStr);          // Prescan regex; check for gross errors and count resources needed to compile it.

   if(stats.legal == FALSE)                                 // Regex was malformed?
      { return E_RegexRtn_BadExpr; }
   else                                                     // else 'regex' is free of gross errors.
   {
      if(regexlt_cfg->getMem == NULL)                       // Did not supply user getMem() (via RegexLT_Init() )
         { return E_RegexRtn_BadCfg; }
      else                                                  // else (try to) malloc() what we need and match against 'regex'
      {
         T_RegexRtn rtn;

         S_TryMalloc programTrunk[] = {{ progV,  sizeof(S_Program) }};

         if( getMemMultiple(programTrunk, RECORDS_IN(programTrunk)) == FALSE)   // Malloc program trunk?
            { return E_RegexRtn_OutOfMemory; }              // Some malloc() error.
         else
         {
            // Attach leaves to trunk
            S_Program *prog = *progV;

            S_TryMalloc programLeaves[] = {
               { (void**)&prog->chars.buf,    (U16)stats.charboxes    * sizeof(S_Chars) },    // Chars-Boxes
               { (void**)&prog->instrs.buf,   (U16)stats.instructions * sizeof(S_Instr) },    // Instructions (list)
               { (void**)&prog->classes.ccs,  (U16)stats.classes      * sizeof(S_C8bag) }};   // any Char-classes

            if( getMemMultiple(programLeaves, RECORDS_IN(programLeaves)) == FALSE)   // Malloc program leaves?
               { return E_RegexRtn_OutOfMemory; }           // Some malloc() error.
            else                                            // otherwise have malloc()ed for program we will now compile.
            {
               /* Fill in the sizes of the as-yet empty 'leaves' we malloced for 'prog'. If we pre-scanned correctly
                  the leaves should be enoigh for the compiled program. But in case not, these are hard fill-limits
                  for the compiler.
               */
               prog->classes.size = stats.classes;
               prog->chars.size   = stats.charboxes;
               prog->instrs.size  = stats.instructions;
               prog->subExprs     = stats.subExprs;

               // **** Compile 'regexStr' into 'prog' ****
               rtn = compileRegex(prog, regexStr) == TRUE   // Right now compile() just returns success or failure.
                  ? E_RegexRtn_OK
                  : E_RegexRtn_CompileFailed;

               printProgram(prog);

               if(rtn != E_RegexRtn_OK)                     // Compile failed?
               {                                            // then free() 'prog' now; otherwise it's returned to caller.
                  void *toFree[] = { prog->classes.ccs, prog->instrs.buf, prog->chars.buf };
                  safeFreeList(toFree, RECORDS_IN(toFree));
                  *progV = NULL;
               }
               return rtn;
            }}}}
} // RegexLT_Compile()



/* --------------------------------------- RegexLT_FreeProgram ---------------------------------- */

PUBLIC T_RegexRtn RegexLT_FreeProgram(void *prog)
{
   void *toFree[] = { ((S_Program*)prog)->classes.ccs, ((S_Program*)prog)->instrs.buf, ((S_Program*)prog)->chars.buf };
   safeFreeList(toFree, RECORDS_IN(toFree));
   return E_RegexRtn_OK;
}

/* ----------------------------------------- RegexLT_MatchProg -------------------------------------

   Match 'srcStr' against 'prog' which is a program made by RegexLT_Compile().  If 'ml' is not
   NULL then put any matches in 'ml'.

   If 'ml' == NULL AND *ml == NULL then malloc() / create a new match list. If *ml != NULL
   then presumes that 'ml' is an existing match list and, if '*(ml)->listSize' is legal, then
   the list is cleared (*ml)->put <- 0, and matches are added to the list.

   ***** Beware, when calling RegexLT_Match() for the 1st time '*ml' must either be initialised
   to NULL OR it must be to an already made match-list. ******
*/
PUBLIC T_RegexRtn RegexLT_MatchProg(void *prog, C8 const *srcStr, RegexLT_S_MatchList **ml, RegexLT_T_Flags flags)
{
   S_StrChkRtns strChk = inputOK(srcStr, regexlt_cfg->maxStrLen);    // Check for a legal source string.

   if(strChk.ok == FALSE )                                           // Too long? Non-printables maybe, depending on our rules?
      { return E_RegexRtn_BadInput; }                                // then bail rightaway.
   else
   {
      #define _prog ((S_Program*)(prog))
                                                                     // Compile 'regexStr'...
      _prog->instrs.maxRunCnt = strChk.len + 10;                     // Thread run-limit is string size plus for some anchors.

      // First, if caller supplies a hook to a match-list use the existing list in 'ml' or make a new new if necessary.

	  if(ml != NULL) {                                                // There's a hook for a match-list?
         if(*ml != NULL) {                                           // That hook is non-NULL? meaning there's already a list made?
		    if((*ml)->listSize >= _prog->subExprs && (*ml)->put <= (*ml)->listSize ) {   // List size is kosher.
		       (*ml)->put = 0; }}                                      // then clean off the list (just 'put' <- 0)
		 else {                                                        // else hook is NULL, meaning we must malloc a list now.
            if(_prog->subExprs > regexlt_cfg->maxSubmatches) {       // More sub-expressions than we counted in the pre-scan?
		       return E_RegexRtn_BadExpr; }                            // How's that - regex or compile is messed up somehow
		    else if( (*ml = newMatchList(_prog->subExprs)) == NULL) {  // else malloc() now; enuf to hold the global match plus sub-groups?
		       return E_RegexRtn_OutOfMemory; }}}                      // Oops! Heap fail.

      /* Run the compiled program 'prog.instrs' on 'srcStr' with matches written to 'ml', if this is supplied.
         Any thread may have up to a global match plus a match for each sub-expression. So reserve 'subExprs'+1.
      */
      U8  matchesPerThread = _prog->subExprs+2;
      return runCompiledRegex( &_prog->instrs, srcStr, ml, matchesPerThread, flags);
   }
}

/* -------------------------------- RegexLT_Match --------------------------------------

   Match 'srcStr' against 'regexStr', If 'ml' is not NULL then put any matches in 'ml'.

   If 'ml' == NULL AND *ml == NULL then malloc() / create a new match list. If *ml != NULL
   then presumes that 'ml' is an existing match list and, if '*(ml)->listSize' is legal, then
   the list is cleared (*ml)->put <- 0, and matches are added to the list.

   ***** Beware, when calling RegexLT_Match() for the 1st time '*ml' must either be initialised
   to NULL OR it must be to an already made match-list. ******
*/
PUBLIC T_RegexRtn RegexLT_Match(C8 const *regexStr, C8 const *srcStr, RegexLT_S_MatchList **ml, RegexLT_T_Flags flags)
{
   S_StrChkRtns strChk = inputOK(srcStr, regexlt_cfg->maxStrLen);             // Check for a legal source string.

   if(strChk.ok == FALSE )                                                    // Too long? Non-printables maybe, depending on our rules?
      { return E_RegexRtn_BadInput; }                                         // then bail rightaway.
   else                                                                       // else input string is OK. Continue.
   {
      T_RegexRtn rtn; S_Program * prog;                                       // Compiled 'regex' will be attached to this.

      if( E_RegexRtn_OK != (rtn = RegexLT_Compile(regexStr, (void**)&prog)))   // Compile 'regexStr'... failed?
         { return rtn; }                                                      // then return why.
      else  {                                                                  // else 'prog' has a valid program
         rtn =  RegexLT_MatchProg(prog, srcStr, ml, flags);                   // Run 'srcStr' thru 'prog'
         RegexLT_FreeProgram(prog);                                           // One-time Match() so free() 'prog' (which) was malloced in RegexLT_Compile().
         return rtn; }
   }
}

/* ----------------------------------- escCharToASCII ----------------------------------- */

PRIVATE C8 escCharToASCII(C8 ch)
{
   switch(ch) {
      case '$':   return '$';
      case '\\':  return '\\';
      case 'r':   return CR;
      case 'n':   return LF;
      case 't':   return TAB;
      default:    return '0';
   }
}

/* ---------------------------------- appendMatch ----------------------------------- */

PRIVATE C8 * appendMatch(C8 *out, RegexLT_S_MatchList const *ml, U8 matchIdx)
{
   if(matchIdx < ml->put)
   {
      memcpy(out, ml->matches[matchIdx].at, ml->matches[matchIdx].len);
      out += ml->matches[matchIdx].len;
   }
   return out;
}

PRIVATE U8 toNum(C8 digit) { return digit - '0'; }

/* ---------------------------------------- replace ------------------------------------------

   Given 'ml' and a regex 'replaceStr', produce a replacement in 'out'.

   'ml' references both the source string and the matches within it. So the source string must
   persist for this call().
*/
PRIVATE T_RegexRtn replace(RegexLT_S_MatchList *ml, C8 const *replaceStr, C8 *out)
{
   BOOL esc;
   C8 const *rs;

   for(rs = replaceStr, esc = FALSE; *rs != '\0'; rs++)              // Until the end of the replace string...
   {
      if(*rs == '\\') {                                              // '\". escape?
         esc = !esc;                                                 // If prev char was NOT backslash, next char will be escaped, and vice versa.
         if(esc) {                                                   // Escaped now?
            continue; }}                                             // then continue to next (non-escaped) char.

      if(esc) {                                                      // Current char was escaped?
         esc = FALSE;                                                // Once we have handled it (the current char), we will be un-escaped.

         if(isdigit(*rs))                                            // '\1' -'\9' etc?
         {                                                           // is a replace tag so...
            out = appendMatch(out, ml, toNum(*rs));                  // look for and insert corresponding match from 'ml'. 'out' is advanced past inserted text.
         }                                                           // If no match then 'out' is unchanged.
         else                                                        // else not a replace tag, something else
         {
            C8 escCh;
            if( (escCh = escCharToASCII(*rs)) != '0') {              // '\r', '\n' etc?
               *out++ = escCh;                                       // If yes, insert the corresponding ASCII.
            }                                                        // else do nothing.
         }
      }
      else {                                                         // else current char was NOT escaped.
         if(*rs == '$')                                              // '$'?...
         {
            if( isdigit(*(++rs))) {                                  // '$1'.. '$9'?. Is (also) a replace tag. So...
               out = appendMatch(out, ml, toNum(*rs));               // look for and insert corresponding match from 'ml'. 'out' is advanced past inserted text.
            }                                                        // If no match then 'out' is unchanged.
         }
         else                                                        // else not '$' (or escaped char)
         {
            *out++ = *rs;                                            // so copy literally to 'out'
         }
      }
   }
   *out = '\0';                                                      // Exhausted 'replaceStr' so done. Terminate 'out'.
   return E_RegexRtn_OK;
}

/* ------------------------------------------- RegexLT_Replace -----------------------------------------

   Apply 'regexStr' to 'inStr'. If there are match(es) apply 'replaceStr' to the match(es); result
   into 'out'.

   Returns E_RegexRtn_Match if there were match(es) and one or more replacements. Otherwise some error
   code. A 'replaceStr' with no replacements is legal.
*/
PUBLIC T_RegexRtn RegexLT_Replace(C8 const *regexStr, C8 const *inStr, C8 const *replaceStr, C8 *out)
{
   if(regexlt_cfg == NULL)                                              // User did not supply a cfg with RegexLT_Init().
      { return E_RegexRtn_BadCfg; }                                     // then go no further.
   else {
      T_RegexRtn rtn;
      RegexLT_S_MatchList *ml = NULL;                                   // Handle for match list. Must be NULL to signal a new match list to be malloc()ed.

      if( (rtn = RegexLT_Match(regexStr, inStr, &ml, _RegexLT_Flags_None)) != E_RegexRtn_Match )      // No match?
         { return rtn; }                                                // then return 'E_RegexRtn_NoMatch' or some error code.
      else                                                              // else matched; so now replace.
      {
         rtn = replace(ml, replaceStr, out);
         return rtn == E_RegexRtn_OK ? E_RegexRtn_Match : rtn;          // Replace succeeded? then return 'E_RegexRtn_Match' else some error code.
      }}
}

/* ------------------------------------------- RegexLT_ReplaceProg -----------------------------------------

   Same as RegexLT_Replace() but using pre-compiled 'prog'.
*/
PUBLIC T_RegexRtn RegexLT_ReplaceProg(void *prog, C8 const *inStr, C8 const *replaceStr, C8 *out)
{
   if(regexlt_cfg == NULL)                                              // User did not supply a cfg with RegexLT_Init().
      { return E_RegexRtn_BadCfg; }                                     // then go no further.
   else {
      T_RegexRtn rtn;   RegexLT_S_MatchList *ml;                        // Handle for match list. Must be NULL to signal a new match list to be malloc()ed.

      if( (rtn = RegexLT_MatchProg(prog, inStr, &ml, _RegexLT_Flags_None)) != E_RegexRtn_Match )      // No match?
         { return rtn; }                                                // then return 'E_RegexRtn_NoMatch' or some error code.
      else                                                              // else matched; so now replace.
      {
         rtn = replace(ml, replaceStr, out);
         return rtn == E_RegexRtn_OK ? E_RegexRtn_Match : rtn;          // Replace succeeded? then return 'E_RegexRtn_Match' else some error code.
      }}
}

/* ------------------------------------------ RegexLT_FreeMatches ------------------------------- */

PUBLIC void RegexLT_FreeMatches(RegexLT_S_MatchList const *ml)
{
   if(ml != NULL) {              // There IS a match-list?
      safeFree(ml->matches);     // then, first free matches.
      safeFree((void*)ml); }     // then the enclosing list;
}


/* -------------------------------------- RegexLT_RtnStr ------------------------------------- */

PUBLIC C8 const * RegexLT_RtnStr(T_RegexRtn r)
{
   switch(r)
   {
      case E_RegexRtn_NoMatch:      return "No Match";
      case E_RegexRtn_Match:        return "Match(es)";
      case E_RegexRtn_BadExpr:      return "Malformed regex expression";
      case E_RegexRtn_BadCfg:       return "No malloc supplied";
      case E_RegexRtn_BadInput:     return "Bad input string (TBD)";
      case E_RegexRtn_OutOfMemory:  return "Out of memory";
      case E_RegexRtn_CompileFailed:return "Compile failed";
      case E_RegexRtn_RanTooLong:   return "Run limit exceeded";
      default:                      return "No string for this return code";
   }
}

// --------------------------------------------- end ---------------------------------------------------


