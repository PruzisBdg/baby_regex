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
|     RegexLT_Match()
|     RegexLT_Replace()
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


/* --------------------------------- RegexLT_PrintMatchList ------------------------------ */

PRIVATE void printOneMatch(RegexLT_S_Match const *m)
{
   #define _MaxChars 20
   C8 buf[_MaxChars+1];
   U8 len;

   memcpy(buf, m->at, len = MinU8(m->len, _MaxChars));
   buf[len] = '\0';
   printf(" [%d %d] \'%s\'", m->idx, m->len, buf);

   #undef _MaxChars
}

PUBLIC void RegexLT_PrintMatchList(RegexLT_S_MatchList const *ml)
{
   printf("\r\nMatches: list size = %d  matches = ",  ml->listSize);

   if(ml->put == 0)
      { printf("None!\r\n\r\n"); }
   else                                            // at least a global match
   {
      U8 c;

      printf("%d\r\n"                            // Number of matches
               "   global: ", ml->put);            // 'global:"

      printOneMatch(&ml->matches[0]);              // 'global [a,b]'

      if(ml->put > 1)
         { printf("\r\n   subs:   ");}
      else
         { printf("\r\n");}

      for( c = 1; c < ml->put; c++ )
         { printOneMatch(&ml->matches[c]); }       // 'subs: [1,43 'dog'  [7,9] 'cat'
   }
}

/* ------------------------ RegexLT_PrintMatchList_OnOneLine ------------------------- */

PUBLIC void RegexLT_PrintMatchList_OnOneLine(RegexLT_S_MatchList const *ml)
{
   if(ml->put == 0)
      { printf("------ No matches"); }
   else                                            // at least a global match
   {
      dbgPrint("------ Matches: ");
      U8 c;
      for( c = 0; c < ml->put; c++ )
         { printOneMatch(&ml->matches[c]); }       // 'subs: [1,43 'dog'  [7,9] 'cat'
   }

}

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


#if 0
/* -------------------------------- RegexLT_Match --------------------------------------

   Match 'srcStr' against 'regexStr'. If 'ml' is not NULL then add any matches to 'ml'.
*/
PUBLIC T_RegexRtn RegexLT_Match(C8 const *regexStr, C8 const *srcStr, RegexLT_S_MatchList **ml, RegexLT_T_Flags flags)
{
   if(regexlt_cfg == NULL)                                  // User did not supply a cfg with RegexLT_Init().
      { return E_RegexRtn_BadCfg; }                         // then go no further.

   S_StrChkRtns strChk = inputOK(srcStr, regexlt_cfg->maxStrLen);

   if(strChk.ok == FALSE )
      { return E_RegexRtn_BadInput; }

   S_RegexStats stats = regexlt_prescan(regexStr);          // Prescan regex; check for gross errors and count resources needed to compile it.

   if(stats.legal == FALSE)                                 // Regex was malformed?
   {
      return E_RegexRtn_BadExpr;
   }
   else                                                     // else 'regex' is free of gross errors.
   {
      if(regexlt_cfg->getMem == NULL)                       // Did not supply user getMem() (via RegexLT_Init() )
      {
         return E_RegexRtn_BadCfg;
      }
      else                                                  // else (try to) malloc() what we need and match against 'regex'
      {
         T_RegexRtn rtn;
         S_Program prog;                                    // 'regex' will be compiled into here.

         S_TryMalloc toMalloc[] = {                         // Memory which the compiler needs
            { (void**)&prog.chars.buf,    (U16)stats.charboxes    * sizeof(S_Chars) },
            { (void**)&prog.instrs.buf,   (U16)stats.instructions * sizeof(S_Instr)},
            { (void**)&prog.classes.ccs,  (U16)stats.classes     * sizeof(S_C8bag) } };

         if( getMemMultiple(toMalloc, RECORDS_IN(toMalloc)) == FALSE)   // Oops!?
         {
            rtn = E_RegexRtn_OutOfMemory;                   // Some malloc() error.
         }
         else                                               // otherwise we can compile and run 'regex'.
         {
            /* Fill in the sizes of the as-yet empty stores we malloced for 'prog'. These are
               hard fill-limits for the compiler.
            */
            prog.classes.size = stats.classes;
            prog.chars.size = stats.charboxes;
            prog.instrs.size = stats.instructions;
            prog.subExprs = stats.subExprs;
            prog.instrs.maxRunCnt = strChk.len + 10;        // Thread run-limit is string size plus for some anchors.

            if( compileRegex(&prog, regexStr) == FALSE)     // Compile 'regexStr' into 'prog'. Failed?
            {
               rtn = E_RegexRtn_CompileFailed;
               printProgram(&prog);
            }
            else                                            // else there's a program to run.
            {
               // First, if caller supplies a hook to a match-list then make one.
               if(ml != NULL) {
                  if(stats.subExprs > regexlt_cfg->maxSubmatches) {
                     rtn = E_RegexRtn_BadExpr;
                     goto FreeAndQuit;
                     }
                     else if( (*ml = newMatchList(stats.subExprs)) == NULL) {
                        rtn = E_RegexRtn_OutOfMemory;
                        goto FreeAndQuit; }                 // Big enuf to hold the global match plus sub-groups.
                  }

               /* Run the compiled program 'prog.instrs' on 'srcStr' with matches written to 'ml', if this is supplied.
                  Any thread may have up to a global match plus a match for each sub-expression. So reserve 'subExprs'+1.
               */
               printProgram(&prog);                         // Run 'prog' on 'srcStr'; matches into 'ml'.
               U8  matchesPerThread = stats.subExprs+2;
               rtn = runCompiledRegex( &prog.instrs, srcStr, ml, matchesPerThread, flags);
            }
         }
         // Done. Free() memory we malloced(), excepting the matches. These are for the caller.
         // Free in reverse order, to be tidy, though it's not essential.
         void *toFree[] = { prog.classes.ccs, prog.instrs.buf, prog.chars.buf };
FreeAndQuit:
         safeFreeList(toFree, RECORDS_IN(toFree));
         return rtn;
      }
   }
}

