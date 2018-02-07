/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Executes compiled search expression.
|
--------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "libs_support.h"
#include "util.h"
#include "regexlt_private.h"

// Private to RegexLT_'.
#define dbgPrint           regexlt_dbgPrint
#define safeFree           regexlt_safeFree
#define safeFreeList       regexlt_safeFreeList
#define getMemMultiple     regexlt_getMemMultiple

#define br regexlt_cfg


/* -------------------------------- matchedRegexCh ----------------------------------------- */

PRIVATE BOOL matchedRegexCh(C8 regexCh, C8 ch)
{
   return
      regexCh == '.'          // '.', i.e match anything?
         ? TRUE               // is always TRUE
         : ch == regexCh;     // otherwise TRUE if equal
}

/* ------------------------------- matchCharsList -------------------------------------- */


PRIVATE BOOL matchCharsList(S_Chars *chs, C8 const **in, C8 const *start, C8 const *mustEndBy)
{
   C8 ch;

   for(; *in <= mustEndBy; chs++, (*in)++)            // Next item on regex-list & next input char... but not past 'mustEndBy'
   {
      if((ch = **in) == '\0' &&                       // End of input string? AND
         chs->opcode != OpCode_Match)                 // have not matched all of this regex segment?
      {
         return FALSE;                                // then fail
      }
      else                                            // else process at least this char.
      {
         T_CharSegmentLen i;

         switch(chs->opcode)              // --- Which kind of char container?
         {
            case OpCode_Match:                        // End of this list (of char containers).
               return TRUE;                           // then completely matched the container-list; Success!

            case OpCode_Chars:            // --- Some segment of the source regex string. Compare char-by-char

               for(i = 0; i < chs->payload.chars.len; i++, (*in)++)  // Until the end of the regex segment
               {
                  ch = **in;
                  if(!matchedRegexCh(chs->payload.chars.start[i], ch))  // Input char did not match segment char?
                     { return FALSE; }                                  // Then this path has failed
                  else if(ch == '\0')                                   // End of input string?
                     { return FALSE; }                                  // then we exhausted the input before exhausting the regex-segment; Fail
               }                                      // else exhausted regex-segment (before end of input); Success.
               (*in)--;                               // Backup to last input char we matched, 'for(;; chs++, (*in)++)' will re-advance ptr.
               break;                                 // and continue through chars-list.

            case OpCode_EscCh:            // --- A single escaped char
               if( ch != chs->payload.esc.ch )        // Input char does not match it?
                  { return FALSE; }                   // then fail.
               else
                  { break; }                          // else continue through chars list

            case OpCode_Anchor:           // --- A single anchor
               if(*in <= start)                       // At or before start of input string? (should never be before but....)
                  { break; }                          // then continue through chars list
               else
                  { return FALSE; }                   // else fail.

            case OpCode_Class:            // --- A character class
               if( C8bag_Contains(chs->payload.charClass, ch) == FALSE )      // Input char is not in the class?
                  { return FALSE; }                                           // so fail
               else
                  { break; }                                                  // else continue through chars list

            default:                      // --- Container should only be one of the above. Anything else is a BIG fail.
               return FALSE;
         }
      }
   }
   return FALSE;
}


/* ----------------------------- recordMatch ------------------------------ */

PRIVATE void wrRecordAt(RegexLT_S_Match *m, C8 const *at, RegexLT_T_MatchLen idx, RegexLT_T_MatchLen len)
   { m->at = at; m->idx = idx; m->len = len; }

#define _IgnoreShorter  FALSE
#define _ReplaceShorter TRUE

PRIVATE BOOL recordMatch(RegexLT_S_MatchList *ml, C8 const *at, RegexLT_T_MatchLen idx, RegexLT_T_MatchLen len, BOOL replaceShorterSubmatch)
{
   if(ml != NULL) {

      if(replaceShorterSubmatch) {
         U8 c;
         for(c = 1; c < ml->put; c++) {                     // matches[0] is the global match. Do not replace that; start at [1].
            if(ml->matches[c].idx == idx) {
               wrRecordAt(&ml->matches[c], at, idx, len);
               return TRUE; }}}

      if(ml->put < ml->listSize) {
         wrRecordAt(&ml->matches[ml->put], at, idx, len);
         ml->put++;
         return TRUE; }}

   return FALSE;
}


/* ---------------------- Regex engine threads support --------------------------------- */

typedef struct {
   RegexLT_T_MatchIdx start;     // 1st char of matched segment, relative to start of input string
   RegexLT_T_MatchLen len;       // This many bytes long.
} S_Match;

