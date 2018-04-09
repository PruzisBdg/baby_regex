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


/* -------------------------------- matchedRegexCh ----------------------------------------- */

PRIVATE BOOL matchedRegexCh(C8 regexCh, C8 ch)
{
   return
      regexCh == '.'          // '.', i.e match anything?
         ? TRUE               // is always TRUE
         : ch == regexCh;     // otherwise TRUE if equal
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

   Compare the S_Chars[] list 'chs' against 'in'. Return TRUE if there's a full match
   before a '\0' or 'end'.

   'start' should the beginning of the WHOLE input string; used to match the '^' anchor.

    Returns with 'in' at the 1st char AFTER the segment which matches 'chs'.
*/
PRIVATE BOOL matchCharsList(S_Chars *chs, C8 const **in, C8 const *start, C8 const *end)
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

               for(i = 0; i < chs->payload.chars.len; i++, (*in)++)  // Until the end of the regex segment
               {
                  ch = **in;
                  if(!matchedRegexCh(chs->payload.chars.start[i], ch))  // Input char did not match segment char?
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

            case OpCode_Anchor:           // --- A single anchor
               if( (chs->payload.anchor.ch == '^' && *in <= start) ||               // Start anchor AND at or before start of input string? OR (should never be before but....)
                   (chs->payload.anchor.ch == '$' && **in == '\0') ||               // End anchor AND at end of string? OR
                    chs->payload.anchor.ch == 'b' && wordBoundary(start, *in) ||    // Word boundary? OR
                    chs->payload.anchor.ch == 'B' && notaWordBoundary(start, *in))  // Not a word boundary?
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
            if(ml->matches[c].idx == idx && ml->matches[c].len < len) {
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
   S_Match     *ms;              // To a buffer of matches.
   U8          bufSize,          // Buffer is this long
               put;              // Next free, from zero upwards.
   T_InstrIdx  latestPC;         // Instruction counter at the most recent match.
   BOOL        isOwner;          // This 'S_MatchList' malloced the ms[]; if FALSE then it is referencing ms[]...
} S_MatchList;                   // ...which were malloced by another 'S_MatchList'.


PRIVATE BOOL copyInMatch(RegexLT_S_MatchList *ml, S_Match const *m, C8 const *str)
{
   return recordMatch(ml, str+m->start, m->start, m->len, _IgnoreShorter);
}

typedef struct {
    T_InstrIdx  pc;              // Program counter
    C8 const   *sp;              // Source (input string) pointer.
    T_RepeatCnt rptCnt;          // A run-count for the character set in this thread. For repeats e.g (Ha){3}
    C8 const   *subgroupStart;   // If a subgroup was opened in this thread, then this is the 1st char of the subgroup.
    T_InstrIdx  lastOpensSub;    // Instruction (PC) which opened the most recent sub-group / sub-expression.
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

PRIVATE void addMatchSub(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end, BOOL eatLeads, BOOL replaceShorter)
{
   if(start < inStr || end < start)                                     // 'start' before start of source string? OR 'end' before 'start'?
      { errPrint("\r\n -------------- Illegal match parms (%d,%d)\r\n", start-inStr, end-start); }   // then illegal match interval.
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
                             dbgPrint("Add lead  m[0]<-[%d %d] @ %d\r\n", m->start, m->len, t->matches.latestPC); }
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
                              dbgPrint("AddM m[%d]<-(%d %d) @ %d\r\n", t->matches.put, m->start, m->len, t->matches.latestPC);
            t->matches.put++; }                                         // and we have one more (match)
         else {                                                         // else not adding a new match
            if(dup == TRUE) {                                           // Extending existing match?
               t->matches.ms[t->matches.put-1].len = len;               // Just replace existing length with new.
                              dbgPrint("\r\n DupM m[%d] (%d,%d)->(%d,%d) @%d \r\n",
                                  t->matches.put-1, startIdx,  t->matches.ms[t->matches.put-1].len, startIdx, len, t->matches.latestPC );
            }
            else
               { dbgPrint("\r\n -------------- No room for match (%d,%d) capacity = %d\r\n", start-inStr, end-start, t->matches.bufSize);  }
         }
      }
   }
}

PRIVATE void addMatch(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end)
   { addMatchSub(t, inStr, start, end, FALSE, FALSE); }

