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
#include "regexlt_private.h"

// Private to RegexLT_'.
#define classParser_Init   regexlt_classParser_Init
#define classParser_AddDef regexlt_classParser_AddDef
#define classParser_AddCh  regexlt_classParser_AddCh
#define getCharClassByKey  regexlt_getCharClassByKey
#define parseRepeat        regexlt_parseRepeat
#define dbgPrint           regexlt_dbgPrint


/* --------------------------------- CharClass_New ----------------------------------- */

PUBLIC S_C8bag * CharClass_New(S_ClassesList *h)
{
   memset(&h->ccs[h->put], 0, sizeof(S_C8bag));
   return &h->ccs[h->put++];
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

/* ------------------------------ handleEscapedNonWhtSpc ---------------------------

   Handle the following one-character escaped chars:

      - escaped regex control chars e.g '\?', '\*'.
            These are made into their corresponding single-char single char literals
            ('?', '*') inside an 'OpCode_EscCh'.

      - predefined character classes e.g \d == [0-9]
            These are put into a character class which is linked to a (new) OpCode_Class.

   Return FALSE if 'ch' isn't one of the above. If success, then 'idx' is advanced
   to the next S_Chars slot, which is written to 'OpCode_Null'
*/

PRIVATE C8 const escapedChars[] = "\\|.*?{}()";

PRIVATE BOOL handleEscapedNonWhtSpc(S_ClassesList *cl, S_Chars *cb, T_InstrIdx *idx, C8 ch)
{
   C8 const    *preBakedClass;
   BOOL        rtn = FALSE;                     // Until we succeed below.
   T_InstrIdx  i = *idx;                        // Current cb[i].

   if(cb[i].opcode != OpCode_Null)                                                  // Not already on an empty 'S_Chars'.
      { cb[++i].opcode = OpCode_Null; }                                             // then advance to next slot. Make sure it's clean; it will likely be filled below.

   if(strchr(escapedChars, ch) != NULL)                                             // Literal of a regex control char?
   {
      cb[i].opcode = OpCode_EscCh;                                                  // will go in this next cb[i]
      cb[i].payload.esc.ch = ch;                                                    // the char s this
      cb[++i].opcode = OpCode_Null;                                                 // Clear the next S_Chars slot (being tidy)
      rtn = TRUE;                                                                   // and we succeeded
   }
   else if((preBakedClass = getCharClassByKey(ch)) != NULL)                         // else a predefined char class e.g '\w'
   {
      S_ParseCharClass parseClass;

      if( (cb[i].payload.charClass = CharClass_New(cl)) != NULL  ) {                // Obtained a fresh empty class?, in the next cb[i]
         classParser_Init(&parseClass);                                             // Start the private parser.
                                                                                    // Success adding char class? (should be, the class was pre-baked by us)
         if( classParser_AddDef(&parseClass, cb[i].payload.charClass, preBakedClass) == TRUE ) {
            cb[i].opcode = OpCode_Class;                                            // then label wot we made.
            cb[++i].opcode = OpCode_Null;                                           // Next instruction cleaned to 'Null'.
            rtn = TRUE; }}                                                          // and Success.
   }
   *idx = i;                        // Instruction index likely advanced; write it back.
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

PRIVATE BOOL fillCharBox(S_ClassesList *cl, S_CharsBox *cBox, C8 const **regexStr)
{
   C8 ch;
   S_ParseCharClass parseClass;
   T_ParseRtn rtn;

   S_Chars * cb = cBox->buf;
   T_InstrIdx idx = 0;                 // Fill 'cb' starting at cBox->buf[0].

   /* If got a '(' rightaway. then this chars-list either opens a subgroup or will be an
      an entire subgroup.
   */
   if(**regexStr == '(')
   {
      cBox->opensGroup = TRUE;
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

         switch(cb[idx].opcode)
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
                     cBox->closesGroup = TRUE;
                     (*regexStr)++;
                  case '\0':     // It's the end of the whole regex.
                  case '|':      // If it's a regex control char, then we end the current char list.
                  case '?':
                  case '*':
                  case '+':
                  case '{':      // Opening a repeat specifier (for the previous char/class/group).
                  case '(':
                     if(cb[idx].opcode != OpCode_Null) idx++;        // If necessary, advance to an open 'Null' char-box.
                     cb[idx].opcode = OpCode_Match;                  // Write a '_Match' terminator to this empty box.
                     cBox->len = idx+1;
                     return TRUE;

                  case ']':      // Char class close without open
                  case '}':      // Repeat count close without open
                     return FALSE;

                  case '[':      // Opens a char class.
                     if(cb[idx].opcode != OpCode_Null) idx++;        // If necessary, advance to an open 'Null' char-box....
                     cb[idx].opcode = OpCode_Class;                  // ...(which will) hold a character class.

                     if( (cb[idx].payload.charClass = CharClass_New(cl)) == NULL  )    // Made a fresh  empty char class into char-box payload?
                        { return FALSE; }                            // Nope! malloc() failed.
                     else
                        { classParser_Init(&parseClass); }           // else got a new class. Also need a parser to load it.
                     break;

                  case '\\':     // e.g '\d','\w', anything which wasn't captured by translateEscapedWhiteSpace() (above).
                     if(cb[idx].opcode == OpCode_Chars &&            // Already building a Chars-Box? AND
                        isaRepeat( (*regexStr)+2 ))                  // Repeat operator e.g '+' or '{3}' follows this e.g '\d'.
                     {
                        cb[++idx].opcode = OpCode_Match;             // then finish the Chars-Box we have so far....
                        cBox->len = idx+1;
                        return TRUE;                                 // return the new Chars-Box WITHOUT advancing '*regexStr'...
                     }                                               // ...the escape, e.g '\d' will go into its own Box, following a '_Split' which holds it's repeat count..
                     else                                            // else we add to the current Chars-Box
                     {
                        if( handleEscapedNonWhtSpc(cl, cb, &idx, *(++(*regexStr)) ) == FALSE)   // Was not a legal escaped thingy?
                           { return FALSE; }                         // then parse fail.
                        else
                           { break; }                                // else success; the escaped thingy is added to current Chars-Box.
                     }

                  default:       // Either non-control char e.g 'a', or CR,LF,TAB, emitted by translateEscapedWhiteSpace() (above)
                     if(!isprint(ch))                                // Non-printable?
                     {                                               // this means it's a whitespace ASCII code, translated by translateEscapedWhiteSpace() above
                        if(cb[idx].opcode != OpCode_Null) idx++;     // If necessary, advance to an open 'Null' char-box.

                        cb[idx].opcode = OpCode_EscCh;               // and make an EscCh instruction
                        cb[idx++].payload.esc.ch = ch;               // Put ASCII code in 'OpCode_EscCh'.
                        cb[++idx].opcode = OpCode_Null;              // Advance to an open 'Null slot'.
                     }
                     else                                            // else printable
                     {
                        if(cb[idx].opcode == OpCode_Chars)           // We are already building a chars segment?
                        {
                           /* Before adding this latest char to the existing segment, check the next char(s).
                              If these specify a repeat i.e '?','*','+'or '{n,n}', then this current char
                              must go into a Box of it's own because a repeat' applies to the previous
                              char alone.
                           */
                           C8 const * rr = (*regexStr)+1;

                           if(isaRepeat(rr))                         // A repeat-operator e.g '+' follows the current char?
                           {
                              cb[++idx].opcode = OpCode_Match;       // then finish the Box we have so far.
                              cBox->len = idx+1;
                              return TRUE;                           // '*regexStr' is not advanced; so current char will be pending and go into it;s own Box.
                           }
                           else                                      // else next char is not a repeat-operator.
                           {
                              cb[idx].payload.chars.len++;           // So add current char to segment; by incrementing segment length.
                           }
                        }
                        else                                         // else this opcode is Null, meaning empty.
                        {
                           cb[idx].opcode = OpCode_Chars;            // so start a new chars segment in it.
                           cb[idx].payload.chars.start = *regexStr;  // Segment starts here.
                           cb[idx].payload.chars.len = 1;            // Just 1 the current char so far.
                        }
                     }
               }
               break;

            case OpCode_Class:                        // --- Parsing a character class definition e.g '[0-9A-.....'
               if( (rtn = classParser_AddCh(&parseClass, cb[idx].payload.charClass, ch)) == E_Fail)
               {
                  printf("unterminated char class\r\n");
                  return FALSE;
               }
               else if(rtn == E_Complete)
               {
                  cb[++idx].opcode = OpCode_Null;
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
   {.buf = NULL, .len = 0, .opensGroup = FALSE, .closesGroup = FALSE, .eatUntilMatch = FALSE };


PRIVATE void addNOP(S_Program *p)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];
   ins->opcode = OpCode_NOP;

   ins->charBox = emptyCharsBox;
   ins->left = 0; ins->right = 0;     // These won't be used but be tidy.
   clearRepeats(&ins->repeats);
   p->instrs.put++;
}

PRIVATE void addSplit(S_Program *p, S16 jmpRelLeft, S16 jmpRelRight)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];

   ins->opcode = OpCode_Split;
   ins->charBox = emptyCharsBox;
   ins->left  = U16plusS16_toU16(p->instrs.put, jmpRelLeft);
   ins->right = U16plusS16_toU16(p->instrs.put, jmpRelRight);
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
}

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