typedef struct {
   S_Match  *ms;                 // To a buffer of matches.
   U8       bufSize,             // Buffer is this long
            put;                 // Next free, from zero upwards.
   BOOL     isOwner;             // This 'S_MatchList' malloced the ms[]; if FALSE then it is referencing ms[]...
} S_MatchList;                   // ...which were malloced by another 'S_MatchList'.


PRIVATE BOOL copyInMatch(RegexLT_S_MatchList *ml, S_Match const *m, C8 const *str)
{
   return recordMatch(ml, str+m->start, m->start, m->len, _ReplaceShorter);
}

typedef struct {
    T_InstrIdx  pc;              // Program counter
    C8 const   *sp;              // Source (input string) pointer.
    T_RepeatCnt runCnt;          // A run-count for the character set in this thread. For repeats e.g (Ha){3}
    C8 const   *subgroupStart;   // If a subgroup was opened in this thread, then this is the 1st char of the subgroup.
    S_MatchList matches;         // Matches which this thread has found - so far.
    BOOL        eatMismatches;   // Eat (leading) mismatches), at the start of the regex
                                 // Flag is defeated at the 1st match in a thread. Avoids thread blowup with 'A+..' which is implicitly '.*A+...' and just explodes.
} S_Thread;

#define _EatMismatches   TRUE
#define _StopAtMismatch  FALSE

typedef U8 T_ThrdListIdx;

typedef struct {
   S_Thread       *ts;
   T_ThrdListIdx   len, put;
} S_ThreadList;


/* -------------------------------- addMatch ---------------------------------------- */


PRIVATE void addMatch(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end)
{
   if(t->matches.put < t->matches.bufSize) {

      S_Match *m = &t->matches.ms[t->matches.put];

      if(start >= inStr && end >= start) {
         m->start = ClipU32toU16(start - inStr);
         m->len = ClipU32toU16(end - start + 1);
         t->matches.put++; }}
   else
      { printf("\r\n -------------- No room for match %c-%c\r\n", *start, *end); }
}

/* --------------------------- threadList -----------------------------------------

   Returns a list to hold/run 'len' threads. NULL if malloc() failed.
*/
PRIVATE S_ThreadList * threadList(T_ThrdListIdx len)
{
   S_Thread     *thrd;
   S_ThreadList *lst;

   S_TryMalloc toMalloc[] = {
      { (void**)&thrd, (size_t)len * sizeof(S_Thread)    },
      { (void**)&lst, (size_t)len * sizeof(S_ThreadList) }};

   if( getMemMultiple(toMalloc, RECORDS_IN(toMalloc)) == FALSE)
   {
      return NULL;            // ... but if a malloc() failed return NULL.
   }
   else
   {
      lst->ts = thrd;         // Attach thread holders to list
      lst->len = len;         // There are these many
      lst->put = 0;           // 1st thread will go here.
      return lst;             // and return the list...
   }
}

/* --------------------------- unThreadList -------------------------------------*/

PRIVATE void unThreadList(S_ThreadList * lst) { safeFree(lst->ts); safeFree(lst); }

/* ---------------------------- addThread --------------------------------

   Add 'toAdd' to 'l'. Return FALSE if no room to add.
*/
PRIVATE BOOL addThread(S_ThreadList *l, S_Thread const *toAdd)
{
   if(l->put >= l->len)
   {
      printf("****** No Add put %d len %d\r\n ***********\r\n", l->put, l->len);
      return FALSE;
   }
   else
   {
      if(toAdd == NULL)
      {
         return FALSE;
      }
      else
      {
         l->ts[l->put] = *toAdd;
         l->put++;
         return TRUE;
      }
   }
}

typedef struct {
   S_MatchList const *lst;
   BOOL              clone;
   U8                newBufSize;
} S_ThrdMatchCfg;

