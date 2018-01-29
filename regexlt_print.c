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
#define br        regexlt_cfg


#define TAB 0x09
#define CR  0x0D
#define LF  0x0A


// -------------------------------- regexlt_dbgPrint --------------------------------------

PUBLIC void regexlt_dbgPrint(C8 const *fmt, ...)
{
   va_list argptr;
   va_start(argptr,fmt);

   if(br->printEnable)
   {
      vfprintf(stdout, fmt, argptr);
   }
   va_end(argptr);
}

// ---------------------------------- opcodeNames ----------------------------------------

PRIVATE C8 const *opcodeNames(T_OpCode op)
{
   switch(op) {
      case OpCode_Null:    return "Null";
      case OpCode_NOP:     return "No-Op";
      case OpCode_Chars:   return "Chars";
      case OpCode_EscCh:   return "Esc";
      case OpCode_Class:   return "Class";
      case OpCode_CharBox: return "CharBox";
      case OpCode_Jmp:     return "Jmp";
      case OpCode_Split:   return "Split";
      case OpCode_Match:   return "Match";
      default:             return "unknown opcode";
   }
}

/* --------------------------------- printsEscCh -------------------------------------- */

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
         default: return "Not an escaped char";
      }
   }
}

/* ------------------------------ printAnyRepeats ------------------------------------- */

PRIVATE void printAnyRepeats(S_RepeatSpec const *rpts)
{
   if(rpts != NULL) {
      if(rpts->valid == TRUE) {
         if(rpts->min == rpts->max && rpts->min > 0)
            { dbgPrint(" {%d}", rpts->min); }
         else if(rpts->min > rpts->max)
            { dbgPrint(" {%d,%d BAD!!", rpts->min, rpts->max); }
         else if(rpts->max == _Repeats_Unlimited)
            { dbgPrint(" {%d,*}", rpts->min); }
         else if(1)
            { dbgPrint(" {%d,%d}", rpts->min, rpts->max); }
      }
   }
}

/* ------------------------------- printCharBox ---------------------------------------- */


PRIVATE void printCharBox(S_CharsBox const *cb, S_RepeatSpec const *rpts)
{
   #define _PrintBufSize 100
   C8          buf[_PrintBufSize];
   C8          listClass[256];
   U8          numChars;
   T_InstrIdx  idx;
   S_Chars     *lst = cb->buf;

   for(idx = 0; lst->opcode != OpCode_Null && idx < 10; idx++, lst++)
   {
      if(lst->opcode == OpCode_Match)
      {
         if(cb->eatUntilMatch == TRUE)
            { dbgPrint(" <<"); }

         dbgPrint(" <end>\r\n");
      }
      else
         { dbgPrint("   %d: %s ", idx, opcodeNames(lst->opcode)); }

      // If this char-list opens a subgroup then print '(' to the left of the chars-list .
      if(cb->opensGroup == TRUE && lst->opcode != OpCode_Match)   // But don't print bracket for 'Match' terminator. It's confusing.
         dbgPrint("(");

      switch(lst->opcode) {
         case OpCode_Chars:
            numChars = MinU8(lst->payload.chars.len, _PrintBufSize-1);
            memcpy(buf, lst->payload.chars.start, numChars);
            buf[numChars] = '\0';
            dbgPrint("\"%s\"", buf);
            break;

         case OpCode_EscCh:
            dbgPrint("\"%s\"", printsEscCh(lst->payload.esc.ch));
            break;

         case OpCode_Class:
            C8bag_List(listClass, lst->payload.charClass);
            dbgPrint("[%s]", listClass);
            break;

      }
      if(lst->opcode != OpCode_Null && lst->opcode != OpCode_Match)
         { printAnyRepeats(rpts); }

      // If this char-list closes a subgroup then print ')' to the right of the chars-list .
      if(cb->closesGroup == TRUE && lst->opcode != OpCode_Match)  // But don't print bracket for 'Match' terminator. It's confusing.
         dbgPrint(")");

      if(lst->opcode == OpCode_Match)
         { break; }
   }
}

/* --------------------------------- regexlt_sprintCharBox_partial ------------------------------ */

PUBLIC void regexlt_sprintCharBox_partial(C8 *out, S_CharsBox const *cb)
{
   #define _PrintBufSize 100
   C8          buf[_PrintBufSize];
   C8          listClass[256];
   U8          numChars;
   T_InstrIdx  idx;
   S_Chars     *lst = cb->buf;

   out[0] = '\0';

   for(idx = 0; lst->opcode != OpCode_Match && lst->opcode != OpCode_Null && idx < 10; idx++, lst++)
   {
      // If this char-list opens a subgroup then print '(' to the left of the chars-list .
      if(cb->opensGroup == TRUE && lst->opcode != OpCode_Match)   // But don't print bracket for 'Match' terminator. It's confusing.
      sprintf(EndStr(out), "(");

      switch(lst->opcode) {
         case OpCode_Chars:
            numChars = MinU8(lst->payload.chars.len, _PrintBufSize-1);
            memcpy(buf, lst->payload.chars.start, numChars);
            buf[numChars] = '\0';
            sprintf(EndStr(out), "<%s>", buf);
            break;

         case OpCode_EscCh:
            sprintf(EndStr(out), "<%s>", printsEscCh(lst->payload.esc.ch));
            break;

         case OpCode_Class:
            C8bag_List(listClass, lst->payload.charClass);
            sprintf(EndStr(out), "[%s]", listClass);
            break;

      // If this char-list closes a subgroup then print ')' to the right of the chars-list .
      if(cb->closesGroup == TRUE && lst->opcode != OpCode_Match)  // But don't print bracket for 'Match' terminator. It's confusing.
         sprintf(EndStr(out), ")");

      if(lst->opcode == OpCode_Match)
         { break; }
      }
   }
}

/* ------------------------------- regexlt_printProgram ---------------------------------------- */

PUBLIC void regexlt_printProgram(S_Program *prog)
{
   T_InstrIdx idx;

   S_Instr *instr = prog->instrs.buf;
   S_RepeatSpec const *rpts;

   dbgPrint("------ Compiled Regex:\r\n");

   for(idx = 0, rpts = NULL; idx < prog->instrs.put && idx < 20; idx++, instr++)
   {
      dbgPrint("%d: %s ", idx, opcodeNames(instr->opcode));

      switch(instr->opcode) {

         case OpCode_CharBox:
            if(instr->charBox.buf == NULL)
               {dbgPrint("empty CharBox\r\n");}
            else
               { dbgPrint("\r\n"); printCharBox(&instr->charBox, rpts);
                 rpts = NULL; }                                      // Repeat spec, if any, was printed. Cancel so we don't reprint.
            break;

         case OpCode_Jmp:
            dbgPrint("%d ", instr->left);
            break;

         case OpCode_Split:
            dbgPrint("(%d, %d) ", instr->left, instr->right);
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
