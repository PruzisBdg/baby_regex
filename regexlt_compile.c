/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Compile search expression.
|
--------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "libs_support.h"
#include "util.h"
#include "arith.h"
#include "regexlt_private.h"

// Private to RegexLT_'.
#define classParser_Init   regexlt_classParser_Init
#define classParser_AddDef regexlt_classParser_AddDef
#define classParser_AddCh  regexlt_classParser_AddCh
#define getCharClassByKey  regexlt_getCharClassByKey
#define parseRepeat        regexlt_parseRepeat
#define dbgPrint           regexlt_dbgPrint
#define errPrint           regexlt_errPrint


/* --------------------------------- CharClass_New -----------------------------------

   Return an empty 'S_C8bag' (to hold a char class). Return NULL if there's no more
   empties available - because pre-scan under-counted the number of classes in the regex.
*/
PUBLIC S_C8bag * CharClass_New(S_ClassesList *h)
{
   if(h->put >= h->size) {                         // Already took all available empties?
      return NULL; }                               // then fail -> NULL.
   else{
      memset(&h->ccs[h->put], 0, sizeof(S_C8bag)); // else clear out the S_C8Bag.
      return &h->ccs[h->put++]; }                  // return it. Advance get.
}



/* --------------------------------- translateEscapedWhiteSpace --------------------------------------

   Given 'regexStr', return the ASCII value any of '\r', '\n' etc. Otherwise return the char as read.
   If a legal escaped whitespace, then 'regexStr' is advanced past the preceding '\'; otherwise it
   is left unchanged.
*/
#define TAB 0x09
#define CR  0x0D
#define LF  0x0A

PRIVATE C8 translateEscapedWhiteSpace(C8 const ** regexStr )
{
   C8 ch = **regexStr;

   if(ch != '\\')                   // Not a '\'.
   {
      return ch;                    // then return just ch
   }
   else                             // else a '\'
   {
      (*regexStr)++;                // Advance to following ch...
      ch = **regexStr;              // ...and read it
      switch(ch)                    // Translate and return any legal escaped whitespace.
      {
         case 'r':   return CR;
         case 'n':   return LF;
         case 't':   return TAB;
         case '\\':  return '\\';   // Also '\', meaning literal '\'
         case '0':   return 0;

         default:                   // Wasn't whitespace?
            (*regexStr)--;          // then backup
            return '\\';            // and return '\', wot we backed up to.
      }
   }
}

/* ---------------------------------- nextCharsSeg ------------------------------------ */

PRIVATE BOOL nextCharsSeg(S_CharsBox *cb)
{
   if(cb->put < cb->bufSize-1) {
      cb->put++;
      return TRUE; }
   else {
      errPrint("No room to add chars-segment --------- put %d bufSize %d\r\n", cb->put, cb->bufSize);
      return FALSE; }
}

/* ----------------------------------- wrNextSeg ------------------------------------ */

PRIVATE BOOL wrNextSeg(S_CharsBox *cb, T_OpCode op)
{
   if( !nextCharsSeg(cb))
      { return FALSE; }
   else {
       cb->segs[cb->put].opcode = op;
       return TRUE;
   }
}

/* -------------------------------- bumpIfEmpty --------------------------------------

   If current 'cb.put' is not at an empty Chars-segment (OpCode_Null) then advance to
   next slot and write it to OpCode_Null (in case it's not already at that).
*/
PRIVATE BOOL bumpIfEmpty(S_CharsBox *cb)
{
   if( cb->segs[cb->put].opcode == OpCode_Null) {
      return TRUE; }
   else {
      return nextCharsSeg(cb);}
}



/* ------------------------------ handleEscapedNonWhtSpc ---------------------------

   Handle the following one-character escaped chars:

      - escaped regex control chars and anchors e.g '\?', '\*', '\^'.
            These are made into their corresponding single-char single char literals
            ('?', '*') inside an 'OpCode_EscCh'.

      - predefined character classes e.g \d == [0-9]
            These are put into a character class which is linked to a (new) OpCode_Class.

   Return FALSE if 'ch' isn't one of the above. If success, then 'idx' is advanced
   to the next S_Chars slot, which is written to 'OpCode_Null'
*/

PRIVATE C8 const escapedChars[] = "\\|.*?{}()^$";
PRIVATE C8 const escapedAnchors[] = "bBIi";              // Word boundaries 'Bb' and case/no-case 'iI'