/* ------------------------------------------ newThread ------------------------------------------

   Make and return a new thread with a match-list empty or no, specified by 'mcf'.

   'src' is the current read of the input string. 'groupStart' and 'rpts' are copied from the
   thread which spawned this one.
*/
PRIVATE S_Thread *newThread(T_InstrIdx pc, C8 const *src, T_RepeatCnt rpts, C8 const *groupStart, S_ThrdMatchCfg const *mcf, BOOL eatMismatches)
{
   static S_Thread t;

   t.pc = pc;                          // Program counter
   t.sp = src;                         // input string read at...
   t.runCnt = rpts;                    // Repeats of this thread so far.
   t.subgroupStart = groupStart;       // If this thread starts a sub-group.
   t.eatMismatches = eatMismatches;    // if this thread eats leading mismatches.

   if(mcf->lst == NULL)                                                                // No existing matches to clone or reference?
   {
      t.matches.put = 0;                                                               // then match list for this thread starts empty

      // Malloc() 'newBufSize' slots for this match-list
      if( (t.matches.ms = br->getMem(mcf->newBufSize * sizeof(S_Match))) == NULL)      // Couldn't malloc for matches?
         { t.matches.bufSize = 0; }                                                    // then say there's space for none.
      else
         { t.matches.bufSize = mcf->newBufSize; }                                      // else malloc success; we can hold these many matches.
      t.matches.isOwner = TRUE;                                                        // Either way; we own the space/non-space for these matches.
   }
   else                                                                                // else there's and existing match list to clone or reference.
   {
      t.matches = *(mcf->lst);                                                         // Copy the 'shell' of the match list.

      if(mcf->clone) {                                                                 // Are we cloning the match-list?, not just referencing the matches.
                                                                                       // then malloc() 'newBufSize'  fresh slots
         if( (t.matches.ms = br->getMem(mcf->newBufSize * sizeof(S_Match))) == NULL)   // Could not malloc for the matches we must clone?
         {
            t.matches.bufSize = 0;                                                     // then we got a zero-sized match list
         }
         else {
            t.matches.bufSize = mcf->newBufSize;                                       // else malloc success; can hold this many matches.
            t.matches.isOwner = TRUE;                                                  // which we will clone, so we own them.

            if(mcf->lst->put > 0) {                                                    // There are matches in the existing list?
               memcpy(t.matches.ms, mcf->lst->ms, mcf->lst->put * sizeof(S_Match)); }} // then copy them into the new list we just made.
         }
      else {
         t.matches.isOwner = FALSE;
      }
   }
   return &t;
}

/* ------------------------------------------ clearThreadList ------------------------------------------ */

PRIVATE void clearThreadList(S_ThreadList *l)
{
   /* Before clearing the list free() any memory allocated for matches. Matches are
      made by one thread and referenced by others which are created as the regex is
      executed. We can't free the same memory twice; so must check.
   */
   U8 c;
   for(c = 0; c < l->put; c++)               // For each thread...
   {
      S_MatchList *m = &l->ts[c].matches;

      if(m->isOwner == TRUE)                 // This thread malloced the matches ms[]?
      {
         safeFree(m->ms);                    // then (this thread) free()s it.
         m->ms = NULL;                       // and null ptr cuz memory is gone.
      }
      m->bufSize = 0;                        // Park 'bufSize', 'put' & 'isOwner' tidy.
      m->put = 0;
      m->isOwner = FALSE;
   }
   // Reset the 'put' ptr clears the thread list itself; we don't bother to zero the actual Thread contents.
   l->put = 0;
}

/* ----------------------------------- swapPtr ---------------------------------------- */

PRIVATE void swapPtr(S_ThreadList **a, S_ThreadList **b)
   { S_ThreadList *t; t = *a; *a = *b; *b = t; }


/* ------------------------------------ prntAddThrd ------------------------------------

   Show what thread was added to left or right. Printout to 'buf'.
*/
PRIVATE C8 * prntAddThrd(C8 *buf, BOOL printBlank, T_ThrdListIdx thrd, T_InstrIdx attachedInstruction)
{
   if(printBlank)
      { buf[0] = '_'; buf[1] = '\0'; }                            // Meaning nothing was added
   else
      { sprintf(buf, "%d(%d:)", thrd, attachedInstruction); }     // "thrd(instr:)"
   return buf;
}

/* -------------------------------- printTriad -----------------------------------

    Print next 3 chars e.g '456'.
    If reach end of string print full-stop e.g '67.
*/
PRIVATE BOOL addCh(C8 *buf, C8 ch)
{
   if(ch == '\0')                   // End of string?
   {
      buf[0] = '.';                 // then print full-stop to show end of string
      buf[1] = ' ';                 // string-terminator
      return FALSE;                 // and say 'ch'was end of string
   }
   else
   {
      buf[0] = ch;                  // else print 'ch'
      return TRUE;                  // and say there may be more
   }
}

PRIVATE C8 const *printTriad(C8 const *p)
{
   static C8 buf[6];

   // Frame
   buf[0] = buf[4] = '\'';          // '---'


   // Add up to 3 chars; '.', full-stop if reach end of string first.
   U8 c;
   for(c = 1; c < 4 && addCh(&buf[c], *p); c++, p++) {}
   buf[5] = '\0';                   // '---\0'
   return buf;
}

