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
#define errPrint           regexlt_errPrint
#define safeFree           regexlt_safeFree
#define safeFreeList       regexlt_safeFreeList
#define getMemMultiple     regexlt_getMemMultiple

#define br regexlt_cfg

typedef enum { eMatchCase = 0, eIgnoreCase } E_CaseRule;

/* -------------------------------- matchedRegexCh ----------------------------------------- */

PRIVATE BOOL matchCI(C8 a, C8 b)
   { return toupper(a) == toupper(b); }   // Case-insensitive?

PRIVATE BOOL matchedRegexCh(C8 regexCh, C8 ch, E_CaseRule cr)
{
   return
      regexCh == '.'                   // '.', i.e match anything?
         ? TRUE                        // is always TRUE
         : (cr == eIgnoreCase          // case-insensitive?
               ? matchCI(ch, regexCh)  // then ignore case
               : ch == regexCh );      // otherwise TRUE if equal
}

/* ------------------------------------ wordBoundary -----------------------------------------

   Return TRUE if 'src' is 1st char of a word OR if 'src' is the 1st char AFTER a word. This
   includes start and end of string. 'start' is the start of string.
*/
PRIVATE BOOL wordBoundary(C8 const *start, C8 const *src)
{
   #define _isChar(ch) (!isspace(ch))
   return
      src == start                                             // 1st char of string?
         ? _isChar(*src)                                       // yes, if it's a char, i.e non-whitespace
         : (*src == '\0'                                       // else end-of-string? (AND the string has at least one char, else 'src' ==  'start' above)
               ? _isChar(*(src-1))                             // yes if previous ch is a char (non-whitespace)
               : (                                             // else if neither start nor end of string?
                     (isspace(*(src-1)) && _isChar(*src)) ||   // 'src' is a char AND previous is whitespace? OR
                     (_isChar(*(src-1)) && isspace(*src))));   // 'src' is whitespace AND previous is a char?
}

/* ------------------------------------ notaWordBoundary -----------------------------------------

   Return TRUE if 'src' is a char with a char on either side, or the converse, a whitespace with
   whitespace either side.

   Note that notaWordBoundary() is not the converse of wordBoundary(); there are sequences for which
   neither wordBoundary() nor notaWordBoundary() are true. Although '\b' are '\B' are the converse
   of each other, the way matchCharsList() works means !wordBoundary() != wordBoundary().
*/
PRIVATE BOOL notaWordBoundary(C8 const *start, C8 const *src)
{
   #define _isChar(ch) (!isspace(ch))
   return
      src == start                                             // 1st char of string?
         ? isspace(*src) && isspace(*(src+1))                  // yes, if first 2 chars are whitespace.
         : (*src == '\0'                                       // else end-of-string? (AND the string has at least one char, else 'src' ==  'start' above)
               ? isspace(*(src-1))                             // yes if previous ch is whitespace
               : (                                             // else if neither start nor end of string?
                                                               // <spc><spc><spc> OR <spc><spc><\0> OR...
                     (isspace(*(src-1)) && isspace(*src) && (isspace(*(src+1)) || *(src+1) == '\0')   ) ||
                                                               // <char><char><char> OR <char><char><\0>
                     (_isChar(*(src-1)) && _isChar(*src) && (_isChar(*(src+1)) || *(src+1) == '\0')  )));   // 'src' is whitespace AND previous is a char?
}