PRIVATE BOOL handleEscapedNonWhtSpc(S_ClassesList *cl, S_CharsBox *cb, C8 ch)
{
   C8 const    *preBakedClass;
   BOOL        rtn = FALSE;                           // Until we succeed below.

   S_Chars *sg = cb->segs;

   if(sg[cb->put].opcode != OpCode_Null)                                         // Not already on an empty 'S_Chars'.
      { wrNextSeg(cb, OpCode_Null); }                                            // then advance to next slot. Make sure it's clean; it will likely be filled below.

   if(strchr(escapedChars, ch) != NULL)                                          // Literal of a regex control char?
   {
      sg[cb->put].opcode = OpCode_EscCh;                                         // will go in this next sg[cb->put]
      sg[cb->put].payload.esc.ch = ch;                                           // the char s this
      rtn = wrNextSeg(cb, OpCode_Null);                                          // Clear the next S_Chars slot (being tidy)
   }
   else if(strchr(escapedAnchors, ch) != NULL)                                   // (backslashed) regex anchor?
   {
      sg[cb->put].opcode = OpCode_Anchor;                                        // will go in this next sg[cb->put]
      sg[cb->put].payload.anchor.ch = ch;                                        // the char s this
      rtn = wrNextSeg(cb, OpCode_Null);                                          // Clear the next S_Chars slot (being tidy)
   }
   else if((preBakedClass = getCharClassByKey(ch)) != NULL)                      // else a predefined char class e.g '\w'
   {
      S_ParseCharClass parseClass;

      if( (sg[cb->put].payload.charClass = CharClass_New(cl)) != NULL  ) {       // Obtained a fresh empty class?, in the next sg[cb->put]
         classParser_Init(&parseClass);                                          // Start the private parser.
                                                                                 // Success adding char class? (should be, the class was pre-baked by us)
         if( classParser_AddDef(&parseClass, sg[cb->put].payload.charClass, preBakedClass) == TRUE ) {
            sg[cb->put].opcode = OpCode_Class;                                   // then label wot we made.
            rtn = wrNextSeg(cb, OpCode_Null); }}                                 // Clear the next S_Chars slot (being tidy). Return success or fail.
   }
   return rtn;
}

/* ---------------------------- isaRepeat -------------------------------------

   Return TRUE if 'rgx' is a (valid) repeat operator. These are '+','*','?'
   OR '{n,n}'.
*/
PRIVATE BOOL isaRepeat(C8 const *rgx)
{
   C8 const    **h = &rgx;                         // Input to parseRepeat()
   C8            ch = *rgx;
   S_RepeatSpec  rpt;                              // parseRepeat() writes result here

   return
      ch == '{'                                    // Opens a repeat-range?  e.g {3,7}
         ? parseRepeat(&rpt, h) == E_Complete      // then try to parse the {...}. Don't use result; just return success/fail
         : (ch == '?' || ch == '*' || ch == '+');  // Not a range; is it a single char?
}

PRIVATE C8 const * toClosesClass(C8 const *p)
   { for(; *p != ']' && *p != '\0'; p++) {} return p-1; }

PRIVATE C8 const * toClosesRpt(C8 const *p)
   { for(; *p != '}' && *p != '\0'; p++) {} return p; }

/* ------------------------------------------- gotCharsAtLeast --------------------------------- */

PRIVATE BOOL gotCharsAtLeast(S_CharsBox const *cb)
{
   return
      cb->put > 0 ||                               // Already completed a chars-segment? OR
      (
         cb->segs[0].opcode == OpCode_Chars &&     // If 1st segment is a chars-list? AND
         cb->segs[0].payload.chars.len > 0         // we already have at least 1 char in that list? (we should, otherwise it would be OpCode_Null)
      );
}


/* ----------------------------- fillCharBox ---------------------------------

   Given an (empty) 'character box' and a location in the regex string, read at
   'regexStr' and, if there are regex character literal(s) or a character class there,
   put those literal(s) or class into 'cBox'.

   If it's a character class, make that new class in 'cl' and link if to 'cBox'

   If 'regexStr' is already at the end of string, then make 'cBox' a MATCH, meaning
   a search will terminate success if it reaches here.

   Return TRUE if made and added a new 'cBox'. Will return FALSE if
      - at a control char e.g '|',
      - there's an illegal regex construction e.g ']' before an '['.
      - couldn't malloc() for a (needed) character class.
*/
extern C8 const *opcodeNames(T_OpCode op);