/* ----------------------------------------- printRegexSample ------------------------------



*/
PRIVATE C8 const * printRegexSample(S_CharsBox const *cb)
{
   #define _width 8
   static C8 rgxBuf[_width+2];


   regexlt_sprintCharBox_partial(rgxBuf, cb, _width);
   strcat(rgxBuf, "         ");
   rgxBuf[_width] = '\0';
   if( rgxBuf[_width-1] != ' ' )
      { rgxBuf[_width-1] = rgxBuf[_width-2] = rgxBuf[_width-3] = '.'; }

   return (C8 const *)rgxBuf;

}

/* ----------------------------------------- prntRpts --------------------------------------

    Print the repeats-spec 'r' (for a thread) and the current repeat-count (of that thread)
    Same-ish format as printAnyRepeats() in regexlt_print.c
*/
PRIVATE C8 const * prntRpts( S_RepeatSpec const *r, T_RepeatCnt cnt )
{
   static C8 buf[20];

   if(r->valid)
      if(r->min == r->max)
         { sprintf(buf, "{%d}%d", r->min, cnt); }
      else if(r->max == _Repeats_Unlimited)
         { sprintf(buf, "{%d,*}%d", r->min, cnt); }
      else
         { sprintf(buf, "{%d,%d}%d", r->min, r->max, cnt); }
   else
      { sprintf(buf, "{_}%d", cnt); }
   return buf;
}