PRIVATE void addLeadMatch(S_Thread *t, C8 const *inStr, C8 const *start, C8 const *end)
   { addMatchSub(t, inStr, start, end, TRUE, FALSE); }

/* --------------------------- threadList -----------------------------------------

   Returns a list to hold/run 'len' threads. NULL if malloc() failed.
*/
PRIVATE S_ThreadList * threadList(T_ThrdListIdx len)
{
   S_Thread     *thrd;
   S_ThreadList *lst;

   S_TryMalloc toMalloc[] = {
      { (void**)&thrd, (size_t)len * sizeof(S_Thread)    },       // All these threads...
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

   Add 'toAdd' to 'l'. Return FALSE if no room to add.
*/
PRIVATE BOOL addThread(S_ThreadList *l, S_Thread const *toAdd)
{
   if(l->put >= l->len)
   {
      errPrint("****** No Add put %d len %d\r\n ***********\r\n", l->put, l->len);
      return FALSE;
   }
   else
   {
      if(toAdd == NULL)
         { return FALSE; }
      else {
         l->ts[l->put] = *toAdd;
         l->put++;
         return TRUE; }
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
PRIVATE S_Thread *newThread(T_InstrIdx pc, C8 const *src, T_RepeatCnt rpts, C8 const *groupStart, T_InstrIdx subStartIdx, S_ThrdMatchCfg const *mcf, BOOL eatMismatches)
{
   static S_Thread t;

   t.pc = pc;                          // Program counter
   t.sp = src;                         // input string read at...
   t.rptCnt = rpts;                    // Repeats of this thread so far.
   t.subgroupStart = groupStart;       // If this thread starts a sub-group.
   t.lastOpensSub = subStartIdx;
   t.eatMismatches = eatMismatches;    // if this thread eats leading mismatches.

   if(mcf->lst == NULL)                                                                // No existing matches to clone or reference?
   {
      t.matches.put = 0;                                                               // then match list for this thread starts empty
      t.matches.latestPC = _Max_T_InstrIdx;                                            // this is 'no instruction'.

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
            { t.matches.bufSize = 0; }                                                 // then we got a zero-sized match list
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

/* ----------------------------------- sprntMatches --------------------------------------- */

PRIVATE void sprntMatches(C8 *buf, S_MatchList const *ml)
{
   C8 b1[10];
   buf[0] = '\0';
   for(U8 c = 0; c < ml->put; c++)
   {
      sprintf(b1, "[%d %d] ", ml->ms[c].start, ml->ms[c].len);
      strcat(buf, b1);
   }
}

/* ----------------------------------- sprntThread --------------------------------------- */

PRIVATE void sprntThread(C8 *buf, S_Thread const *t)
{
   C8 b1[50];
   sprntMatches(b1, &t->matches);
   sprintf(buf, "eat %d %s ",  t->eatMismatches, b1);
}

/* --------------------------------- threadsEquivalent ------------------------------------

   'a' and 'b' in a thread list are equivalent if they are at the same instruction (pc =
   program counter" and have the same repeat count.
*/
PRIVATE BOOL threadsEquivalent(S_Thread const *a, S_Thread const *b)
{
   return
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
               to->put++; dbgPrint("-##############################plus plus\r\n"); }                              // and to[] has an (additional) submatch.
         }
      }
   } // for each match in from[]
}

/* ------------------------------------ foundDuplicate ------------------------------------

    Return TRUE if there's an (earlier) duplicate of thread [ti] in 'thread-list 'tl'. A duplicate
    is a thread at the same instruction and with the same repeat-count as 'ti'.

    Duplicate threads can happen with regex which have explosive quantifiers. A duplicate means
    that the two threads have reached the same place by two different routes. There's no point
    in propagating both; they will take the same same path.
*/
PRIVATE BOOL foundEarlierDuplicate(S_ThreadList *tl, T_ThrdListIdx ti)
{
   if(ti > 0) {                                                      // At least one earlier thread in 'tl'?
      for(U8 c = ti; c; c--) {                                       // From the latest thread (-1) to the 1st
         if( threadsEquivalent(&tl->ts[ti], &tl->ts[c-1]) ) {        // This earlier thread is equivalent to the latest?
            mergeMatches(&tl->ts[c-1].matches, &tl->ts[ti].matches);
            C8 b1[100]; C8 b2[100];
            sprntThread(b1, &tl->ts[ti]);
            sprntThread(b2, &tl->ts[c-1]);
            dbgPrint("-------Duplicate found: rpts %d: [%d] %s [%d] %s\r\n", tl->ts[ti].rptCnt, ti, b1, c-1, b2);
            return TRUE; }}}                                         // then TRUE.
   return FALSE;                    // else no equivalent found -> FALSE.
}

/*----------------------------------- soloAnchor -------------------------------------

   Return TRUE if 'cb' holds just and anchor (which we do not match - below).
*/
PRIVATE BOOL soloAnchor(S_CharsBox const *cb)
   { return cb->put == 1 && cb->segs[0].opcode == OpCode_Anchor; }

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
   T_ThrdListIdx len = prog->put + 23;     // Add 3 to be safe.
   curr = threadList(len);
   next = threadList(len);

   S_ThrdMatchCfg matchesCfg0 = {.lst = NULL, .clone = TRUE, .newBufSize = maxMatches };

   // Make the 1st thread in and put the 1st opcode in it. Attach the start of the input string.
   addThread(curr,                     // to the current thread list
      newThread(  0,                   // PC starts at zero
                  str,                 // From start of the input string
                  0,                   // Loop/repeat count starts at 0. WIll increment if JMP back to reuse previous Chars-Box.
                  NULL,                // No group start
                  0,                   // Park program counter for sub-group at instruction zero.
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
         loopCnt = thrd->rptCnt;                                              // Read the thread repeat count, to be propagated (below) and tested by 'Split' (below).

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
                     newT = newThread(pc+1, sp, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg, _StopAtMismatch);

                     if(!soloAnchor(&ip->charBox))
                        { addLeadMatch(newT, str, cBoxStart, cBoxStart); }

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
                        dfltMatchCfg.clone = TRUE;                               // Spawning a new thread so clone match list of the existing thread (instead of referencing it).
                        addThread(next,
                           newThread(pc, cBoxStart+1, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg,
                              matchedMinimal ?                                   // Already got a (minimal) match?
                                 _StopAtMismatch : _EatMismatches ) );           // then end this thread upon hitting a mismatch -
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
                        addThread( next,
                           newThread( pc, cBoxStart+1, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg,
                                       matchedMinimal ? _StopAtMismatch : _EatMismatches));
                     }
                  }                              // --- else continue below.
                                                                     dbgPrint("   %d(%d:) %s %s  %s    [%d --> %s,%s {%d}]\r\n",
                                                                        ti, pc,
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

                        Also, this was a char box; so bump the repeat count used by 'Split'to test repeat-ranges.
                     */
                     newT = newThread(pc+1, sp, loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches);

                     /* If the Chars-Box we matched is the 1st AND if it contains something besides an anchor then
                        add a (leading) zero-length match interval i.e [box-start, box-start]. This match will be updated
                        to a global match if and when the regex is exhausted.
                     */
                     if(thrd->matches.put == 0 && !soloAnchor(&ip->charBox))
                        { addMatch(newT, str, cBoxStart, cBoxStart); }

                     /* If this instruction opens a sub-group and we did NOT loop directly back to this instruction
                        then we are starting a new subgroup which will also be the start of a new sub-match. Mark
                        this instruction and also the position in the source string.
                     */
                     if(ip->opensGroup && thrd->lastOpensSub != pc)              // Starting new subgroup.
                     {
                        newT->lastOpensSub = pc;                                 // then mark program counter so we can tell if we revisit.
                        newT->subgroupStart = cBoxStart;                         // and note the start of the (possible) sub-match in the source string.
                     }
                     else                                                        // else not starting a new subgroup...
                        { }    // ...so leave 'lastOpensSub' & 'subgroupStart' as they were copied from previous thread (above).

                     // If this instruction closes a subgroup then add a sub-match from 'subgroupStart' to group close at 'sp-1'.
                     if(ip->closesGroup)
                        { addMatch(newT, str, newT->subgroupStart, sp-1); }      // (sp-1, cuz src pointer is one-past subgroup close)

                                                                     dbgPrint("   %d(%d:) %s ==  %s    [%d --> %d(%d:) {%d}]\r\n",
                                                                        ti, pc, printRegexSample(&ip->charBox), printTriad(cBoxStart), ti, next->put, pc+1, loopCnt);
                     addThread(next, newT);                                      // Add thread we made to 'next'
                  }
                  else                                                           // Source chars did not match? OR input string too long?
                  {                                                              // then we are done with this branch of code execution; ...
                     if(sp >= mustEnd ) {                                        // Input too long?
                        rtn = E_RegexRtn_BadInput;                               // then we are done running the program
                        goto CleanupAndRtn; }
                     else {                                                      // else we are done just with this thread...Do not renew it in 'next'
                                                                     dbgPrint("   %d(%d): %s !=  %s    [%d ->  _] \t\t\t\r\n",
                                                                        ti, pc, printRegexSample(&ip->charBox), printTriad(cBoxStart), ti);
                     }
                  }
               }
               break;

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
                  ml->put = 0;                                                   // then empty this list (in case an earlier thread matched and filled it)

                  if(execCycles == 0 && ti == 0)                                 // First instruction AND 1st time thru? (is 'OpCode_Match')
                     {errPrint("line %d ", __LINE__); addMatch(thrd, str, str, str+strlen(str)-1); }             // then it's the empty regex; matches everything, so add the whole string.
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
                     dbgPrint("   %d(%d:) Match!:             -- Final, longest *****\r\n", ti, pc);
                     goto EndsCurrentStep;
                  }
               }
               else
               {
                  if(next->put == 0)
                  {
                     dbgPrint("   %d(%d:) Match!:             -- Final, first-maximal *****\r\n", ti, pc);
                  }
                  else
                  {
                     dbgPrint("   %d(%d:) match:     @ %s -- interim, (%d threads still open)\r\n",
                                 ti, pc, printTriad(cBoxStart), next->put);
                  }
               }
               break;


            case OpCode_Jmp:                          // --- Jump
               if( !foundEarlierDuplicate(curr, ti))
               {
                  dfltMatchCfg.clone = FALSE;
                                                                        dbgPrint("   %d(%d:) jmp: %d     @ %s    [%d +>  %d(%d:)]    \t\t%s%s\r\n",
                                                                                 ti, pc, ip->left,
                                                                                 printTriad(sp),
                                                                                 ti, curr->put, ip->left,
                                                                                 prntRpts(&ip->repeats, loopCnt),
                                                                                 ip->left < pc ? "++" : "");
                  addThread(curr, newThread(ip->left, sp,
                                             ip->left < pc ? loopCnt+1 : loopCnt,  // If jumping back then bump the loop cnt.
                                             gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches));

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
               } // if( !foundEarlierDuplicate(curr, ti))
               continue;

            case OpCode_Split:                        // --- Split
               if( !foundEarlierDuplicate(curr, ti))
               {
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
                     addThread(curr, newThread(ip->left, sp, ip->left < pc ? loopCnt+1 : loopCnt, gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches));
                     addL = TRUE;
                  }   // then this thread loops back to the current chars-block.

                  if(!ip->repeats.valid || loopCnt >= ip->repeats.min)            // Unconditional? OR is conditional AND have tried at least min-repeats of current chars-block.
                  {
                     dfltMatchCfg.clone = TRUE;
                     addThread(curr, newThread(ip->right, sp, 0, gs, thrd->lastOpensSub, &dfltMatchCfg, thrd->eatMismatches));
                     addR = TRUE;
                  }  // then will now also attempt to match the next text block.
                                                                        dbgPrint("   %d(%d:) split(%d %d) @ %s    [%d +>  %s,%s]   \t\t%s%s\r\n",
                                                                           ti, pc, ip->left, ip->right,
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
               } // if( !foundEarlierDuplicate(curr, ti))
               continue;
         }     //switch(ip->opcode )
      }     // for(ti = 0; ti < curr->put; ti++)
EndsCurrentStep:

      /* Exhausted the current thread list. But may have queued some new threads in 'next.
         Make 'next' the new 'curr' and clean out 'next' for next go-around..
      */
                                                                        dbgPrint("\r\n%d: ---- [curr.put <- next.put]: [%d %d]->[%d _]\r\n",
                                                                                                                  execCycles+1, curr->put, next->put, next->put);
      swapPtr(&curr, &next);
      clearThreadList(next);

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
