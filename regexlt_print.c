/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Degug/Development printouts & support.
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
#define dbgPrint  regexlt_dbgPrint


#define TAB 0x09
#define CR  0x0D
#define LF  0x0A


// -------------------------------- regexlt_dbgPrint --------------------------------------

PUBLIC void regexlt_dbgPrint(C8 const *fmt, ...)
{
   va_list argptr;
   va_start(argptr,fmt);

   if(regexlt_cfg->printEnable)
      { vfprintf(stdout, fmt, argptr); }
   va_end(argptr);
}

// ---------------------------------- opcodeNames ----------------------------------------

PUBLIC C8 const *opcodeNames(T_OpCode op)
{
   switch(op) {
      // Program nstruction codes
      case OpCode_NOP:     return "No-Op";
      case OpCode_CharBox: return "CBox ";
      case OpCode_Jmp:     return "Jmp  ";
      case OpCode_Split:   return "Split";

      // Chars-Box (CBox) contents.
      case OpCode_Chars:   return "Chars";
      case OpCode_EscCh:   return "Esc";
      case OpCode_Anchor:  return "Anchor";
      case OpCode_Class:   return "Class";

      // Common to Program and CBox
      case OpCode_Null:    return "Null ";   // Denotes a free space.
      case OpCode_Match:   return "Match";   // Terminates program or CBox.

      default:             return "unknown opcode";
   }
}

/* --------------------------------- printsEscCh --------------------------------------

   May be whitespace or an escaped literal e.g '\['.
*/
PRIVATE C8 const * printsEscCh(C8 ch)
{
   static C8 buf[2];
   if(isprint(ch) )
   {
      buf[0] = ch;
      buf[1] = '\0';
      return buf;
   }
   else
   {
      switch(ch)
      {
         case CR:  return "\\r";
         case LF:  return "\\n";
         case TAB: return "\\t";
         default:  return "Not an escaped char";
      }
   }
}

/* ------------------------------ printAnyRepeats -------------------------------------

    Print repeats-specifier 'rpts' if it is 'valid', meaning it exists.

*/
PRIVATE void printAnyRepeats(S_RepeatSpec const *rpts)
{
   if(rpts != NULL) {
      if(rpts->valid == TRUE) {
         if(rpts->min == rpts->max && rpts->min > 0)                 // min == max? i.e match exactly  -> {3}
            { dbgPrint(" {%d}", rpts->min); }
         else if(rpts->min > rpts->max)                              // min > max?  -> Illegal!!
            { dbgPrint(" {%d,%d BAD!!}", rpts->min, rpts->max); }
         else if(rpts->max == _Repeats_Unlimited)                    // No upper limit? -> at-least -> {3,*}
            { dbgPrint(" {%d,*}", rpts->min); }
         else
            { dbgPrint(" {%d,%d}", rpts->min, rpts->max); }          // else a range e.g {3,6}
      }
   }
}