/* ----------------------------------- runOnce ---------------------------------

   Run the compiled regex 'prog' over 'str' until 'Match', meaning the regex was exhausted,
   OR 'str' is exhausted, meaning no match. List the total match and any subgroup matches
   in 'ml'.
*/
PRIVATE T_RegexRtn runOnce(S_InstrList *prog, C8 const *str, RegexLT_S_MatchList *ml, U8 maxMatches, RegexLT_T_Flags flags)
{
   S_ThreadList *curr, *next;

   /* Make (empty) 'now' and 'next' thread lists; each as long the compiled regex in 'prog'.
      Because no instruction splits into more than 2 paths, the tree/threads for executing
      'prog' can be no wider than 'prog' is long, plus 1 for the root thread.
   */
   T_ThrdListIdx len = prog->put + 3;     // Add 3 to be safe.
   curr = threadList(len);
   next = threadList(len);

   S_ThrdMatchCfg matchesCfg0 = {.lst = NULL, .clone = TRUE, .newBufSize = maxMatches };

   // Make the 1st thread in and put the 1st opcode in it. Attach the start of the input string.
   addThread(curr,                     // to the current thread list
      newThread(  0,                   // PC starts at zero
                  str,                 // From start of the input string
                  0,                   // Loop/repeat count starts at 0. WIll increment if JMP back to reuse previous Chars-Box.
                  NULL,                // No match-list
                  &matchesCfg0,        // Accumulate any matches here.
                  _EatMismatches));    // Eat leading mismatches unless told otherwise.

   dbgPrint("------ Trace:\r\n"
            "   '(<' eat-leading,   '==' 'matched char' ' !=' no match\r\n"
            "   '+>' append to current list   '-->' append to next list\r\n"
            "   '-> _' = nothing (dead) \r\n\r\n");
   C8 bufL[20], bufR[20];

   /* Run 'prog', which will create and terminate threads. Do until we either reach 'Match'
      in 'prog' (success) OR no more threads scheduled, meaning we exhausted the input
      string without a match (fail).
   */
   T_RegexRtn rtn = E_RegexRtn_NoMatch;      // Unless we succeed or get an exception, below.
   C8 const *mustEnd = str + regexlt_cfg->maxStrLen;

   // Set once found e.g '34' in '34444' using '34+'.
   BOOL matchedMinimal = FALSE;

   //if(ml != NULL) {ml->put = 0;}


   U8 execCycles = 0;                     // Count how many times we renew the thread list.

   do {
      T_InstrIdx     pc;
      S_Instr const  *ip;
      C8 const       *sp;               // Advance through input string.
      C8 const       *gs;
      C8 const       *cBoxStart;          // Start of current Char-Box (while 'strP' may be advanced past Box)
      T_RepeatCnt    loopCnt = 0;

      T_ThrdListIdx  ti;
      T_ThrdListIdx  cput;
      BOOL addL, addR;

      if(execCycles % 10 == 0) {
         dbgPrint("   PC CBox/Op      inStr    [thrd -> l(@pc:), r(@pc:) {rpt-cnt}]:\r\n"
                  "-----------------------------------------------------------------\r\n"); }

      for(ti = 0; ti < curr->put; ti++)                                       // For each active thread.
      {

         S_Thread * thrd = &curr->ts[ti];
         S_Thread *newT;

         pc = thrd->pc;                                                       // The program counter and...
         sp = thrd->sp;                                                       // ..it's read pointer (to the source string)
         gs = thrd->subgroupStart;
         loopCnt = thrd->runCnt;                                              // Read the thread repeat count, to be propagated (below) and tested by 'Split' (below).

 ReloadInstuction:
         ip = &prog->buf[pc];                                                 // (Address of) the instruction referenced by 'pc'

         /* Premake a matches-cfg for most of the addThread()s below. Default is that
            thrd->matches is cloned into a new buffer[maxMatches]
         */
         S_ThrdMatchCfg dfltMatchCfg = {
            .lst = &thrd->matches, .clone = TRUE, .newBufSize = maxMatches };

         cBoxStart = sp;

         switch(ip->opcode )                          // Opcode is?...
         {
            case OpCode_NOP:                          // ---- A No-op, from open-subgroup '(', which was not later filled by 'Split'
               pc++;                                                             // Advance program ctr...
               goto ReloadInstuction;                                            // ... load and execute next instruction.

            case OpCode_CharBox:                      // --- A list of Character(s)
               if(ip->charBox.eatUntilMatch && thrd->eatMismatches)              // Have not yet matched input against 1st char-group of regex?
               {                                                                 // then advance thru input string until we find 1st match
                  addL = FALSE; addR = FALSE; cput = next->put;

                  if( matchCharsList(ip->charBox.segs, &sp, str, mustEnd) == TRUE)    // Matched current CharBox?...
                  {                                                              // ...yes, 'sp' is now at 1st char AFTER matched segment.
                     addL = TRUE;                                                // so we will advance this thread

                     /* Advance this thread to the next regex instruction (pc+1); applied to the next
                        char in the input string after the match (sp). We just got our leading match on this
                        Char-Box so a subsequent mismatch should terminate this thread => '_StopAtMismatch'.
                     */
                     newT = newThread(pc+1, sp, loopCnt, gs, &dfltMatchCfg, _StopAtMismatch);
                     addMatch(newT, str, cBoxStart, cBoxStart);

                     if(ip->closesGroup)                                         // This chars-list closed a subgroup?
                     {
                        if(ip->opensGroup)                                       // and this chars-list also opened a subgroup
                        {
                           newT->subgroupStart = cBoxStart;
                           addMatch(newT, str, cBoxStart, sp-1);
                        }
                     }
                     else if(ip->opensGroup && newT->subgroupStart == NULL)      // else this path opens a subgroup now?
                     {
                        newT->subgroupStart = sp-1;                              // Mark the start -> will be copied into the fresh thread
                     }
                     addThread(next, newT);                                      // Add thread we made to 'next'

                     if( *(cBoxStart+1) != '\0')                                 // Advance... there's at least one more char in the input string?
                     {
                        addR = TRUE;                                             // then add a new thread to 'next' applying existing CharBox start at this new char.
                        dfltMatchCfg.clone = TRUE;
                        addThread(next,
                           newThread(pc, cBoxStart+1, loopCnt, gs, &dfltMatchCfg,
                              !matchedMinimal && prog->buf[pc+1].opcode == OpCode_CharBox
                                 ? _EatMismatches
                                 : _StopAtMismatch) );
                     }
                  }
                  else
                  {
                     if(sp >= mustEnd ) {                                        // else didn't match - because we reached limit on length of input string?
                        rtn = E_RegexRtn_BadInput;                               // then we are done running the program.
                        goto CleanupAndRtn;
                     }
                     else if( *(cBoxStart+1) != '\0')                                   // Advance... there's at least one more char in the input string?
                     {
                        addR = TRUE;                                             // then add a new thread to 'next' applying existing CharBox start at this new char.
                        addThread( next,
                           newThread( pc, cBoxStart+1, loopCnt, gs, &dfltMatchCfg,
                                       matchedMinimal ? _StopAtMismatch : _EatMismatches));
                     }
                  }                              // --- else continue below.
                                                                     dbgPrint("   %d: %s %s  %s    [%d --> %s,%s {%d}]\r\n",
                                                                        pc,
                                                                        printRegexSample(&ip->charBox),
                                                                        addL ? "==" : "(<",
                                                                        printTriad(cBoxStart),
                                                                        ti,
                                                                        prntAddThrd(bufL, addL==FALSE, cput, pc+1),
                                                                        prntAddThrd(bufR, addR==FALSE, addL==FALSE ? cput : cput+1, pc),
                                                                        loopCnt);
               }
               else                                                  // else we got the 1st match (above)
               {
                  if( matchCharsList(ip->charBox.segs, &sp, str, mustEnd) == TRUE)     // Source chars matched? ...
                  {
                                                                                 // ...(and 'sp' is advanced beyond the matched segment)
                     /* Because we matched, continue this execution path. Package the NEXT instruction
                        into a fresh thread for the 'next' thread list, with 'sp' at the char after
                        the matched segment.

                        Also, this was a char box; so bump the repeat count used by 'Split'
                        to test repeat-ranges.
                     */
                     newT = newThread(pc+1, sp, loopCnt, gs, &dfltMatchCfg, thrd->eatMismatches);

                     if(thrd->matches.put == 0)
                     {
                        addMatch(newT, str, cBoxStart, cBoxStart);
                     }

                     if(ip->closesGroup)                                         // This chars-list closed a subgroup?
                     {
                        if(ip->opensGroup && gs == NULL)                         // and this chars-list also opened a subgroup
                        {
                           newT->subgroupStart = cBoxStart;
                           addMatch(newT, str, cBoxStart, sp-1);
                        }
                        else if(gs != NULL)                                      // else is there's was an open subgroup on this path (which we close now)?
                        {
                           addMatch(newT, str, gs, sp-1);
                        }
                     }
                     else if(gs != NULL)                                         // else there's a subgroup on this path which remains open?
                     {
                        newT->subgroupStart = gs;                                // then copy over 'start' marker into the fresh thread...
                     }
                     else if(ip->opensGroup && newT->subgroupStart == NULL)      // else this path opens a subgroup now?
                     {
                        newT->subgroupStart = sp;                                // Mark the start -> will be copied into the fresh thread
                     }
                                                                     dbgPrint("   %d: %s ==  %s    [%d --> %d(%d:) {%d}]\r\n",
                                                                        pc, printRegexSample(&ip->charBox), printTriad(cBoxStart), ti, next->put, pc+1, loopCnt);
                     addThread(next, newT);                                      // Add thread we made to 'next'
                  }
                  else                                                           // Source chars did not match? OR input string too long?
                  {                                                              // then we are done with this branch of code execution; ...
                     if(sp >= mustEnd ) {                                        // Input too long?
                        rtn = E_RegexRtn_BadInput;                               // then we are done running the program
                        goto CleanupAndRtn; }
                     else {                                                      // else we are done just with this thread...Do not renew it in 'next'
                                                                     dbgPrint("   %d: %s !=  %s    [%d ->  _] \t\t\t\r\n",
                                                                        pc, printRegexSample(&ip->charBox), printTriad(cBoxStart), ti);
                     }
                  }
               }
               break;

            case OpCode_Match:
               rtn = E_RegexRtn_Match;

               /* If caller supplied a hook for a match list then we will have malloced for a match list.
                  Fill the list with the matches which this thread found. If we are looking for a
                  maximal match on the entire input string then this match may not be the first.
                  So empty 'ml' before adding the matches from this thread.
               */
               if(ml != NULL)                                                    // 'ml' references a (malloced) match list?
               {
                  ml->put = 0;                                                   // then empty this list (in case an earlier thread matched and filled it)

                  if(execCycles == 0 && ti == 0)                                 // First instruction AND 1st time thru? (is 'OpCode_Match')
                     {addMatch(thrd, str, str, str+strlen(str)-1); }             // then it's the empty regex; matches everything, so add the whole string.
                  else                                                           // else it's a possible global match....
                     { thrd->matches.ms[0].len = cBoxStart - str - thrd->matches.ms[0].start; }    // ...we already marked the start in matches.ms[0]; add the length.

                  // Copy any matches from this thread into the master
                  U8 execCycles;
                  for(execCycles = 0; execCycles < thrd->matches.put; execCycles++)
                     { copyInMatch(ml, &thrd->matches.ms[execCycles], str); }
               }

               if( flags & _RegexLT_Flags_MatchLongest )
               {
                  if(next->put == 0)
                  {
                     dbgPrint("   %d: Match!:             -- Final, longest *****\r\n", pc);
                     goto EndsCurrentStep;
                  }
               }
               else
               {
                  if(next->put == 0)
                  {
                     dbgPrint("   %d: Match!:             -- Final, first-maximal *****\r\n", pc);
                  }
                  else
                  {
                     dbgPrint("   %d: match:     @ %s -- interim, (%d threads still open)\r\n",
                                 pc, printTriad(cBoxStart), next->put);
                  }
               }
               break;


            case OpCode_Jmp:                          // --- Jump
               dfltMatchCfg.clone = FALSE;
                                                                     dbgPrint("   %d: jmp: %d     @ %s    [%d +>  %d(%d:)]    \t\t%s%s\r\n",
                                                                              pc, ip->left,
                                                                              printTriad(sp),
                                                                              ti, curr->put, ip->left,
                                                                              prntRpts(&ip->repeats, loopCnt),
                                                                              ip->left < pc ? "++" : "");
               addThread(curr, newThread(ip->left, sp,
                                          ip->left < pc ? loopCnt+1 : loopCnt,  // If jumping back then bump the loop cnt.
                                          gs, &dfltMatchCfg, thrd->eatMismatches));

               /* If this JMP is the last opcode before match, then it can only be a jump back. (A jump to the
                  next instruction is redundant; the  compiler would never produce it.) If we are JMPing back
                  then we have matched every regex element at least once, and so have at least a minimal match.
                  Once there is a minimal match, if any other search paths are 'eating' leading mismatches
                  they must stop doing so. Otherwise these searches will continue past this 1st match and find
                  any subsequent ones. Meaning, e.g ...

                     34+' will bypass the 1st '344' in '1234411134444' and continue to the 2nd, '34444'.

                  We don't want this unless we specifically unless '_RegexLT_Flags_MatchLongest/Latest'
                  AND we get to compare the earlier and later matches. 'matchedMinimal' <- TRUE prevents other
                  parallel searches from bypassing leading mismatches. If a search has already found
                  it's first match, it may continue to completion and find a longer sequence than that found
                  by the search which produced the first minimal match. But this sequence will be in the same
                  segment of source text, and not a separate section further along.
               */
               if( prog->buf[pc+1].opcode == OpCode_Match)
                  { matchedMinimal = TRUE; }
               continue;

            case OpCode_Split:                        // --- Split
               /* Add both forks to 'curr' thread; both will execute rightaway in this loop.

                  But... if this 'Split' instruction contains repeat-counts conditionals, then
                  continue on a loop or down a branch only if relevant the count criterion is met.

                  The left fork is a loopback thru the current chars-block, and continues
                  until 'max-repeats'. The right for is a jump forward to the next char
                  block, and doesn't happen until min-repeats.
               */

               addL = FALSE; addR = FALSE; cput = curr->put;

               if(!ip->repeats.valid || loopCnt < ip->repeats.max)             // Unconditional? OR is conditional AND have not tried max-repeats of current chars-block?
               {
                  dfltMatchCfg.clone = FALSE;
                  addThread(curr, newThread(ip->left, sp, ip->left < pc ? loopCnt+1 : loopCnt, gs, &dfltMatchCfg, thrd->eatMismatches));
                  addL = TRUE;
               }   // then this thread loops back to the current chars-block.

               if(!ip->repeats.valid || loopCnt >= ip->repeats.min)            // Unconditional? OR is conditional AND have tried at least min-repeats of current chars-block.
               {
                  dfltMatchCfg.clone = TRUE;
                  addThread(curr, newThread(ip->right, sp, 0, gs, &dfltMatchCfg, thrd->eatMismatches));
                  addR = TRUE;
               }  // then will now also attempt to match the next text block.
                                                                     dbgPrint("   %d: split(%d %d) @ %s    [%d +>  %s,%s]   \t\t%s%s\r\n",
                                                                        pc, ip->left, ip->right,
                                                                        printTriad(sp),
                                                                        ti,
                                                                        prntAddThrd(bufL, addL==FALSE, cput, ip->left),
                                                                        prntAddThrd(bufR, addR==FALSE, addL==FALSE ? cput : cput+1, ip->right),
                                                                        prntRpts(&ip->repeats, loopCnt),
                                                                        ip->left < pc ? "++" : "");

               /* Same as for JMP above; if this 'Split' is followed by a 'Match', then one of the splits must
                  be a backward jump. This implies we have matched every part of the regex at least once.
               */
               if( prog->buf[pc+1].opcode == OpCode_Match)
                  { matchedMinimal = TRUE; }
               continue;

         }     //switch(ip->opcode )
      }     // for(ti = 0; ti < curr->put; ti++)
EndsCurrentStep:

      /* Exhausted the current thread list. But may have queued some new threads in 'next.
         Make 'next' the new 'curr' and clean out 'next' for next go-around..
      */
                                                                        dbgPrint("\r\nnew list %d: [curr.put <- next.put]: [%d %d]->[%d _]\r\n",
                                                                                                                  execCycles, curr->put, next->put, next->put);
      swapPtr(&curr, &next);
      clearThreadList(next);

   } while(curr->put > 0 && ++execCycles < 20);


CleanupAndRtn:
   clearThreadList(curr);
   unThreadList(curr);
   unThreadList(next);
   return rtn;
}