PRIVATE BOOL fillCharBox(S_ClassesList *cl, S_CharsBox *cb, C8 const **regexStr)
{
   C8 ch;
   S_ParseCharClass parseClass;
   T_ParseRtn rtn;

   S_Chars * sgs = cb->segs;
   cb->put = 0;                           // Fill 'cb' starting at cb->buf[0].
   sgs[cb->put].opcode = OpCode_Null;      // Until we fill it this 1st segment has no opcode, and...
   sgs[cb->put].payload.chars.len = 0;     // and, for a chars-segment, no chars either.

   /* If got a '(' rightaway. then this chars-list either opens a subgroup or will be an
      an entire subgroup.
   */
   if(**regexStr == '(')
   {
      cb->opensGroup = TRUE;
      (*regexStr)++;                // Goto next char.
   }

   while(1)
   {
      ch = **regexStr;

      if(!isprint(ch) && ch != '\0')                  // Neither printable OR end of regex string?
      {
         return FALSE;                                // then it's an illegal expression. fail.
      }
      else                                            // else a legal regex char OR end of the regex string.
      {
         ch = translateEscapedWhiteSpace(regexStr);   // Convert any '\r','\n' etc to ASCII values

         switch(sgs[cb->put].opcode)
         {
            case OpCode_Null:                         // --- No current context. See wot we got.
            case OpCode_Chars:                        // ---- Running through a character segment e.g '...fghij...'
               switch(ch)
               {
                  /* Any one of the following ends a list of chars and char-classes. Terminate the
                     list with 'OpCode_Match'; same as the compiled instruction list. That way the
                     regex engine thread can run down a char list and self-terminate at the end.
                  */

                  /* Either'(' or ')' (below) close the preceding set of character segments/escapes/classes.
                     ')' means that the set closes a sub-group in the regex.
                  */
                  case ')':
                     cb->closesGroup = TRUE;
                     (*regexStr)++;
                  case '\0':     // It's the end of the whole regex.
                  case '|':      // If it's a regex control char, then we end the current char list.
                  case '?':
                  case '*':
                  case '+':
                  case '{':      // Opening a repeat specifier (for the previous char/class/group).
                  case '(':
                     if( !bumpIfEmpty(cb) )                             // If necessary, advance to an open 'Null' char-box.
                        { return FALSE; }                               // Return fail if didn't count and malloc() enuf S_Chars in prescan.
                     sgs[cb->put].opcode = OpCode_Match;                // Write a '_Match' terminator to this empty box.
                     cb->len = cb->put+1;
                     return TRUE;

                  case ']':      // Char class close without open
                  case '}':      // Repeat count close without open
                     return FALSE;

                  case '[':      // Opens a char class.
                     // First, if this char-class is preceded by a chars-segment AND followed by a repeat-specifier,
                     // then we must close out the current Chars-Box and start a new one for the (repeated)
                     // char-class. This because the repeat binds only the preceding class.
                     if( gotCharsAtLeast(cb) &&                      // Already got a partial chars-segment or whole segment(s), any type?
                         isaRepeat( toClosesClass(*regexStr))+1 )    // ...repeat operator e.g follows this e.g [0-8]{1,4}.
                     {
                        if( !bumpIfEmpty(cb) )                       // If necessary, advance to an open 'Null' char-box.
                           { return FALSE; }                         // Return fail if didn't count and malloc() enuf S_Chars in prescan.
                        sgs[cb->put].opcode = OpCode_Match;          // ...and terminate the CBox.
                        cb->len = cb->put+1;
                        return TRUE;                                 // return the new Chars-Box WITHOUT advancing '*regexStr'...
                     }
                     else
                     {
                        if( !bumpIfEmpty(cb) )                       // If necessary, advance to an open 'Null' char-box.
                           { return FALSE; }                         // Return fail if didn't count and malloc() enuf S_Chars in prescan.
                        sgs[cb->put].opcode = OpCode_Class;          // ...(which will) hold a character class.

                        if( (sgs[cb->put].payload.charClass = CharClass_New(cl)) == NULL  )    // Made a fresh  empty char class into char-box payload?
                           { return FALSE; }                         // Nope! malloc() failed.
                        else
                           { classParser_Init(&parseClass); }        // else got a new class. Also need a parser to load it.
                        break;
                     }

                  case '^':
                  case '$':
                     if( !bumpIfEmpty(cb) )                          // If necessary, advance to an open 'Null' char-box.
                        { return FALSE; }                            // Return fail if didn't count and malloc() enuf S_Chars in prescan.
                     sgs[cb->put].opcode = OpCode_Anchor;            // ...(which will) hold an anchor.
                     sgs[cb->put].payload.anchor.ch = ch;            // This is the anchor char;
                     if( !wrNextSeg(cb, OpCode_Null))                // Advance and clean out next char-segment.
                        {return FALSE; }
                     break;

                  case '\\':     // e.g '\d','\w', anything which wasn't captured by translateEscapedWhiteSpace() (above).
                     // First, if this escaped char-class is preceded by a chars-segment AND followed by a repeat-specifier,
                     // then we must close out the current Chars-Box and start a new one for the (repeated)
                     // char-class. This because the repeat binds only the preceding class.
                     if( gotCharsAtLeast(cb) &&                      // Already got a partial chars-segment or whole segment(s), any type?
                         isaRepeat( (*regexStr)+2 ))                 // ...repeat operator follows this e.g '\d+'.
                     {                                               // then repeat operator applies just to the escaped \\. Finish the Chars-Box we have so far....
                        if( !bumpIfEmpty(cb) )                       // If necessary, advance to an open 'Null' char-box.
                           { return FALSE; }                         // Return fail if didn't count and malloc() enuf S_Chars in prescan.
                        sgs[cb->put].opcode = OpCode_Match;          // ...and terminate the CBox.
                        cb->len = cb->put+1;
                        return TRUE;                                 // return the new Chars-Box WITHOUT advancing '*regexStr'...
                     }                                               // ...the escape, e.g '\d' will go into its own Box, following a '_Split' which holds it's repeat count..
                     else                                            // else we add to the current Chars-Box
                     {
                        if( handleEscapedNonWhtSpc(cl, cb, *(++(*regexStr)) ) == FALSE)   // Was not a legal escaped thingy?
                           { return FALSE; }                         // then parse fail.
                        else
                           { break; }                                // else success; the escaped thingy is added to current Chars-Box.
                     }

                  default:       // Either non-control char e.g 'a', or CR,LF,TAB, emitted by translateEscapedWhiteSpace() (above)
                     if(!isprint(ch))                                // Non-printable?
                     {                                               // this means it's a whitespace ASCII code, translated by translateEscapedWhiteSpace() above
                        if( !bumpIfEmpty(cb) )                       // If necessary, advance to an open 'Null' char-box.
                           { return FALSE; }                         // Return fail if didn't count and malloc() enuf S_Chars in prescan.

                        sgs[cb->put].opcode = OpCode_EscCh;          // and make an EscCh instruction
                        sgs[cb->put].payload.esc.ch = ch;            // Put ASCII code in 'OpCode_EscCh'.
                        if( !wrNextSeg(cb, OpCode_Null))
                           {return FALSE; }
                     }
                     else                                            // else printable
                     {
                        if(sgs[cb->put].opcode == OpCode_Chars)      // We are already building a chars segment?
                        {
                           /* Before adding this latest char to the existing segment, check the next char(s).
                              If these specify a repeat i.e '?','*','+'or '{n,n}', then this current char
                              must go into a Box of it's own because a repeat' applies to the previous
                              char alone.
                           */
                           C8 const * rr = (*regexStr)+1;

                           if(isaRepeat(rr))                         // A repeat-operator e.g '+' follows the current char?
                           {                                         // then finish the Box we have so far.
                              if( !wrNextSeg(cb, OpCode_Match))      // Add 'Match' terminator. No?
                                 {return FALSE; }                    // cuz prescan did't reserve enuf heap. Compile fails.
                              cb->len = cb->put+1;                   // else success. CBox length is now this.
                              return TRUE;                           // '*regexStr' is not advanced; so current char will be pending and go into it's own Box.
                           }
                           else                                      // else next char is not a repeat-operator.
                           {
                              sgs[cb->put].payload.chars.len++;      // So add current char to segment; by incrementing segment length.
                           }
                        }
                        else                                         // else this opcode is Null, meaning empty.
                        {
                           sgs[cb->put].opcode = OpCode_Chars;            // so start a new chars segment in it.
                           sgs[cb->put].payload.chars.start = *regexStr;  // Segment starts here.
                           sgs[cb->put].payload.chars.len = 1;            // Just 1 the current char so far.
                        }
                     }
               }
               break;

            case OpCode_Class:                        // --- Parsing a character class definition e.g '[0-9A-.....'
               if( (rtn = classParser_AddCh(&parseClass, sgs[cb->put].payload.charClass, &ch)) == E_Fail)
               {
                  errPrint("unterminated char class\r\n");
                  return FALSE;
               }
               else if(rtn == E_Complete)
               {
                  if( !wrNextSeg(cb, OpCode_Null))      // then finish the Box we have so far.
                     {return FALSE; }
               }
               break;

            case OpCode_EscCh:                        // --- Should never see this; state engine should always move on to _Null.
            default:
               return FALSE;
         }
         (*regexStr)++;
      }

   }
   return TRUE;
}