/* ------------------------------- printCharsBox ----------------------------------------

   Print the one or more 'S_Chars' elements of 'cb' on one line. Each element may be a
   segment of the regex, a single escaped char (of the regex) or a character class
   (defined in the regex).
*/
PRIVATE void printCharsBox(S_CharsBox const *cb, S_RepeatSpec const *rpts)
{
   T_InstrIdx  idx;
   S_Chars     *seg = cb->segs;

   for(idx = 0; seg->opcode != OpCode_Null && idx < 10; idx++, seg++)         // Until all box items, including OpCode_EndCBox, have been printed.
   {
      if(seg->opcode == OpCode_EndCBox)                                       // End of Box?
      {
         if(cb->eatUntilMatch == TRUE)                                        // This box eats leading mismatches? (in the source string)
            { dbgPrint(" (<"); }                                              // then print PACMAN

         dbgPrint(" <end>\r\n");                                              // Says Chars_Box all printed..
      }
      else                                                                    // else more items in 'cb'.
      {
         dbgPrint("%d: %s ", idx, opcodeNames(seg->opcode));                  // Print the name of this CharsBox item e.g 'Class' for a char-class.

         // If this char-list opens a subgroup then print '(' to the left of the chars-list .
         if(cb->opensGroup == TRUE && seg->opcode != OpCode_EndCBox)          // But don't print bracket for 'Match' terminator. It's confusing.
            dbgPrint("(");

         #define _PrintBufSize 100
         C8 buf[_PrintBufSize];

         switch(seg->opcode) {                                                // This segment of the CharsBox is? ...
            case OpCode_Chars:  {                                             //    ... a (reference to) a piece of the regex.
               U8 numChars = MinU8(seg->payload.chars.len, _PrintBufSize-1);  // Will copy up to as many chars as will fit in format buffer.
               memcpy(buf, seg->payload.chars.start, numChars);               // Copy regex segment into 'buf'
               buf[numChars] = '\0';                                          // Make into a string.
               dbgPrint("\"%s\"", buf);                                       // and print.
               break; }

            case OpCode_EscCh:                                                //    ... a single escaped char
               dbgPrint("\"%s\"", printsEscCh(seg->payload.esc.ch));          // which we print using the printsEscCh() translator
               break;

            case OpCode_Anchor:
               dbgPrint("\"%c\"", seg->payload.anchor.ch);
               break;

            case OpCode_Class: {                                              //    ... a character class in a 'C8Bag_'.
               C8 listClass[256];                                             // Will list the elements of the class here (Should really need just a s

               C8bag_List(listClass, seg->payload.charClass);                 // using C8bag_List(), (which should really print only the <128 printables)
               dbgPrint("[%s]", listClass);
               break; }

         }
         // If 'opcode' was for a chars-segment (above) then print repeat count/range.
         if(seg->opcode != OpCode_Null && seg->opcode != OpCode_EndCBox)
            { printAnyRepeats(rpts); }                                        // e.g {3} = just 3; [3,6] = 3 to 6 etc.

         // If this char-list closes a subgroup then print ')' to the right of the chars-list .
         if(cb->closesGroup == TRUE && seg->opcode != OpCode_EndCBox)         // But don't print bracket for 'Match' terminator. It's confusing.
            dbgPrint(")");

         dbgPrint("  ");
      }
      if(seg->opcode == OpCode_EndCBox)                                       // Got 'EndCBox' (and it printed above)?
         { break; }                                                           // then done printing.
   }
}

/* --------------------------------- regexlt_sprintCharBox_partial ------------------------------

   Print Chars-Box 'cb' into 'out', but no more than 'maxChars' (excluding '\0'). So 'out' must
   be at least 'maxChars + 1'.
*/
PUBLIC U16 regexlt_sprintCharBox_partial(C8 *out, S_CharsBox const *cb, U16 maxChars)
{
   #define _PrintBufSize 100
   C8          buf[_PrintBufSize];
   T_InstrIdx  idx;
   S_Chars     *seg = cb->segs;

   if(maxChars == 0)
   {
      return 0;
   }
   else
   {
      U16 charCnt;
      out[0] = '\0';

      #define _MaxBoxElements 10

      for(idx = 0, charCnt = 0;                                                        // From start of CharsBox, nothing printed yet....
          seg->opcode != OpCode_EndCBox && seg->opcode != OpCode_Null &&               // Until end of CBox...
          idx < _MaxBoxElements;                                                       // But no more than (10) items
          idx++, seg++)
      {
         /* If this char-list opens a subgroup then print '(' to the left of the chars-list.
            But don't print that bracket for 'Match' terminator. It's confusing.
         */
         if(cb->opensGroup == TRUE && seg->opcode != OpCode_EndCBox)
         {
            if(charCnt + 1 > maxChars)
               { goto printCBox_Done; }
            else
               { sprintf(out, "(");  charCnt++; out++; }
         }
         else
            { sprintf(out, " "); charCnt++; out++; }

         switch(seg->opcode) {                                                         // This segment is a...
            case OpCode_Chars: {                                              // ...a piece of the source regex.
               U8 segChars = MinU8(seg->payload.chars.len, _PrintBufSize-1);           // Will copy no more chars from segment than we can buffer.

               if(charCnt + segChars + 2 > maxChars)                                   // What we want to copy will not fit in 'out'?
                  {  goto printCBox_Done; }                                            // then quit with what we printed so far.
               else
               {
                  memcpy(buf, seg->payload.chars.start, segChars);                     // Otherwise copy into 'buf'
                  buf[segChars] = '\0';
                  sprintf(out, "'%s'", buf);                                           // and print to 'out', enclosed in '<>'.
                  charCnt += (segChars+2);
                  out += (segChars+2);
                  break;
               }}

            case OpCode_EscCh:                                                // ...an escape sequence, printed; 1 or 2 chars
               if(charCnt + 2 + 2 > maxChars)                                          // No room to add e.g '<\t>'?
                  { goto printCBox_Done; }                                             // then quit with what we printed so far.
               else {
                  sprintf(out, "<%s>", printsEscCh(seg->payload.esc.ch));              // else append to 'out'.
                  charCnt += (2+2);                                                    // We added 3 or 4 chars.
                  out += (2+2);
                  break; }

            case OpCode_Anchor:
               if(charCnt + 2 + 2 > maxChars)                                          // No room to add e.g '<\t>'?
                  { goto printCBox_Done; }                                             // then quit with what we printed so far.
               else {
                  sprintf(out, "<%c>", seg->payload.anchor.ch);                        // else append to 'out'.
                  charCnt += (2+2);                                                    // We added 3 or 4 chars.
                  out += (2+2);
                  break; }

            case OpCode_Class: {                                              // ...a char class
               C8 listClass[257];
               C8bag_List(listClass, seg->payload.charClass);                          // List elements of the class. 0..256off
               size_t len = strlen(listClass);                                         // We have these many

               if(charCnt + len + 2 > maxChars)                                        // No room to add e.g '[12345]'?
               {
                  if(maxChars > charCnt+3)
                  {
                     listClass[maxChars-charCnt] = '\0';
                     sprintf(out, "[%s..", listClass);                                     // else append to 'out'
                     charCnt += (len + 3);                                                // We added class chars plus brackets.
                     out += (len+3);
                  }
                  goto printCBox_Done;                                                 // then quit with what we printed so far.
               }
               else
               {
                  sprintf(out, "[%s]", listClass);                                     // else append to 'out'
                  charCnt += (len + 2);                                                // We added class chars plus brackets.
                  out += (len+2);
               }
               break; }

         /* If this char-list opens a subgroup then print ')' to the right of the chars-list.
            But don't print that bracket for 'Match' terminator. Same as for '(', open..
         */
         if(cb->closesGroup == TRUE && seg->opcode != OpCode_EndCBox)
         {
            if(charCnt + 1 > maxChars)
               { goto printCBox_Done; }
            else
               { sprintf(out, ")");  charCnt++; out++; }
         }

         if(seg->opcode == OpCode_EndCBox)
            { break; }
         }
      }
printCBox_Done:
      return charCnt;
   }
}