PUBLIC RegexLT_S_MatchList * newMatchList(U8 len);

PRIVATE void swapML(RegexLT_S_MatchList **a, RegexLT_S_MatchList **b)
   { RegexLT_S_MatchList *t;  t = *a; *a = *b; *b = t; }

/* -------------------------------- addToMatchIdxs -----------------------------------------

   Add 'n' to the 'idx' of each match in 'l'.
*/
PRIVATE void addToMatchIdxs(RegexLT_S_MatchList *l, RegexLT_T_MatchIdx n)
{
   U8 c;
   for(c = 0; c < l->put; c++)
      { l->matches[c].idx += n; }
}

/* ----------------------------------- regexlt_runCompiledRegex ---------------------------------

   Run the compiled regex 'prog' over 'str' until 'Match', meaning the regex was exhausted,
   OR 'str' is exhausted, meaning no match. List the total match and any subgroup matches
   in 'ml'.

   If '_RegexLT_Flags_MatchLongest' is the repeat until no more matches and return the
   longest match.
*/
PUBLIC T_RegexRtn regexlt_runCompiledRegex(S_InstrList *prog, C8 const *str, RegexLT_S_MatchList **ml, U8 maxMatches, RegexLT_T_Flags flags)
{
   T_RegexRtn rtn, r2;

   rtn = runOnce(prog, str, *ml, maxMatches, flags);           // Try to match at least once.

   // Now, if we got 1st match and we are to look for longest anywhere, then try again
   if( BSET(flags, _RegexLT_Flags_MatchLongest | _RegexLT_Flags_MatchLast) &&          // Search for longest or last? AND
       rtn == E_RegexRtn_Match &&                              // got 1st match? AND
       *ml != NULL)                                            // Caller supplied matches-handle? ..._RegexLT_Flags_MatchLongest
   {                                                           // ...otherwise there's no point in trying again; what can we tell the caller?
      // Make a 2nd match list so we can compare matches and find longest
      RegexLT_S_MatchList *m2;
      if( (m2 = newMatchList(maxMatches)) == NULL)             // Couldn't make 2nd list?
      {
         rtn = E_RegexRtn_OutOfMemory;                         // then fail.
      }
      else                                                     // else attempt further matches, compare lengths.
      {
         U8 c = 0;
         do {
            RegexLT_S_Match *m = &(*ml)->matches[0];
            C8 const *newStart = m->at + m->len;               // Next search starts here, at the end of the previous match.

            if(newStart + m->len >= EndStr(str))               // But, adding the longest match so far boof past end of input string?
            {                                                  // then no subsequent match can be longer than longer than the one we have...
               break;                                          // ...so we are done.
            }
            else
            {                                                  // Try another match
               m2->put = 0;                                    // Clear out the match list (it may have been used before).
               if( (r2 = runOnce(prog, newStart, m2, maxMatches, flags)) != E_RegexRtn_Match )   // No more matches?
               {
                  if(r2 != E_RegexRtn_NoMatch)                 // There was an error? (not just a failure to match)?
                  {
                     rtn = r2;                                 // then return that error code.
                  }
                  break;                                       // Either-way, we are done; 'ml' holds the longest match so far, if any.
               }
               else                                            // else another match
               {
                  if( m2->matches[0].len > m->len ||           // Longest match so far? OR
                      BSET(flags, _RegexLT_Flags_MatchLast))   // we are looking for the last match?
                  {
                     /* This latest search was from 'newStart'. We must correct the idx' of
                        any matches for the distance from the beginning of the string 'src' to
                        'newSTart'.
                     */
                     addToMatchIdxs(m2, newStart - str);
                     swapML(ml, &m2);                          // else give this new match to caller.
                  }
                  else                                         // else we are looking for longest match and this isn't it.
                  {
                     continue;                                 // then discard this match. Back round and see if we can find another/longer one..
                  }
               }

            }
         } while(++c < 10);     // For now, in case we get lost in the 4th Quadrant.

         RegexLT_FreeMatches(m2);
      }
   }
   return rtn;
}


// -------------------------------------------------- eof -----------------------------------------------------