/* ------------------------ Add operation(s) to program -----------------------------

   Append each of the operations below to 'p'. Each operation encode to one or more
   instructions for the non-finite automaton (NFA) which executes the regex.

   'jmpRefleft' and 'jmpRelRight' are jump offsets.

   The 'put' is advanced to the next free instruction slot.
*/

PRIVATE void clearRepeats(S_RepeatSpec *r) {r->min = r->max = 0; r->valid = FALSE; }

PRIVATE S_CharsBox const emptyCharsBox =
   {.segs = NULL, .put = 0, .len = 0, .opensGroup = FALSE, .closesGroup = FALSE, .eatUntilMatch = FALSE };


PRIVATE void addNOP(S_Program *p)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];
   ins->opcode = OpCode_NOP;

   ins->charBox = emptyCharsBox;
   ins->left = 0; ins->right = 0;     // These won't be used but be tidy.
   clearRepeats(&ins->repeats);
   p->instrs.put++;
}

PRIVATE void addSplitAbs(S_Program *p, T_InstrIdx at, T_InstrIdx jmpAbsLeft, T_InstrIdx jmpAbsRight)
{
   S_Instr *ins = &p->instrs.buf[at];

   ins->opcode = OpCode_Split;
   ins->charBox = emptyCharsBox;
   ins->left  = jmpAbsLeft;
   ins->right = jmpAbsRight;
   clearRepeats(&ins->repeats);

   if(at == p->instrs.put)
      { p->instrs.put++; }
}