/* ------------------------------- regexlt_printProgram ----------------------------------------

   List the compiled regex 'prog'
*/
PUBLIC void regexlt_printProgram(S_Program *prog)
{
   T_InstrIdx idx;

   S_Instr *instr = prog->instrs.buf;
   S_RepeatSpec const *rpts;

   dbgPrint("------ Compiled Regex:\r\n");

   // Limit the number of lines in case compile produced an unterminated program.
   #define _MaxProgLines 40

   for(idx = 0, instr = prog->instrs.buf,                            // From the start of the program
       rpts = NULL;
       idx < prog->instrs.put; idx++, instr++)                       // until the 'put'; which is the end.
   {
      if(idx >= _MaxProgLines)                                       // 'prog' had too many lines to print
      {
         printf("Too many lines (%d) to print - raise the limit! ****************\r\n", idx);
         return;                                                     // then say so.
      }

      dbgPrint("%d: %s ", idx, opcodeNames(instr->opcode));          // Print opcode 'Jump', 'Split', 'Chars'(Box), 'match' or 'NOP'.

      switch(instr->opcode) {                                        // If Current opcode is.... may append stuff.

         case OpCode_CharBox:                                        // Chars-Box?
            if(instr->charBox.segs == NULL)                          // Empty? it shouldn't be
               {dbgPrint("empty CharBox\r\n");}                      // Say so!
            else
               { printCharsBox(&instr->charBox, rpts);               // else append the Chars-Box contents
                 rpts = NULL; }                                      // Repeat spec, if any, was printed. Cancel so we don't reprint.
            break;

         case OpCode_Jmp:                                            // Jump?
            dbgPrint("%d ", instr->left);                            // then show jump destination, which is left-branch.
            break;

         case OpCode_Split:                                          // Split?
            dbgPrint("(%d, %d) ", instr->left, instr->right);        // then show both destinations.
            /* Any repeat-specification for the following Char-Box is held with (this) '_Split'.
               For clarity we want to print repeat-spec after the contents of the Char_Box. Take a
               ref now to use later with the 'case OpCode_CharBox:'.
            */
            rpts = &instr->repeats;
      }
      if(instr->opcode != OpCode_CharBox) { dbgPrint("\r\n"); }
   }
   dbgPrint("\r\n");
}


// ------------------------------------------- eof ----------------------------------------------------