#else

/* --------------------------------------- RegexLT_FreeProgram ---------------------------------- */

PUBLIC T_RegexRtn RegexLT_FreeProgram(void *prog)
{
   void *toFree[] = { ((S_Program*)prog)->classes.ccs, ((S_Program*)prog)->instrs.buf, ((S_Program*)prog)->chars.buf };
   safeFreeList(toFree, RECORDS_IN(toFree));
   return E_RegexRtn_OK;
}

/* -------------------------------- RegexLT_Match --------------------------------------

   Match 'srcStr' against 'regexStr'. If 'ml' is not NULL then add any matches to 'ml'.
*/
PUBLIC T_RegexRtn RegexLT_Match(C8 const *regexStr, C8 const *srcStr, RegexLT_S_MatchList **ml, RegexLT_T_Flags flags)
{
   S_StrChkRtns strChk = inputOK(srcStr, regexlt_cfg->maxStrLen);    // Check for a legal source string.

   if(strChk.ok == FALSE )                                           // Too long? Non-printables maybe, depending on our rules?
      { return E_RegexRtn_BadInput; }                                // then bail rightaway.
   else
   {
      T_RegexRtn rtn;
      S_Program * prog;                                              // Compiled 'regex' will attached to this.
                                                                     // Compile 'regexStr'...
      if( E_RegexRtn_OK != (rtn = RegexLT_Compile(regexStr, (void**)&prog)))   // ... failed?
      {
         return rtn;                                                 // then return why.
      }
      else                                                           // else 'prog' has a valid program
      {
         prog->instrs.maxRunCnt = strChk.len + 10;                   // Thread run-limit is string size plus for some anchors.

         // First, if caller supplies a hook to a match-list then make one.
         if(ml != NULL) {
            if(prog->subExprs > regexlt_cfg->maxSubmatches) {
               rtn = E_RegexRtn_BadExpr;
               goto FreeAndQuit;
               }
               else if( (*ml = newMatchList(prog->subExprs)) == NULL) {    // Big enuf to hold the global match plus sub-groups?
                  rtn = E_RegexRtn_OutOfMemory;
                  goto FreeAndQuit; }
            }

         /* Run the compiled program 'prog.instrs' on 'srcStr' with matches written to 'ml', if this is supplied.
            Any thread may have up to a global match plus a match for each sub-expression. So reserve 'subExprs'+1.
         */
         U8  matchesPerThread = prog->subExprs+2;
         rtn = runCompiledRegex( &prog->instrs, srcStr, ml, matchesPerThread, flags);
   FreeAndQuit:
         RegexLT_FreeProgram((void*)prog);                          // Free() 'prog' (which) was malloced in RegexLT_Compile().
         return rtn;
      }
   }
}


#endif


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

/* ------------------------------------------- RegexLT_Replace -----------------------------------------


*/
PRIVATE U8 toNum(C8 digit) { return digit - '0'; }

PUBLIC T_RegexRtn RegexLT_Replace(C8 const *searchRegex, C8 const *inStr, C8 const *replaceStr, C8 *out)
{
   T_RegexRtn           rtn;
   RegexLT_S_MatchList  *ml;

   if(regexlt_cfg == NULL)                      // User did not supply a cfg with RegexLT_Init().
      { return E_RegexRtn_BadCfg; }             // then go no further.

   if( (rtn = RegexLT_Match(searchRegex, inStr, &ml, _RegexLT_Flags_None)) == E_RegexRtn_Match )      // Matched the regex?
   {
      // Then attempt replace.
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
   }
   return rtn;
}

/* ------------------------------------------ RegexLT_FreeMatches ------------------------------- */

PUBLIC void RegexLT_FreeMatches(RegexLT_S_MatchList const *ml)
{
   safeFree(ml->matches);     // First free matches.
   safeFree((void*)ml);              // then the enclosing match-list;
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