PRIVATE void addSplit(S_Program *p, S16 jmpRelLeft, S16 jmpRelRight)
    { addSplitAbs(p, p->instrs.put, U16plusS16_toU16(p->instrs.put, jmpRelLeft), U16plusS16_toU16(p->instrs.put, jmpRelRight)); }


 PRIVATE void addSplit_wRepeats(S_Program *p, S16 jmpRelLeft, S16 jmpRelRight, S_RepeatSpec const *r)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];

   ins->opcode = OpCode_Split;
   ins->charBox = emptyCharsBox;
   ins->left  = U16plusS16_toU16(p->instrs.put, jmpRelLeft);
   ins->right = U16plusS16_toU16(p->instrs.put, jmpRelRight);
   ins->repeats.min = r->min;
   ins->repeats.max = r->max;
   ins->repeats.valid = TRUE;
   p->instrs.put++;
}

PRIVATE void addJumpAbs(S_Program *p, T_InstrIdx at, T_InstrIdx jmpAbs)
{
   S_Instr *ins = &p->instrs.buf[at];

   ins->opcode = OpCode_Jmp;
   ins->charBox = emptyCharsBox;
   ins->left = jmpAbs;
   ins->right = p->instrs.put;
   clearRepeats(&ins->repeats);

   if(at == p->instrs.put)
      { p->instrs.put++; }
}

PRIVATE void addJump(S_Program *p, S16 jmpRel)
   { addJumpAbs(p, p->instrs.put, U16plusS16_toU16(p->instrs.put,jmpRel)); }


PRIVATE void addFinalMatch(S_Program *p)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];

   ins->opcode = OpCode_Match;
   ins->charBox = emptyCharsBox;
   ins->left = 0; ins->right = 0;     // These won't be used but be tidy.
   clearRepeats(&ins->repeats);
   p->instrs.put++;
}

PRIVATE S16 prevCBox(S_Program *p)
{
   T_InstrIdx i;

   for(i = p->instrs.put; i; i--) {
      if(p->instrs.buf[i].opcode == OpCode_CharBox )
         { break; }}
   return i - p->instrs.put;
}

/* ------------------------------ attachCharBox --------------------------------------

   If 'cb' is a non-empty chars-list, then append a 'CharBox instruction to 'p',
   COPY 'cb' into the new instruction. Clear 'cb' (so it won't be re-used).
*/

PRIVATE void attachCharBox(S_Program *p, S_CharsBox *cb)
{
   if(cb != NULL)                                        // 'cb' exists?
   {
      if(cb->len != 0)                                   // 'cb' has content?
      {
         S_Instr *ins = &p->instrs.buf[p->instrs.put];   // then will make a new instruction at the current 'put'

         ins->opcode = OpCode_CharBox;                   // This instruction is a 'CharBox'.
         ins->charBox = *cb;                             // and this is the chars-list is contains.

         // Clear branches and repeats; they are not used.
         ins->left = 0; ins->right = 0;
         clearRepeats(&ins->repeats);

         // If this CharBox opens or closes a subgroup, then add that attribute to the instruction.
         if(cb->opensGroup == TRUE)
            { ins->opensGroup = TRUE; }

         if(cb->closesGroup == TRUE)
            { ins->closesGroup = TRUE; }

         p->instrs.put++;                                // Advance 'Put' to next instruction
         p->chars.put += cb->len;                        // Also bump 'put' for the chars-group store to next free.

         cb->len = 0;                                    // Empty 'cb' so it won't be reused by a later attachCharBox().
      }
   }
}

/* ----------------------------- lookaheadFor_GroupClose ------------------------------------ */


PRIVATE S_CharsBox * lookaheadFor_GroupClose(S_CharsBox *cb, C8 const *rs)
{
   if(*(rs + 1) == ')')
      { cb->closesGroup = TRUE; }
   return cb;
}