PRIVATE void addJump(S_Program *p, S16 jmpRel)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];

   ins->opcode = OpCode_Jmp;
   ins->charBox = emptyCharsBox;
   ins->left = U16plusS16_toU16(p->instrs.put, jmpRel);
   ins->right = p->instrs.put;
   clearRepeats(&ins->repeats);
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
}

PRIVATE void addFinalMatch(S_Program *p)
{
   S_Instr *ins = &p->instrs.buf[p->instrs.put];

   ins->opcode = OpCode_Match;
   ins->charBox = emptyCharsBox;
   ins->left = 0; ins->right = 0;     // These won't be used but be tidy.
   clearRepeats(&ins->repeats);
   p->instrs.put++;
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

/* ---------------------------- rightOperator -----------------------------

   Given a regex string 'rgx', return the char of the controlling operator for rgx[0]
   - the 1st char.

   Returns:
      - if there is no controlling operator, 'E' (meaning End).
      - if no operators before end-of-string, '$'
      - otherwise, '+','*','?','|' or '{', meaning a char class.

   Examples:
      'a?bc'      -> '?'
      'abc'       -> '$'       (end of string)
      'abc(def)+' ->  E       'abc' has no rightward operator; it's not controlled by the '+'.
      'ab+c'      ->          '+' does not apply to 'a'
      '(ab)*c     -> '*'      '*' applies to the 1st subgroup.
      'dog|cat'   -> '|'      ('|' is right-associative, applies to all of 'dog'.
      'a(dog|cat) ->  E       '|' does no apply to 'a'.
*/



PRIVATE BOOL isPostOpCh(C8 ch)
   { return ch == '{' || ch == '?' || ch == '*' || ch == '+'; }


PUBLIC C8 rightOperator(C8 const *rgx)
{
   C8 const *rp = rgx;
   C8 ch;
   U8 grpCnt, inSub;                 // Counts chars, char-classes and groups for purposes of operator left-associativity.
   BOOL esc, inClass, inCh;

   for(grpCnt = 0, esc = FALSE, inClass = FALSE, inCh = FALSE, inSub = 0; *rp != '\0'; rp++) // Until the end of regex maybe.
   {
      ch = *rp;

      if(ch == '\\') {                          // Backslash?
         esc = !esc;                            // If prev char was NOT backslash, next char will be escaped, and vice versa.
         if(esc) {                              // Escaped now?
            continue;}}                         // then continue to the next (escaped) char

      if(esc){                                  // This char is escaped?
         esc = FALSE;                           // then cancel escape; it applies just to this char, which we process now.
         if(!inCh) {                            // Starting char sequence?
            inCh = TRUE;
            grpCnt++;                           // then we have one more char-group
         }
         else {                                 // otherwise already in char-sequence.
            if( isPostOpCh(*(rp+1))) {          // Next char is a post-op, '+','*' etc?
               grpCnt++; }}}                    // then post-operator binds this char, so previous chars make one more char-group.
      else {                                    // else this char is not escaped; must look for groups, classes etc.
         if(inClass) {                          // In a '[A-Z]'?
            if(ch == ']') {                     // Closing ']'?
               inClass = FALSE;                 // then we are out of the class-specifier.
               if( isPostOpCh(*(rp+1)))         // Next char is a post-op, '+','*' etc?
                  { grpCnt++; }}                // then post-operator binds this char-class, so previous chars make one more char-group.
         }
         else {                                 // else not in class specifier
            if(ch == '[') {                     // Opening '['?
               inClass = TRUE;                  // then we are in class specifier now.
            }
            else {
               if(inSub > 0) {                  // else... Nested in subgroup(s)?
                  if(ch == ')'){                // Closing ')'?
                     inSub--;                   // then up one
                     if(inSub == 0){            // Now out of subgroup(s)?
                        grpCnt++;}              // then exiting to top adds another char-group
                  }
                  else if(ch == '(') {          // Opening?
                     inSub++;                   // then we go deeper
                  }
               }
               else {                           // else didn't enter subgroup(s)
                  if(ch == ')') {               // But, got a ')'? so must have started inside one.
                     if(inSub > 0) {
                        inSub--; }
                  }
                  else if(ch == '(') {          // Opening '('
                     inCh = FALSE;              // then not counting chars any more
                     inSub = 1;                 // we are instead down in a subgroup
                  }
                  else if(ch == '|') {          // Alternation, '|'?
                     return '|';                // Is right associative, so binds to rgx[0], no matter what's in between
                  }
                  else if(isPostOpCh(ch)) {     // else '+','*' etc?
                     return grpCnt > 1          // then, if there's at least one group between rgx[0] and '+'?
                        ? 'E'                   // rgx[0] is 'free'; it's not bound to the post-operator
                        : ch;                   // else rgx[0] is boud to the post-operator, so return the char '+','*' etc.
                  }
                  else {                        // else none of the above.
                     if(!inCh) {                // Starting char sequence?
                        inCh = TRUE;
                        grpCnt++;               // then we have one more char group.
                     }
                     else if( isPostOpCh(*(rp+1))) // else if already in char sequence AND next char is a post-op?
                        { grpCnt++; }           // then post-operator binds this char, so previous chars make one more char-group.
                     } }}}}
   }
   return
      grpCnt > 1     // Passed one or more subgroups on the way to '\0'?
         ? 'E'       // then 'free' 0, 'E'
         : '$';      // else 'free' '$' <> EOS.
}

/* ----------------------------- regexlt_compileRegex ---------------------------------

   Compile 'regexStr' into a 'prog'. The program consists of instructions. Each
   instruction has an opcode, maybe one or two jumps and maybe a sequence of
   characters and character classes from the source string.
*/
PUBLIC BOOL regexlt_compileRegex(S_Program *prog, C8 const *regexStr)
{
   C8 const *rs = regexStr;
   C8 const *ps;
   BOOL forked = FALSE;                // Until we meet and alternate '|'
   U8 boxesToRight;
   BOOL ate1st = FALSE;
   BOOL gotCharBox = FALSE;

   prog->instrs.put = 0;               // Zero 'put's for the instruction and character buffers.
   prog->chars.put = 0;
   prog->classes.put = 0;
   C8 firstOp = 0;

   S_CharsBox cb = {.buf = prog->chars.buf, .len = 0 };

   S_RepeatSpec   rpt;
   #define _NotANOP 0xFF
   T_InstrIdx     nopMark = _NotANOP;
   T_InstrIdx     jmpMark;

   while(1)                            // Until end-of-regex or there's a compile error.
   {
      if(!isprint(*rs) && *rs != '\0')                   // Regex string contains a non-printable?
      {
         return FALSE;                                   // Fail! Regex have only printable chars
      }
      else                                               // else it's a char which is legal somewhere in a regex. OR end-of-string
      {
         switch(*rs)                   // --- Wot is it?
         {
            case '\0':                                // --- End of regex
               if(forked == TRUE)                        // But were waiting for char-right of an alternate?
               {
printf("Barfed here\r\n");
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
               addSplit(prog, +1, +2);                   // Either try next or skip it..
               attachCharBox(prog,                       // This is 'next'
                  lookaheadFor_GroupClose(&cb, rs));     // If ')' after the '?' then this CharBox is/ends a subgroup. Close the subgroup.
               rs++;
               break;

            case '*':                                 // --- Zero or more
               addSplit(prog, +1, +3);                   // Either try next repeatedly or skip.
               attachCharBox(prog,                       // This is 'next'.
                  lookaheadFor_GroupClose(&cb, rs));     // If ')' after the '*' then close current subgroup at the CharBox.
               addJump(prog, -2);                        // then back to retry or move on.
               rs++;
               break;

            case '+':                                 // --- One or more
               attachCharBox(prog,                       // Always try this once...
                  lookaheadFor_GroupClose(&cb, rs));     // If ')' after the '+' then close current subgroup at the CharBox.
               addSplit(prog, -1, +1);                   // then either move on or retry.
               rs++;
               break;

            case '|':                                 // --- Alternates
               if(nopMark != _NotANOP)
               {
                  addSplitAbs(prog, nopMark, nopMark+1, prog->instrs.put+2);
                  nopMark = _NotANOP;
               }
               else
               {
                  addSplit(prog, +1, +3);                // Split the execution path; fork-left is next; fork-right is after Jmp.
               }
               attachCharBox(prog, &cb);                 // Attach fork-left ... which we parsed before reaching '|'. Goto next free bytecode slot.
               forked = TRUE;                            // Mark that we forked; so we know when we finally get code-right.
               boxesToRight = 0;                         // Will count CharBoxes to right of '|' which are arguments or that '|'. So can JMP past them.
               rs++;                                     // Goto next char past '|'.
               break;

            case '{':                                 // --- Opens a repeat specifier e.g {2,5} (match preceding 2 to 5 times}
               if(parseRepeat(&rpt, &rs) == E_Fail)      // Was not any of e.g {3}, {3,} or {3,5}?
               {
                  return FALSE;                          // then regex is malformed.
               }
               else                                      // else got a repeat specifier.
               {
                  addSplit_wRepeats(prog, +1, +3, &rpt); // Write instructions as for 'zero-or-more', but with repeat specifiers non-zero.
                  attachCharBox(prog,                    // This is 'next'.
                     lookaheadFor_GroupClose(&cb, rs));
                  addJump(prog, -2);                     // then back to retry or move on.
                  break;
               }

            case ')':                                 // --- Closing a subgroup (after an operator, since it wasn't captured by fillCharBox(), below)
               rs++;                                     // then the ')' is superfluous, since the preceding operator implicitly closed the subgroup
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
               cb.buf = &prog->chars.buf[prog->chars.put];  // Give it the next free space from what was malloced for S_CharsBox[]

               ps = rs;
               if( fillCharBox(&prog->classes, &cb, &rs) == FALSE)   // Got (contiguous) chars into 'cb'?
               {
                  return FALSE;                          // No, parse error.. Fail
               }
               else                                      // else 'cb' has the chars (and 'rs' is advanced to next un-read input)
               {
                  gotCharBox = TRUE;                     // Mark that there's an assembled Box.
printf("Got CharBox %c %c %d\r\n", *ps, *rs, *rs);
                  if(firstOp == 0)
                     { firstOp = rightOperator(ps); }

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
                  if( (prog->instrs.put == 0 && *ps != '.') ||    // 1st char AND it's not a wildcard? OR
                      (
                        rightOperator(ps) == '|' &&               // operator to right is alternation? AND...
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

                        jmpMark = prog->instrs.put;               // Mark the spot...
                        addNOP(prog);                             // and reserve a slot for the JMP. To be inserted later when we figure how far right the '|' extends.
                     }

                     C8 rop = rightOperator(ps);
printf("rem %s rop %c\r\n", ps, rop);
                     if( rop == '$' )
                     {
                        addJumpAbs(prog, jmpMark, jmpMark + boxesToRight + 2 );
                        forked = FALSE;
                     }
                     else
                     {
                        boxesToRight++;
                     }
                  }

                  if(cb.opensGroup)                               // This CharBox opens a subgroup?
                  {
                     if(rightOperator(rs) == '|')                 // Next operator (somewhere to the right) is '|'?
                     {
                        nopMark = prog->instrs.put;               // then mark this spot
                        addNOP(prog);                             // and reserve a slot ofr the 'SPlit' which be inserted when we reach the '|'.
                     }
                  }

                  if(*rs == '(')                                  // Next char opens a new subgroup?
                  {
                     attachCharBox(prog, &cb);                    // then attach the existing newly-made chars-list now; don't have to wait for a post-operator.
                  }
                  break;
               }
         }
      }
   }        //while(1)
   return TRUE;
}


// ---------------------------------------------- eof --------------------------------------------------