/* ------------------------------- matchCharsList --------------------------------------

   Compare the S_CharSegs[] list 'chs' against 'in'. Return TRUE if there's a full match
   before a '\0' or 'end'.

   'start' should the beginning of the WHOLE input string; used to match the '^' anchor.

   'cr' sets whether matching is case-sensitive or no. 'cr' may also be modified by this
   function if there's a case/no-case control in 'chs'.

    Returns with 'in' at the 1st char AFTER the segment which matches 'chs'.
*/
PRIVATE BOOL matchCharsList(S_CharSegs *chs, C8 const **in, C8 const *start, C8 const *end, E_CaseRule *cr)
{
   C8 ch;

   for(; *in <= end;                                        // Until defined end-of-input, which may be before '\0'.
         (*in) += (chs->opcode == OpCode_Anchor ? 0 : 1),   // If matching an anchor then hold, else next input char.
         chs++)                                             // Next item on regex-list
   {
      if((ch = **in) == '\0' &&                             // End of input string? AND
         chs->opcode != OpCode_Match &&                     // have not matched all of this regex segment? AND
         chs->opcode != OpCode_Anchor)                      // not on an anchor? - which may match '\0' and so is checked in case OpCode_Anchor: below
      {
         return FALSE;                                      // then failed to match all items in 'chs'
      }
      else                                                  // else process at least this char.
      {
         T_CharSegmentLen i;

         switch(chs->opcode)              // --- Which kind of char container is this chars-segment?
         {
            case OpCode_Match:                              // End of this list (of char containers).
               return TRUE;                                 // then completely matched the container-list; Success!

            case OpCode_Chars:            // --- Some segment of the source regex string. Compare char-by-char

               for(i = 0; i < chs->payload.literals.len; i++, (*in)++)  // Until the end of the regex segment
               {
                  ch = **in;
                  if(!matchedRegexCh(chs->payload.literals.start[i], ch, *cr))  // Input char did not match segment char?
                     { return FALSE; }                                  // Then this path has failed
                  else if(ch == '\0')                                   // End of input string?
                     { return FALSE; }                                  // then we exhausted the input before exhausting the regex-segment; Fail
               }                                            // else exhausted regex-segment (before end of input); Success.
               (*in)--;                                     // Backup to last input char we matched, 'for(;; chs++, (*in)++)' will re-advance ptr.
               break;                                       // and continue through chars-list.

            case OpCode_EscCh:            // --- A single escaped char
               if( ch != chs->payload.esc.ch )              // Input char does not match it?
                  { return FALSE; }                         // then fail.
               else
                  { break; }                                // else continue through chars list

            case OpCode_Anchor:           // --- A single anchor or control char e.g case-sensitivity.
               if(chs->payload.anchor.ch == 'I') {
                  *cr = eMatchCase; }
               else if(chs->payload.anchor.ch == 'i') {
                  *cr = eIgnoreCase; }
               else if( (chs->payload.anchor.ch == '^' && *in <= start) ||             // Start anchor AND at or before start of input string? OR (should never be before but....)
                   (chs->payload.anchor.ch == '$' && **in == '\0') ||                  // End anchor AND at end of string? OR
                    (chs->payload.anchor.ch == 'b' && wordBoundary(start, *in)) ||     // Word boundary? OR
                    (chs->payload.anchor.ch == 'B' && notaWordBoundary(start, *in)))   // Not a word boundary?
                  { break; }                                // then continue through chars list
               else
                  { return FALSE; }                         // else fail.

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


/* ----------------------------- recordSubMatch ------------------------------ */

PRIVATE void wrRecordAt(RegexLT_S_Match *m, C8 const *at, RegexLT_T_MatchLen idx, RegexLT_T_MatchLen len)
   { m->at = at; m->idx = idx; m->len = len; }

#define _IgnoreShorter  FALSE
#define _ReplaceShorter TRUE

PRIVATE BOOL recordSubMatch(RegexLT_S_MatchList *ml, C8 const *at, RegexLT_T_MatchLen idx, RegexLT_T_MatchLen len, BOOL replaceShorterSubmatch)
{
   if(ml != NULL) {

      if(replaceShorterSubmatch) {
         U8 c;
         for(c = 1; c < ml->put; c++) {                     // matches[0] is the global match. Do not replace that; start at [1].
            if(ml->matches[c].idx == idx && ml->matches[c].len < len) {
               wrRecordAt(&ml->matches[c], at, idx, len);
               return TRUE; }}}

      if(ml->put < ml->listSize) {
         wrRecordAt(&ml->matches[ml->put], at, idx, len);
         ml->put++;
         return TRUE; }}

   return FALSE;
}

typedef struct {
   RegexLT_T_MatchIdx start;     // 1st char of matched segment, relative to start of input string
   RegexLT_T_MatchLen len;       // This many bytes long.
} S_Match;

typedef struct {
   S_Match     *ms;              // To a buffer of matches.
   U8          bufSize,          // Buffer is this long
               put;              // Next free, from zero upwards.
   T_InstrIdx  latestPC;         // Instruction counter at the most recent match.
   BOOL        isOwner;          // This 'S_MatchList' malloced the ms[]; if FALSE then it is referencing ms[]...
} S_MatchList;                   // ...which were malloced by another 'S_MatchList'.

PRIVATE BOOL copyInMatch(RegexLT_S_MatchList *ml, S_Match const *m, C8 const *str)
{
   return recordSubMatch(ml, str+m->start, m->start, m->len, _ReplaceShorter);
}

PRIVATE void copyMatchToList(RegexLT_S_Match *dest, S_Match const *src, C8 const *srcStr)
{
   dest->idx = src->start;
   dest->len = src->len;
   dest->at = srcStr + src->start;
}

/* ---------------------- Regex engine threads support --------------------------------- */

typedef struct {
   T_InstrIdx  pc;              // Program counter
   C8 const   *sp;              // Source (input string) pointer.
   T_RepeatCnt rptCnt;          // A run-count for the character set in this thread. For repeats e.g (Ha){3}
   C8 const   *subgroupStart;   // If a subgroup was opened in this thread, then this is the 1st char of the subgroup.
   T_InstrIdx  lastOpensSub;    // Instruction (PC) which opened the most recent sub-group / sub-expression.
   S_MatchList matches;         // Matches which this thread has found - so far.
   BOOL        eatMismatches;   // Eat (leading) mismatches), at the start of the regex
                                // Flag is defeated at the 1st match in a thread. Avoids thread blowup with 'A+..' which is implicitly '.*A+...' and just explodes.
   E_CaseRule  caseRule;        // Match case; ignore case etc.
   BOOL        deleted;       // If FALSE then this Thread was a duplicate. It and it's matches were merged into another Thread.
} S_Thread;

#define _EatMismatches   TRUE
#define _StopAtMismatch  FALSE

typedef U8 T_ThrdListIdx;

typedef struct {
   S_Thread       *ts;
   T_ThrdListIdx   len, put;
} S_ThreadList;


/* -------------------------------- addMatch ---------------------------------------- */

#define _LineNumberFmt "%4u: "

#define _EatLeads          TRUE
#define _NoEatLeads        FALSE
PRIVATE void addMatchSub(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end, BOOL eatLeads, U16 line, C8 const *tag)
{
   #define _MatchHdr "      --- " _LineNumberFmt
   #define _MatchTag "  \t\t\t<%s>"

   if(start < inStr || end < start)                                     // 'start' before start of source string? OR 'end' before 'start'?
      { errPrint(_MatchHdr "AddM m[%d] <- &(?,?) (%d:) Illegal 'start' or 'end' ptr" _MatchTag "\r\n", line, t->matches.put, t->matches.latestPC, tag); }   // then illegal match interval.
   else                                                                 // else match interval is fine. Continue...
   {
      // then the interval is this.
      RegexLT_T_MatchIdx startIdx = ClipU32toU16(start - inStr);
      RegexLT_T_MatchIdx len = ClipU32toU16(end - start + 1);

      if(eatLeads && t->matches.put > 0)                                // Eating leading mismatches? AND the list has at least 1 match?
      {
         S_Match *m = &t->matches.ms[0];                                // 1st match is the global.
         if(startIdx < m->start) {                                      // This new match starts earlier than the global on the list?
            m->start = startIdx; m->len = len;                          // then the new match replaces the existing one.
                             dbgPrint(_MatchHdr "LeadM m[0] <- @(%d,%d) (%d:)" _MatchTag "\r\n", line, m->start, m->len, t->matches.latestPC, tag); }
         t->matches.latestPC = t->pc;
         if(t->matches.put == 0)
            { t->matches.put = 1; }
      }
      else                                                              // otherwise don't compare global match. Add 1st global or a sub-match.
      {
         /* If there's already a sub-match and the new match AND this new match is to the same sub-expression as the
            most recent existing match AND it starts at the same point as the previous sub-match but is longer (it should be)
            then lengthen the existing sub-match.

            Otherwise it's a minimal new sub-match and we append it to the match-list.
         */
         BOOL dup = FALSE;

         if(t->matches.put > 1 &&                                       // There's a sub-match? AND
            t->pc == t->matches.latestPC) {                             // it's on the same regex sub-expression as the most recent?
            if(t->matches.ms[t->matches.put-1].start == startIdx &&     // New match starts at same place in source string? AND
               len > t->matches.ms[t->matches.put-1].len ) {            // it's longer?
               dup = TRUE; }}                                           // then we have an extension of the most recent sub-match.

         if( dup == FALSE && t->matches.put < t->matches.bufSize) {     // NOT an extension? must add... Enuf room to add another match?

            S_Match *m = &t->matches.ms[t->matches.put];                // then add new match.
            m->start = startIdx; m->len = len;
            t->matches.latestPC = t->pc;
                              dbgPrint(_MatchHdr "AddM m[%d] <- @(%d,%d) (%d:)" _MatchTag "\r\n", line, t->matches.put, m->start, m->len, t->matches.latestPC, tag);
            t->matches.put++;
            }                                         // and we have one more (match)
         else {                                                         // else not adding a new match
            if(dup == TRUE) {                                           // Extending existing match?
               t->matches.ms[t->matches.put-1].len = len;               // Just replace existing length with new.
                              dbgPrint(_MatchHdr "DupM m[%d] @(%d,%d) -> (%d,%d) (%d:)" _MatchTag "\r\n",
                                  line, t->matches.put-1, startIdx,  t->matches.ms[t->matches.put-1].len, startIdx, len, t->matches.latestPC, tag );
            }
            else
               { dbgPrint(_MatchHdr "No room for match @(%d,%d) capacity = %d" _MatchTag "\r\n", line, start-inStr, end-start, t->matches.bufSize, tag);  }
         }
      }
   }
}

PRIVATE void addMatch(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end, U16 line, C8 const *tag)
   { addMatchSub(t, inStr, start, end, _NoEatLeads, line, tag); }

PRIVATE void addLeadMatch(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end, U16 line, C8 const *tag)
   { addMatchSub(t, inStr, start, end, _EatLeads, line, tag); }

/* --------------------------- threadList -----------------------------------------

   Returns a list to hold/run 'len' threads. NULL if malloc() failed.
*/
PRIVATE S_ThreadList * threadList(T_ThrdListIdx len)
{
   S_Thread     *thrd;
   S_ThreadList *lst;

   S_TryMalloc toMalloc[] = {
      { (void**)&thrd, (size_t)len * (sizeof(S_Thread)+2)    },       // All these threads...
      { (void**)&lst, 1            * sizeof(S_ThreadList) }};     // ...held in 1 list.

   if( getMemMultiple(toMalloc, RECORDS_IN(toMalloc)) == FALSE)
      { return NULL; }        // ... but if a malloc() failed return NULL.
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

   Add 'toAdd' to 'l'. Return NULL if no room to add; else return a reference to the 'S_Thread'
   added to 'l'.
*/
PRIVATE S_Thread const * addThread(S_ThreadList *l, S_Thread const *toAdd)
{
   if(l->put >= l->len)
   {
      errPrint("#%u ****** No Add put %d len %d\r\n ***********\r\n", tdd_TestNum, l->put, l->len);
      return NULL;
   }
   else
   {
      if(toAdd == NULL)
         { return NULL; }
      else {
         l->ts[l->put] = *toAdd;
         l->put++;
         return &l->ts[l->put-1]; }
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
PRIVATE S_Thread *newThread(S_Thread *t, T_InstrIdx pc, C8 const *src, T_RepeatCnt rpts, C8 const *groupStart,
                            T_InstrIdx subStartIdx, S_ThrdMatchCfg const *mcf, BOOL eatMismatches, E_CaseRule caseRule)
{
   t->pc = pc;                          // Program counter
   t->sp = src;                         // input string read at...
   t->rptCnt = rpts;                    // Repeats of this thread so far.
   t->subgroupStart = groupStart;       // If this thread starts a sub-group.
   t->lastOpensSub = subStartIdx;
   t->eatMismatches = eatMismatches;    // if this thread eats leading mismatches.
   t->caseRule = caseRule;
   t->deleted = FALSE;

   if(mcf->lst == NULL)                                                                // No existing matches to clone or reference?
   {
      t->matches.put = 0;                                                              // then match list for this thread starts empty
      t->matches.latestPC = _Max_T_InstrIdx;                                           // this is 'no instruction'.

      // Malloc() 'newBufSize' slots for this match-list
      if( (t->matches.ms = br->getMem(mcf->newBufSize * sizeof(S_Match))) == NULL)     // Couldn't malloc for matches?
         { t->matches.bufSize = 0; }                                                   // then say there's space for none.
      else
         { t->matches.bufSize = mcf->newBufSize; }                                     // else malloc success; we can hold these many matches.
      t->matches.isOwner = TRUE;                                                       // Either way; we own the space/non-space for these matches.
   }
   else                                                                                // else there's and existing match list to clone or reference.
   {
      t->matches = *(mcf->lst);                                                        // Copy the 'shell' of the match list.
      if(mcf->clone) {                                                                 // Are we cloning the match-list?, not just referencing the matches.
         if( (t->matches.ms = br->getMem(mcf->newBufSize * sizeof(S_Match))) == NULL)  // Could not malloc for the matches we must clone?
            { t->matches.bufSize = 0; }                                                // then we got a zero-sized match list
         else {
            t->matches.bufSize = mcf->newBufSize;                                      // else malloc success; can hold this many matches.
            t->matches.isOwner = TRUE;                                                 // which we will clone, so we own them.

            if(mcf->lst->put > 0) {                                                    // There are matches in the existing list?
               memcpy(t->matches.ms, mcf->lst->ms, mcf->lst->put * sizeof(S_Match)); }} // then copy them into the new list we just made.
         }
      else {
         t->matches.isOwner = FALSE;
      }
   }
   return t;
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

      if(m->isOwner == TRUE) {               // This thread malloced the matches ms[]?
         safeFree(m->ms);                    // then (this thread) free()s it.
         m->ms = NULL; }                     // and null ptr cuz memory is gone.

      m->bufSize = 0;                        // Park 'bufSize', 'put' & 'isOwner' tidy.
      m->put = 0;
      m->isOwner = FALSE;
   }
   l->put = 0;       // Reset the 'put' ptr clears the thread list itself; we don't bother to zero the actual Thread contents.
}

/* ----------------------------------- swapPtr ---------------------------------------- */

PRIVATE void swapPtr(S_ThreadList **a, S_ThreadList **b)
   { S_ThreadList *t; t = *a; *a = *b; *b = t; }


/* ------------------------------------ prntAddThrd ------------------------------------

   Show what thread was added to left or right. Printout to 'buf'.
*/
PRIVATE C8 * prntAddThrd(C8 *buf, BOOL printBlank, T_ThrdListIdx thrd, T_InstrIdx attachedInstruction, S_Thread const *t)
{
   #define _SubStartTag "<"

   C8 const *groupStart(C8 *out, S_Thread const *t)
   {
      if(t == NULL)
         { return _SubStartTag "xx"; }
      else {
         if(t->subgroupStart == NULL)
            { sprintf(out, _SubStartTag "_ "); }
         else
            { sprintf(out, _SubStartTag "%c ", *(t->subgroupStart) ); }
         return out; }
   }

   if(printBlank)
      { buf[0] = '_'; buf[1] = '\0'; }                            // Meaning nothing was added
   else
      { sprintf(buf, "%d(%d:)%s", thrd, attachedInstruction, groupStart((C8[10]){} ,t)); }     // "thrd(instr:)"
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

/* ----------------------------------------- printRegexSample ------------------------------ */

PRIVATE C8 const * printRegexSample(S_CharsBox const *cb)
{
   #define _width 8
   static C8 rgxBuf[_width+2];

   memset(rgxBuf, ' ', _width);                                         // Prefill with spaces.
   rgxBuf[_width] = '\0';
   regexlt_sprintCharBox_partial(rgxBuf, cb, _width);                   // Whatever kind of CharsBox, print it into rxgBuf[_width].
   rgxBuf[_width] = '\0';
   // If printout fills all of output field then end with trailing ellipsis ...
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

   if(r->cntsValid)
      if(r->min == r->max)
         { sprintf(buf, "{%d}%d", r->min, cnt); }
      else if(r->max == _Repeats_Unlimited)
         { sprintf(buf, "{%d,*}%d", r->min, cnt); }
      else
         { sprintf(buf, "{%d,%d}%d", r->min, r->max, cnt); }
   else if(r->always)
      { sprintf(buf, "{*}%d", cnt); }
   else
      { sprintf(buf, "{_}%d", cnt); }
   return buf;
}

/* ----------------------------------- sprntMatches --------------------------------------- */

PRIVATE C8 const * sprntMatches(C8 *out, S_MatchList const *ml)
{
   C8 b1[10];
   out[0] = '\0';
   for(U8 c = 0; c < ml->put; c++)
   {
      sprintf(b1, "(%d %d)", ml->ms[c].start, ml->ms[c].len);
      strcat(out, b1);
   }
   return out;
}

/* ----------------------------------- sprntThread --------------------------------------- */

PRIVATE C8 const * sprntThread(C8 *out, T_ThrdListIdx ti, S_Thread const *t)
{
   sprintf(out, "%u(%u:) M%s", ti, t->pc, sprntMatches((C8[50]){}, &t->matches));
   return out;
}

/* --------------------------------- threadsEquivalent ------------------------------------

   'a' and 'b' in a thread list are equivalent if they are at the same instruction (pc =
   program counter" and have the same repeat count.
*/
PRIVATE BOOL threadsEquivalent(S_Thread const *a, S_Thread const *b)
{
   return
      a->deleted == FALSE &&
      b->deleted == FALSE &&
      a->pc == b->pc &&
      a->rptCnt == b->rptCnt;
}

/* ---------------------------------- matchesSame -------------------------------------- */

PRIVATE BOOL matchesSame(S_Match const *a, S_Match const *b)
   { return a->start == b->start && a->len == b->len; }

/* ------------------------------------- mergeMatches ------------------------------------------------
*/
PRIVATE void mergeMatches(S_MatchList *to, S_MatchList const *from)
{
   for(U8 c = 0; c < from->put; c++)                     // For each match in 'from'...
   {
      BOOL dup = FALSE;
      for(U8 d = 0; d < to->put; d++)                    // With each match in 'to'...
      {
         if(matchesSame(&from->ms[c], &to->ms[d]) ) {    // Same [start, len]?
            dup = TRUE;                                  // then from[c] is already in to[];
            break; }                                     // quit looking now; no action required
      }
      if(dup == FALSE)                                   // Did NOT find from[c] in to[]?
      {                                                  // So must copy it in.
         if(c == 0)                                      // At from[0]? ... the global match
         {                                               // then compare global matches; from[0] vs. to[0].
            if(from->ms[0].start < to->ms[0].start)      // from[0] is earlier?
               { to->ms[0] = from->ms[0]; }              // then overwrite to[0] with that earlier match.
         }
         else                                            // else at from[1...]; submatches
         {                                               // Just add the submatch, if there's room.
            if(to->put < to->bufSize) {                  // Room in to[] yet?
               to->ms[to->put] = from->ms[c];            // then append from[c]
               to->put++; }                              // and to[] has an (additional) submatch.
         }
      }
   } // for each match in from[]
}

// -----------------------------------------------------------------------------------------------
PRIVATE C8 const * prntInstr(C8 *out, S_Instr const *ip)
{
   // For 'Split' and 'Jmp' append destinations. For other opcodes just print the opcode.
   if(ip->opcode == OpCode_Jmp) {
      sprintf(out, "jmp: %u", ip->left); }
   else if(ip->opcode == OpCode_Split) {
      sprintf(out, "split(%u,%u)", ip->left, ip->right); }
   else {
      strcpy(out, opcodeNames(ip->opcode)); }
   return out;
}

/* ------------------------------------ removeDuplicateThreads ------------------------------------

    Remove duplicate Threads in 'tl', folding the matches in the into the Thread(s) that are
    retained.  Duplicate Threads at are ones with the same instruction and with the same
    repeat-count.

    Duplicate threads can happen with regex which have explosive quantifiers. The two threads
    have reached the same place by two different routes. There's no point in propagating both;
    they will take the same same path.

    removeDuplicateThreads() folds later (higher-index) threads into earlier (lower-index)
    ones.

    Return FALSE if the list did not have at least 2 Thread (and hence could not have duplicates)
*/
PRIVATE BOOL removeDuplicateThreads(S_ThreadList *tl, S_InstrList const *instr)
{
   if(tl->put >= 2) {                                             // At least 2 Threads in 'tl'?
      BOOL prntedHdr = FALSE;

      for(U8 _from = tl->put-1; _from > 1; _from--) {             // From the last Thread to the 2nd....
         for(U8 _to = _from-1; _to; _to--) {                      // For each Thread preceding...
            S_Thread *from = &tl->ts[_from];
            S_Thread *to = &tl->ts[_to];

            if( threadsEquivalent(from, to) ) {                   // Later Thread is equivalent to earlier? ...

               if(prntedHdr == FALSE) {
                  dbgPrint("   ---- Found duplicates: Remove later (higher-indexed) one. ----\r\n");
                  prntedHdr = TRUE; }

               S_Instr *ip = &instr->buf[from->pc];

               dbgPrint("   %u(%u:) %s %s %s duplicates %s {n,n}n  --> ",
                           _from, from->pc,
                           prntInstr((C8[30]){}, ip),
                           sprntMatches((C8[50]){}, &from->matches),
                           prntRpts(&ip->repeats, from->rptCnt),
                           sprntThread((C8[100]){}, _to, to));

               mergeMatches(&to->matches, &from->matches);        // ...then merge matches from later in earlier.
               from->deleted = TRUE;                              // and mark later as deleted.

               dbgPrint("Merged: %s\r\n",  sprntThread((C8[100]){}, _to, to)); }}}

      if(prntedHdr == TRUE) {
         dbgPrint("\r\n"); }
      return TRUE; }
   return FALSE;                    // else no equivalent found -> FALSE.
}

/*----------------------------------- soloAnchor -------------------------------------

   Return TRUE if 'cb' holds just and anchor (which we do not match - below).
*/
PRIVATE BOOL soloAnchor(S_CharsBox const *cb)
   { return cb->put == 1 && cb->segs[0].opcode == OpCode_Anchor; }

/* ---------------------------------- rightOpen -------------------------------------

   Return TRUE if there's no upper limit on 'r' i.e it's from 'a*", 'a+' or a{n,}.
*/
PRIVATE BOOL rightOpen(S_RepeatSpec const *r)
   { return r->always || (r->cntsValid && r->max == _Repeats_Unlimited); }

/* ----------------------------------- runOnce ---------------------------------

   Run the compiled regex 'prog' over 'str' until 'Match', meaning the regex was exhausted,
   OR 'str' is exhausted, meaning no match. List the total match and any subgroup matches
   in 'ml'.
*/
PRIVATE T_RegexRtn runOnce(S_InstrList *prog, C8 const *str, RegexLT_S_MatchList *ml, U8 maxMatches, RegexLT_T_Flags flags)
{
   /* Make (empty) 'now' and 'next' thread lists; each as long the compiled regex in 'prog'. runOnce()
      executes the 'curr' Thread list. Any Threads which must continue are copied into 'next'. Any new
      Threads required are also made in 'next. Then, when all Threads in 'curr' have been exhausted 'next'
      and 'curr' are swapped, i.e 'next' becomes the new 'curr'.

      Because no instruction splits into more than 2 paths, the tree/threads for executing
      'prog' can be no wider than twice 'prog' length, plus 1 for the root thread. That's the reasoning
      at least; but it's not correct because some test cases need more.
   */
   S_ThreadList *curr, *next;
   T_ThrdListIdx len = (2 * (prog->put)) + 5;     // Add 3 to be safe.
   curr = threadList(len);
   next = threadList(len);

   S_ThrdMatchCfg matchesCfg0 = {.lst = NULL, .clone = TRUE, .newBufSize = maxMatches };

   // Make the 1st thread in and put the 1st opcode in it. Attach the start of the input string.
   addThread(curr,                     // to the current thread list
      newThread(  &(S_Thread){},
                  0,                   // PC starts at zero
                  str,                 // From start of the input string
                  0,                   // Loop/repeat count starts at 0. WIll increment if JMP back to reuse previous Chars-Box.
                  NULL,                // No group start
                  0,                   // Park program counter for sub-group at instruction zero.
                  &matchesCfg0,        // Accumulate any matches here.
                  _EatMismatches,      // Eat leading mismatches unless told otherwise.
                  eMatchCase));        // Match case is the default.

   dbgPrint("------ Trace:\r\n"
            "   '(<' eat-leading,   '==' 'matched char' ' !=' no match\r\n"
            "   '+>' append to current list   '-->' append to next list\r\n"
            "   '-> _' = nothing (dead) \r\n\r\n");

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
                                                                        dbgPrint("\r\n%d: ---- [curr.put, next.put]: [%d %d]->[%d _]\r\n",
                                                                                                                  execCycles, curr->put, next->put, next->put);
   do {
      T_InstrIdx     pc;                  // Program counter
      S_Instr const  *ip;                 // Current instruction (at Program Counter)
      C8 const       *sp;                 // Advance through input string.
      C8 const       *gs;                 // Start of a Subgroup in the current thread.
      C8 const       *cBoxStart;          // Start of current Char-Box (while 'strP' may be advanced past Box)
      T_RepeatCnt    loopCnt = 0;         // Repeats loop count.

      T_ThrdListIdx  ti;
      BOOL addL, addR;

      if(execCycles % 10 == 0) {
         dbgPrint("   thrd(PC) Op    inStr    [op -> L(PC:), R(PC:) {rpt-cnt}]:    {repeats}cnt\r\n"
                  "---------------------------------------------------------------------------------\r\n"); }

      for(ti = 0; ti < curr->put; ti++)                                       // For each active thread.
      {
         S_Thread * thrd = &curr->ts[ti];                                     // the current Thread.

         pc = thrd->pc;                                                       // The program counter and...
         sp = thrd->sp;                                                       // ..it's read pointer (to the source string)
         gs = thrd->subgroupStart;
         loopCnt = thrd->rptCnt;                                              // Read the thread repeat count, to be propagated (below) and tested by 'Split' (below).

 ReloadInstuction:
         ip = &prog->buf[pc];                                                 // (Address of) the instruction referenced by 'pc'

         // If this Thread was a duplicate of an earlier then skip it.
         if(thrd->deleted == TRUE) {
            dbgPrint("   %d(%d:) %s Deleted!\r\n", ti, pc, prntInstr((C8[30]){}, ip));
            continue; }

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
            {
               if(ip->charBox.eatUntilMatch && thrd->eatMismatches)              // Have not yet matched input against 1st char-group of regex?
               {                                                                 // then advance thru input string until we find 1st match
                  S_Thread *newL = NULL;
                  S_Thread const *thrdL = NULL; S_Thread const *thrdR = NULL;

                  addL = FALSE; addR = FALSE; T_ThrdListIdx cput = next->put;

                  if( matchCharsList(ip->charBox.segs, &sp, str, mustEnd, &thrd->caseRule) == TRUE)    // Matched current CharBox?...
                  {                                                              // ...yes, 'sp' is now at 1st char AFTER matched segment.
                     addL = TRUE;                                                // so we will advance this thread

                     /* ---- Left-fork

                        Advance this thread to the NEXT regex instruction (pc+1); applied to the NEXT
                        char in the input string after the match (sp). We just got our leading match on this
                        Char-Box so a subsequent mismatch should terminate this thread => '_StopAtMismatch'.
                     */
                     newL = newThread(&(S_Thread){}, pc+1, sp, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg, _StopAtMismatch, thrd->caseRule);

                     if(!soloAnchor(&ip->charBox))
                        { addLeadMatch(newL, str, cBoxStart, cBoxStart, __LINE__, "!soloAnchor(&ip->charBox)"); }

                     if(ip->closesGroup)                                         // This chars-list closed a subgroup?
                     {
                        if(ip->opensGroup)                                       // and this chars-list also opened a subgroup
                        {
                           newL->subgroupStart = cBoxStart;
                           addMatch(newL, str, cBoxStart, sp-1, __LINE__, "ip->opensGroup");
                        }
                     }
                     else if(ip->opensGroup && newL->subgroupStart == NULL)      // else this path opens a subgroup now?
                     {
                        newL->subgroupStart = sp-1;                              // Mark the start -> will be copied into the fresh thread
                     }
                     thrdL = addThread(next, newL);                              // Add thread we made to 'next'

                     /* ---- Right-fork

                        If there's there's at least one more char in the input string then start another thread at
                        the CURRENT regex instruction applied to the NEXT input char.
                           However do NOT do this if the current instruction is a single char and right-open e.g 'a*',
                        or a{2,}. For these forking here needlessly adds threads on each cycle; the current thread is
                        already counting the maximal match.
                     */
                     if( *(cBoxStart+1) != '\0' &&                               // At least one more char in the input string? AND
                        !rightOpen(&ip->repeats))                                // not right-open?
                     {
                        addR = TRUE;                                             // then add a new thread to 'next' applying existing CharBox start at this new char.
                        dfltMatchCfg.clone = TRUE;                               // Spawning a new thread so clone match list of the existing thread (instead of referencing it).
                        thrdR = addThread(next,
                           newThread(&(S_Thread){}, pc, cBoxStart+1, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg,
                              matchedMinimal ?                                   // Already got a (minimal) match?
                                 _StopAtMismatch : _EatMismatches, thrd->caseRule ) );           // then end this thread upon hitting a mismatch -
                     }
                  }
                  else                                                           // else failed to match this 1st Chars_Box?
                  {
                     if(*sp == '\0') {                                           // Hit end-of-string too?
                        rtn = E_RegexRtn_NoMatch;                                // then didn't even get a leading match. We are done.
                        goto CleanupAndRtn;
                     }
                     else if(sp >= mustEnd ) {                                   // else reached limit on length of input string?
                        rtn = E_RegexRtn_BadInput;                               // then input was too long; we are done.
                        goto CleanupAndRtn;
                     }
                     else if( *(cBoxStart+1) != '\0')                            // else if there's at least one more char in the input string?...
                     {                                                           // ...then advance to this char retry the existing CharsBox
                        addR = TRUE;                                             // starting at this new char.
                        thrdR = addThread( next,
                           newThread(&(S_Thread){}, pc, cBoxStart+1, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg,
                                       matchedMinimal ? _StopAtMismatch : _EatMismatches, thrd->caseRule));
                     }
                  }                              // --- else continue below.
                                                                     dbgPrint("   %d(%d:) %s %s  %s    \t[ --> %s,%s {%d}] \t\tLM%s\r\n",
                                                                        ti, pc,
                                                                        printRegexSample(&ip->charBox),
                                                                        addL ? "==" : "(<",
                                                                        printTriad(cBoxStart),
                                                                        prntAddThrd((C8[25]){}, addL==FALSE, cput, pc+1, thrdL),
                                                                        prntAddThrd((C8[25]){}, addR==FALSE, addL==FALSE ? cput : cput+1, pc, thrdR),
                                                                        loopCnt,
                                                                        addL == TRUE ? sprntMatches((C8[30]){}, &newL->matches ) : "");
               }
               else                                                  // else we got the 1st match (above)
               {
                  if( matchCharsList(ip->charBox.segs, &sp, str, mustEnd, &thrd->caseRule) == TRUE)     // Source chars matched? ...
                  {
                                                                                 // ...(and 'sp' is advanced beyond the matched segment)
                     /* ---- Left-fork.

                        Because we matched, continue this execution path. Package the NEXT instruction
                        into a fresh LEFT thread for the 'next' thread list, with 'sp' at the char after
                        the matched segment.

                        Also, this was a char box; so bump the repeat count used by 'Split'to test repeat-ranges.
                     */
                     S_Thread *newL = newThread(&(S_Thread){}, pc+1, sp, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches, thrd->caseRule);

                     /* If the Chars-Box we matched is the 1st AND if it contains something besides an anchor then
                        add a (leading) zero-length match interval i.e [box-start, box-start]. This match will be updated
                        to a global match if and when the regex is exhausted.
                     */
                     if(thrd->matches.put == 0 && !soloAnchor(&ip->charBox))
                        { addMatch(newL, str, cBoxStart, cBoxStart, __LINE__, "(thrd->matches.put == 0..."); }

                     /* If this instruction opens a sub-group and we did NOT loop directly back to this instruction
                        then we are starting a new subgroup which will also be the start of a new sub-match. Mark
                        this instruction and also the position in the source string.
                     */
                     if(ip->opensGroup && thrd->lastOpensSub != pc)              // Starting new subgroup.
                     {
                        newL->lastOpensSub = pc;                                 // then mark program counter so we can tell if we revisit.
                        newL->subgroupStart = cBoxStart;                         // and note the start of the (possible) sub-match in the source string.
                     }
                     else                                                        // else not starting a new subgroup...
                        { }    // ...so leave 'lastOpensSub' & 'subgroupStart' as they were copied from previous thread (above).

                     // If this instruction closes a subgroup AND there was ab earlier match opening a subgroup
                     // then add a sub-match from 'subgroupStart' to group close at 'sp-1'.
                     if(ip->closesGroup && newL->subgroupStart != NULL)
                        { addMatch(newL, str, newL->subgroupStart, sp-1, __LINE__, "ip->closesGroup"); }      // (sp-1, cuz src pointer is one-past subgroup close)

                                                                     dbgPrint("   %d(%d:) %s ==  %s    \t[ --> %d(%d:)" _SubStartTag "%c ,_ {%d}] \tLM%s\r\n",
                                                                        ti, pc, printRegexSample(&ip->charBox), printTriad(cBoxStart), next->put, pc+1,
                                                                        newL->subgroupStart == NULL ? '_' : *(newL->subgroupStart),
                                                                        loopCnt,
                                                                        sprntMatches((C8[30]){}, &newL->matches ));
                     addThread(next, newL);                                      // Add thread we made to 'next'
                  }
                  else                                                           // Source chars did not match? OR input string too long?
                  {                                                              // then we are done with this branch of code execution; ...
                     if(sp >= mustEnd ) {                                        // Input too long?
                        rtn = E_RegexRtn_BadInput;                               // then we are done running the program
                        goto CleanupAndRtn; }
                     else {                                                      // else we are done just with this thread...Do not renew it in 'next'
                                                                     dbgPrint("   %d(%d:) %s !=  %s    \t[ --> _,_]\r\n",
                                                                        ti, pc, printRegexSample(&ip->charBox), printTriad(cBoxStart));
                     }
                  }
               }
               break;
            } // case OpCode_CharBox:

            case OpCode_Match:
               rtn = E_RegexRtn_Match;

               /* Mark that we got at least a minimal match. After this any (live) thread will terminate
                  on a mismatch. This must be so otherwise at least one thread will (needlessly) eat mismatches
                  until the end of input - and then report 'E_RegexRtn_NoMatch' (above).
               */
               matchedMinimal = TRUE;

               /* If caller supplied a hook for a match list then we will have malloced for a match list.
                  Fill the list with the matches which this thread found. If we are looking for a
                  maximal match on the entire input string then this match may not be the first.
                  So empty 'ml' before adding the matches from this thread.
               */
               if(ml != NULL)                                                    // 'ml' references a (malloced) match list?
               {
#if 0
                  ml->put = 0;                                                   // then empty this list (in case an earlier thread matched and filled it)

                  if(execCycles == 0 && ti == 0)                                 // First instruction AND 1st time thru? (is 'OpCode_Match')
                     { addMatch(thrd, str, str, str+strlen(str)-1, __LINE__, "(execCycles == 0 && ti == 0)"); }             // then it's the empty regex; matches everything, so add the whole string.
                  else                                                           // else it's a possible global match....
                     { thrd->matches.ms[0].len = cBoxStart - str - thrd->matches.ms[0].start; }    // ...we already marked the start in matches.ms[0]; add the length.

                  // Copy any matches from this thread into the master
                  U8 i;
                  for(i = 0; i < thrd->matches.put; i++)
                     { copyInMatch(ml, &thrd->matches.ms[i], str); }
#else
                  if(execCycles == 0 && ti == 0)                                 // First instruction AND 1st time thru? (is 'OpCode_Match')
                     { addMatch(thrd, str, str, str+strlen(str)-1, __LINE__, "(execCycles == 0 && ti == 0)"); }             // then it's the empty regex; matches everything, so add the whole string.
                  else                                                           // else it's a possible global match....
                     { thrd->matches.ms[0].len = cBoxStart - str - thrd->matches.ms[0].start; }    // ...we already marked the start in matches.ms[0]; add the length.

                  if(ml->put == 0 || thrd->matches.ms[0].len > ml->matches[0].len)
                  {
                     copyMatchToList(&ml->matches[0], &thrd->matches.ms[0], str);
                     if(ml->put == 0) {ml->put = 1;}
                  }
                  // Copy any matches from this thread into the master
                  U8 i;
                  for(i = 1; i < thrd->matches.put; i++)
                     { copyInMatch(ml, &thrd->matches.ms[i], str); }
#endif
               }

               if( flags & _RegexLT_Flags_MatchLongest )
               {
                  if(next->put == 0)
                  {
                     dbgPrint("   %d(%d:) Match!:             -- Final, longest *****\r\n", ti, pc);
                     goto EndsCurrentStep;
                  }
               }
               else
               {
                  if(next->put == 0)
                  {
                     dbgPrint("   %d(%d:) Match!: %s    -- Final, first-maximal *****\r\n", ti, pc, sprntMatches((C8[30]){}, &thrd->matches));
                  }
                  else
                  {
                     dbgPrint("   %d(%d:) Match!:     @ %s -- interim, (%d threads still open)\r\n",
                                 ti, pc, printTriad(cBoxStart), next->put);
                  }
               }
               break;


            case OpCode_Jmp:                          // --- Jump
            {
               dfltMatchCfg.clone = FALSE;
               S_Thread * newT;
               T_ThrdListIdx cput = curr->put;

               addThread(curr, newT = newThread(&(S_Thread){}, ip->left, sp,
                                          ip->left < pc ? loopCnt+1 : loopCnt,  // If jumping back then bump the loop cnt.
                                          gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches, thrd->caseRule));

                                                                     dbgPrint("   %d(%d:) jmp: %d     @ %s    \t[ +>  %d(%d:),_]\t\t %s%s \tM%s\r\n",
                                                                              ti, pc, ip->left,
                                                                              printTriad(sp),
                                                                              cput, ip->left,
                                                                              prntRpts(&ip->repeats, loopCnt),
                                                                              ip->left < pc ? "++" : "",
                                                                              sprntMatches((C8[30]){}, &newT->matches));

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
            } // case OpCode_Jmp:
            continue;

            case OpCode_Split:                        // --- Split
            {
               /* Add both forks to 'curr' thread; both will execute rightaway in this loop.

                  But... if this 'Split' instruction contains repeat-counts conditionals, then
                  continue on a loop or down a branch only if relevant the count criterion is met.

                  The left fork is a loopback thru the current chars-block, and continues
                  until 'max-repeats'. The right for is a jump forward to the next char
                  block, and doesn't happen until min-repeats.
               */

               addL = FALSE; addR = FALSE;
               T_ThrdListIdx cput = curr->put;                                // Mark current 'put' for printout below, before we addThread()

               S_Thread *newL = NULL; S_Thread *newR = NULL;

               // ----- Left Fork? Loop back.
               if(!ip->repeats.cntsValid || loopCnt < ip->repeats.max)        // Unconditional repeat? OR repeat is conditional AND have not tried max-repeats of current chars-block?
               {
                  dfltMatchCfg.clone = FALSE;
                  addThread(curr, newL = newThread(&(S_Thread){}, ip->left, sp, ip->left < pc ? loopCnt+1 : loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches, thrd->caseRule));
                  addL = TRUE;
               }   // then this thread loops back to the current chars-block.

               // ----- Right Fork? Forward.
               if(!ip->repeats.cntsValid || loopCnt >= ip->repeats.min)       // Unconditional repeat? OR repeat is conditional AND have tried at least min-repeats of current chars-block.
               {
                  dfltMatchCfg.clone = TRUE;
                  addThread(curr, newR = newThread(&(S_Thread){}, ip->right, sp, 0, gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches, thrd->caseRule));
                  addR = TRUE;
               }  // then will now also attempt to match the next text block.
                                                                     dbgPrint("   %d(%d:) split(%d %d) @ %s    \t[ +>  %s,%s]\t %s%s \tLM%s \tRM%s\r\n",
                                                                        ti, pc, ip->left, ip->right,
                                                                        printTriad(sp),
                                                                        prntAddThrd((C8[25]){}, addL==FALSE, cput, ip->left, NULL),
                                                                        prntAddThrd((C8[25]){}, addR==FALSE, addL==FALSE ? cput : cput+1, ip->right, NULL),
                                                                        prntRpts(&ip->repeats, loopCnt),
                                                                        ip->left < pc ? "++" : "",
                                                                        addL == TRUE ? sprntMatches((C8[30]){}, &newL->matches) : "_",
                                                                        addR == TRUE ? sprntMatches((C8[30]){}, &newR->matches) : "_");

               /* Same as for JMP above; if this 'Split' is followed by a 'Match', then one of the splits must
                  be a backward jump. This implies we have matched every part of the regex at least once.
               */
               if( prog->buf[pc+1].opcode == OpCode_Match)
                  { matchedMinimal = TRUE; }
            } // case OpCode_Split:
            continue;
         }     //switch(ip->opcode )
      }     // for(ti = 0; ti < curr->put; ti++)
EndsCurrentStep:
                                                                        dbgPrint("\r\n%d: ---- [curr.put, next.put]: [%d %d]->[%d _]\r\n",
                                                                                                                  execCycles+1, curr->put, next->put, next->put);
      /* Exhausted the current thread list. But if the regex is not exhausted then will
         have queued new threads in 'next. Clean out 'curr' and make 'next' the new 'curr'.

         Before running the 'next' Thread list we have built, remove any duplicate Threads in it,
         folding the matches in the into one Thread that is retained.  Duplicate Threads at are
         ones with the same instruction and with the same repeat-count. They can happen e.g with
         regexes which have explosive quantifiers. Two or more threads have reached the same
         Instruction by different routes. There's no point in propagating each of these duplicates;
         they will take the same same path.
      */
      removeDuplicateThreads(next, prog);
      clearThreadList(curr);                       // Clear current list; to be populated from 'next'
      swapPtr(&curr, &next);                       // Make 'next' the current list - go round again.

      // Break if too many cycles of the thread list. Something badly wrong; as this is based of the
      // length of the input string.
      if(++execCycles > prog->maxRunCnt) {
         rtn = E_RegexRtn_RanTooLong;
         goto CleanupAndRtn; }

   } while(curr->put > 0);


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