/* ------------------------------ Stack for Split closures -------------------------------------

   For e.g  '\\d{5}(-\\d{4})?'. This stack holds an instruction slot for a Split at the opening
   '(' until we reach the closing ')?' when we can fill the open slot to bypass instructions
   between the '(' and the ')'.
*/
#define _NOPStackSize 4
typedef struct {T_InstrIdx s[_NOPStackSize]; U8 put; } S_NOPs;
PRIVATE S_NOPs nops;

#define _NotANOP 0xFF
PRIVATE void nops_Init(void)
   { nops.put = 0; }

PRIVATE BOOL nops_Push(T_InstrIdx ni) {
   if(nops.put >= _NOPStackSize) {
      return FALSE; }
   else {
      nops.s[nops.put++] = ni;
      return TRUE; }}

PRIVATE T_InstrIdx nops_Pop(void) {
   return nops.put == 0
            ? _NotANOP
            : nops.s[--nops.put]; }

/* ----------------------------- regexlt_compileRegex ---------------------------------

   Compile 'regexStr' into a 'prog'. The program consists of instructions. Each
   instruction has an opcode, maybe one or two jumps and maybe a sequence of
   characters and character classes from the source string.
*/
PUBLIC BOOL regexlt_compileRegex(S_Program *prog, C8 const *regexStr)
{
   C8 const *rgxP = regexStr;          // From the start of the regex.
   C8 const *segStart;                 // Pins the start of a character segment.
   BOOL forked = FALSE;                // Until we meet and alternate '|'
   BOOL eatYet = FALSE;
   U8 boxesToRight;
   BOOL ate1st = FALSE;
   BOOL gotCharBox = FALSE;

   prog->instrs.put = 0;               // Zero 'put's for the instruction and character buffers.
   prog->chars.put = 0;
   prog->classes.put = 0;
   C8 firstOp = 0;

   /* A new zero-length Chars-Box. Attach to the malloced 'prog.chars.buf'. Copy in 'bufSize'; used
      to check we don't overrun the malloc(), in case the pre-scan under-counted the number of
      Chars-segments.
   */
   S_CharsBox cb = {.segs = prog->chars.buf, .bufSize = prog->chars.size, .len = 0 };

   S_RepeatSpec   rpt;
   T_InstrIdx     rightFork;
   T_InstrIdx m;

   nops_Init();

   while(1)                            // Until end-of-regex or there's a compile error.
   {
      if(!isprint(*rgxP) && *rgxP != '\0')               // Regex string contains a non-printable?
      {
         return FALSE;                                   // Fail! Regex have only printable chars
      }
      else                                               // else it's a char which is legal somewhere in a regex. OR end-of-string
      {
         switch(*rgxP)                   // --- Wot is it?
         {
            case '\0':                                // --- End of regex
               if(forked == TRUE)                        // But were waiting for char-right of an alternate?
               {
                  return FALSE;                          // So parse fail
               }
               else                                      // else compile is complete
               {
                  attachCharBox(prog, &cb);              // Attach the last CharBox we made (below)
                  addFinalMatch(prog);                   // 'Match' terminates the program
                  return TRUE;                           // Success!
               }
               break;

            case '?':                                 // --- Zero or one
               if( (m = nops_Pop()) != _NotANOP)            // The 'open' for this '?' was back somewhere. We left a NOP, ready to fill
                  { addSplitAbs(prog, m, m+1, prog->instrs.put); }   // so fill that NOP; either try succeeding Chars-Boxes or skip them.
               else                                         // else the open for this '?' is just the Chars_box we are about to attach.
                  { addSplit(prog, +1, +2); }               // so either try next or skip it..

               attachCharBox(prog,                          // This is 'next'
                  lookaheadFor_GroupClose(&cb, rgxP));      // If ')' after the '?' then this CharBox is/ends a subgroup. Close the subgroup.

               if(prog->instrs.put == 1 &&                  // The Chars-Box which preceded this '*' (zero-or-) was the 1st? AND
                  prog->instrs.buf[0].charBox.eatUntilMatch == TRUE)  // that CBox was an 'eatUntilMatch'?
                  { eatYet = TRUE; }                        // then the CBox to right of '*' will be too. - because it's a 'zero-or'.
               rgxP++;
               break;

            case '*':                                 // --- Zero or more
               if(cb.len == 0)                              // No CBox to add? (was closed out by a preceding group)
                  { addSplit(prog, +1, +2); }               // Either try next JMP or skip it
               else {                                       // else there is a CBox
                  addSplit(prog, +1, +3);                   // Either try that CBox repeatedly or skip past the JMP which is beyond it
                  attachCharBox(prog,                       // This is 'next'.
                     lookaheadFor_GroupClose(&cb, rgxP)); } // If ')' after the '*' then close current subgroup at the CharBox.

               addJump(prog, MinS16(-2, prevCBox(prog)));   // then JMP back to retry the previous CBox (but not the one we added, which is '-1')

               if(prog->instrs.put == 1 &&                  // The Chars-Box which preceded this '*' (zero-or-) was the 1st? AND
                  prog->instrs.buf[0].charBox.eatUntilMatch == TRUE)  // that 1st CBox was an 'eatUntilMatch'?
                  { eatYet = TRUE; }                        // then the one right of '*' will be too. - because it's a 'zero-or'.
               rgxP++;
               break;

            case '+':                                 // --- One or more
               attachCharBox(prog,                       // Always try this once...
                  lookaheadFor_GroupClose(&cb, rgxP));   // If ')' after the '+' then close current subgroup at the CharBox.
               addSplit(prog, -1, +1);                   // then either move on or retry.
               rgxP++;
               break;

            case '|':                                 // --- Alternates
               if( (m = nops_Pop()) != _NotANOP)         // The 'open' for the right of this '|' was back somewhere. We left a NOP, ready to fill
                  { addSplitAbs(prog, m, m+1, prog->instrs.put+2); } // so fill that NOP; either try succeeding Chars-Boxes or skip them.
               else
                  { addSplit(prog, +1, +3); }            // Split the execution path; fork-left is next; fork-right is after Jmp.

               attachCharBox(prog, &cb);                 // Attach fork-left ... which we parsed before reaching '|'. Goto next free bytecode slot.
               forked = TRUE;                            // Mark that we forked; so we know when we finally get code-right.
               boxesToRight = 0;                         // Will count CharBoxes to right of '|' which are arguments or that '|'. So can JMP past them.
               rgxP++;                                   // Goto next char past '|'.
               break;

            case '{':                                 // --- Opens a repeat specifier e.g {2,5} (match preceding 2 to 5 times}
               if(parseRepeat(&rpt, &rgxP) == E_Fail)    // Was not any of e.g {3}, {3,} or {3,5}?
               {
                  return FALSE;                          // then regex is malformed.
               }
               else                                      // else got a repeat specifier.
               {
                  addSplit_wRepeats(prog, +1, +3, &rpt); // Write instructions as for 'zero-or-more', but with repeat specifiers non-zero.

                  if(*rgxP == ')')
                     {cb.closesGroup = TRUE;}
                  attachCharBox(prog, &cb );
                  addJump(prog, -2);                     // then back to retry or move on.
                  break;
               }

            case ')':                                 // --- Closing a subgroup (after an operator, since it wasn't captured by fillCharBox(), below)
               rgxP++;                                   // then the ')' is superfluous, since the preceding operator implicitly closed the subgroup
               break;                                    // So just boof past the ')'.

            default:                                  // --- (else) a regex literal char

               /* This regex literal is the first of a char group. That's because fillCharBox(), below, eats
                  all of a contiguous sequence of chars (and char classes). So, to get back here, this
                  regex literal must have been preceded by something which was not a regex literal.

                  So we give this first literal to fillCharGroup() which will eat all contiguous literals,
                  escapes and classes and assemble them into a chars-group. This leaves us at the next non-char
                  which will be handled by one of the cases above.
               */
               if(gotCharBox)                            // But but, if this follows a chars-list which closes a subgroup?
               {
                  gotCharBox = FALSE;
                  attachCharBox(prog, &cb);              // then load that chars-list before assembling the next.
               }

               cb = emptyCharsBox;                       // Make a new S_CharsBox for fillCharBox() to fill.
               cb.segs = &prog->chars.buf[prog->chars.put];  // Give it the next free space from what was malloced for S_CharsBox[]
               cb.bufSize = prog->chars.size - prog->chars.put;

               segStart = rgxP;                          // Mark start of this segment; we will

               /* If next chars segment starts a group? '(' then it may be this first of multiple
                  Chars-Boxes spanning multiple clauses. If the right-operator is '|' or '?' then
                  these clauses will be preceded by a 'Split'. We must leave a placeholder for the Split until
                  we reach the close ')' for this open. Then we can fill the Split to execute or bypass
                  everything between the '(' and the ')'.
               */
               if(*rgxP == '(')                          // Next chars segment starts a group? ... which may have multiple sub-clauses.
               {                                         // If next operator is '|' or '?' then we must leave a placegolder for a 'Split'...
                  C8 ch = rightOperator(rgxP);
                  if(ch == '|' || ch == '?')                   // Next operator (somewhere to the right) is '|' or '?'?
                  {
                     if(nops_Push(prog->instrs.put) == FALSE)  // then push a mark for this this spot.
                        { return FALSE; }                      // Couldn't push? Fail.
                     else
                        { addNOP(prog); }                      // and reserve a slot ofter the 'SPlit' which be inserted when we reach the '|'.
                  }
               }

               if( fillCharBox(&prog->classes, &cb, &rgxP) == FALSE)   // Got (contiguous) chars into 'cb'?
               {
                  return FALSE;                          // No, parse error.. Fail
               }
               else                                      // else 'cb' has the chars (and 'rs' is advanced to next un-read input)
               {
                  gotCharBox = TRUE;                     // Mark that there's an assembled Box.

                  if(firstOp == 0)                             // Didn't already get the 1st right operator?
                     { firstOp = rightOperator(segStart); }    // then find it now.

                  /* If 1st operator is '|' (alternation) then, because '|' is greedy (left and right), we must
                     dump mismatches to the 1st of multiple CharBox to the left of '|' until we match all
                     CharBoxs (to the left of '|').  e.g
                                          '(do)g|cat'  vs   dotty_dog
                     must not fail on 'dotty' but must continue past 'tty_' to 'dog'. 'eatUntilMatch' instructs
                     the run-engine to eat leading mismatches to 'do'.

                     Also eat leading mismatches of each part of multiple alternations e.g
                              'dog|cat|mouse'   matches 'adog', 'acat' and ''amouse'
                     We must also dump leading mismatches if the first char/char-class has no controlling operator.
                     For example:
                                          'def'    vs 'abcdefg'
                                          'def'   vs 'abcdefgh'      - regex has no operators at all
                                          'de+f   vs 'abcdeeefgh'    - 'd' is not linked to an operator.
                     BUT NOT if there's an explicit wildcard i.e:
                                          '.*def'  vs 'abcdefg'
                     Here the wildcard matches 'abc' and the match starts at 'a', not 'd'.
                  */
                  if( (prog->instrs.put == 0 && *segStart != '.' && *segStart != '^') ||    // 1st char AND it's not a wildcard or an anchor? OR
                      (
                        rightOperator(segStart) == '|' &&         // operator to right is alternation? AND...
                        firstOp == '|' &&                         // ... this '|' was not preceded by any different operator? AND
                        ate1st == FALSE ))                        // this is 1st char block to left of 1st '|'? (else 'ate1st' would be TRUE)
                     {
                        cb.eatUntilMatch = TRUE;                  // then this CharBox will eat leading mismatches.
                        ate1st = TRUE;                            // We found leftward-est block, subsequent block left of 1st '|' do not 'eat'.
                     }

                  if(forked == TRUE)                              // Were these chars-right of '|'?
                  {
                     if(boxesToRight == 0)                        // This was the 1st box after '|'?
                     {
                        if( firstOp == '|')                       // and that '|' was 1st operator?
                           { cb.eatUntilMatch = TRUE; }           // then eat leading mismatches for this box.

                        rightFork = prog->instrs.put;             // Mark the where we forked...
                        addNOP(prog);                             // and reserve a slot for the JMP. To be inserted later when we figure how far right the '|' extends.
                     }

                     if( rightOperator(segStart) == '$' )         // Right fork extends to end of input string?
                     {                                            // then jump past the 1st JMP and the right-fork Char-Box after it (+2)
                        addJumpAbs(prog, rightFork, rightFork + boxesToRight + 2 );
                        forked = FALSE;                           // and the fork is done.
                     }
                     else                                         // else right fork does NOT extend to end of input.
                     {                                            // then, because '|' is greedy, we need to keep adding content...
                        boxesToRight++;                           // until end-of-input or another '|'.
                     }
                  }

                  if(eatYet == TRUE) {
                     eatYet = FALSE;
                     cb.eatUntilMatch = TRUE; }

                  if(cb.opensGroup)                               // This CharBox opens a subgroup?
                  {
                     C8 ch = rightOperator(rgxP);
                     if(ch == '|' || ch == '?')                   // Next operator (somewhere to the right) is '|'?
                     {
                        if(nops_Push(prog->instrs.put) == FALSE)  // then push a mark for this this spot.
                           { return FALSE; }                      // Couldn't push? Fail.
                        else
                           { addNOP(prog); }                      // and reserve a slot ofter the 'SPlit' which be inserted when we reach the '|'.
                     }
                  }

                  if(*rgxP == '(')                                // Next char opens a new subgroup?
                     { attachCharBox(prog, &cb); }                // then attach the existing newly-made chars-list now; don't have to wait for a post-operator.
                  break;
               }
         }
      }
   }        //while(1)
   return TRUE;
}


// ---------------------------------------------- eof --------------------------------------------------
