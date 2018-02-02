/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Module-private exports.
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

#ifndef REGEXLT_PRIVATE_H
#define REGEXLT_PRIVATE_H

typedef struct {
   U16   len,              // Of the regex string.
         charboxes,        // Holding either char-segments, char-classes or escaped chars
         instructions,     // Program-size; Splits, JMPs or CharBoxes.
         classes,          // Char-classes, which must be malloced()
         subExprs;         // 1 + number of possible sub-matches.
   BOOL  legal;            // Did the pre-scan say regex was OK?
} S_RegexStats;

typedef struct {
   BOOL  inClass,    // Inside a character-class definition.
         inRange,    // e.g {4,7}
         charSeg,    // In a character-segment e.g '...cdef...'
         inGroup,    // Inside '(....  )'
         uneatenSubGrp,// A preceding group has not yet been consumed by an operator.
         esc;        // Preceding char was '\'

   U8    classCnt,   // Numbers of character class definitions so far
         segCnt,     // Character segments so far
         escCnt,     // Number of chars escaped OUTSIDE A CHAR CLASS.
         operators,
         subExprs;
} S_CntRegexParts;


PUBLIC void regexlt_dbgPrint(C8 const *fmt, ...);

PUBLIC S_RegexStats regexlt_prescan(C8 const *regex);

typedef enum { E_Continue = 0, E_Complete = 1, E_Fail } T_ParseRtn;

typedef struct {
   C8    prevCh;
   BOOL  range;      // Previous ch was a '-', meaning we are parsing
   BOOL  negate;     // Previous ch was '^', meaning following char is a negation.
   BOOL  esc;        // an escape '\', to be followed by a class specifier.
} S_ParseCharClass;

PUBLIC void       regexlt_classParser_Init(S_ParseCharClass *p);
PUBLIC BOOL       regexlt_classParser_AddDef(S_ParseCharClass *p, S_C8bag *cc, C8 const *def);
PUBLIC T_ParseRtn regexlt_classParser_AddCh(S_ParseCharClass *p, S_C8bag *cc, C8 newCh);
PUBLIC C8 const * regexlt_getCharClassByKey(C8 key);

typedef U8 T_RepeatCnt;    // Regex repeat counts e.g [Ha ]{3} = 'Ha Ha Ha '
#define _Repeats_Unlimited MAX_U8   // If the max repeats was left open, i.e '{3,}
#define _MaxRepeats (_Repeats_Unlimited - 1)

typedef struct {T_RepeatCnt min, max; BOOL valid; } S_RepeatSpec;

PUBLIC T_ParseRtn regexlt_parseRepeat(S_RepeatSpec *r, C8 const **ch);


typedef U8 T_InstrIdx;     // To index an instruction in a array of S_Instr; a program counter.

// A list/heap of character classes
typedef struct {
   U8       size,    // Size of S_C8bag[] reserved by malloc()
            put;     // Next put.
   S_C8bag  *ccs;    // malloced space is here.
} S_ClassesList;

typedef U8 T_OpCode;       // Opcodes in compiled regex instructions


/* These are opcodes & labels for both character lists 'S_Chars' and compiled regex
   instructions 'S_Instr'. 'Null' and 'Match' are common to both types.
*/
enum {
   OpCode_Null = 0,           // Empty unwritten item
   OpCode_NOP,                // No-op is a placeholder for the start of a sub-group.
   OpCode_Chars,              // Contiguous segment of the regex string.
   OpCode_EscCh,              // Single escaped char from the regex string e.g '\n' or \d (a digit)
   OpCode_Class,              // Character class e.g [0-9a-z]
   OpCode_CharBox,            // List of 'Chars', 'EscCh', 'Class', representing a contiguous stretch of the regex between operators.
   OpCode_Jmp,                // Jump relative
   OpCode_Split,              // Split into 2 simultaneous threads of execution.
   OpCode_Match } eOpCode;    // Terminates both character box list and compiled regex instructions list.

static inline BOOL opCode_HoldsChars(T_OpCode op)
   { return op == OpCode_Chars || op == OpCode_EscCh || op == OpCode_Class; }

// A segment of chars in the source regex.
typedef U8 T_CharSegmentLen;
typedef struct {C8 const *start; T_CharSegmentLen len; } S_CharSegment;
typedef struct { C8 ch; } S_EscChar;

typedef union {                     // One of... from the match string.
   S_CharSegment chars;             //    a char segment (start, numChars)
   S_EscChar     esc;               //    an escaped char e.g \n, \t
   S_C8bag       *charClass;        //    a char class e.g [0-8ab]
} U_Payload;


typedef struct  {                   // Holds either a char segment, escaped char or a char class.
   T_OpCode    opcode;              // 'OpCode_Chars', 'OpCode_EscCh' or 'OpCode_Class', dpending what's in 'payload'.
   U_Payload   payload;
} S_Chars;

typedef struct {                    // A box of one or more character segments or classes in the match string.
   S_Chars     *buf;                // The char segments and char classes.
   T_InstrIdx  len;                 // Number of elements in 'buf'
   BOOL        opensGroup,
               closesGroup;
   BOOL        eatUntilMatch;       // Eat source string until 1st match with chars or class in 'buf'[0].
} S_CharsBox;


typedef struct {                    // A (compiled) instruction. Contains:
   T_OpCode       opcode;           // an eOpCode e.g OpCode_Chars, OpCode_Jmp, OpCode_Match
   S_CharsBox     charBox;          // A box of one or more character segments or classes in the match string.
   T_InstrIdx     left, right;      // 'left' is the  destination for a  '_Jmp'; for '_Split' it's both 'left' and 'right'.
   S_RepeatSpec   repeats;          // Match zero of more repeats of 'charBox'
   BOOL           opensGroup,       //  This instruction opens of closes a group.
                  closesGroup;
} S_Instr;

typedef struct {                    // List of character-segments and char-classes from the match string...
   S_Chars     *buf;                // ...which are here
   T_InstrIdx  put;                 // 'put' to add another segment / number of S_Instr in 'buf'.
} S_CharsList;

typedef struct {                    // List of instructions...
   S_Instr     *buf;                // ...which are here.
   T_InstrIdx  put;                 // 'put' to add another one / number of S_Instr in 'buf'.
} S_InstrList;

// A compiled regex is...
typedef struct {
   S_InstrList    instrs;           // Instructions to execute, terminated by 'Match'
   S_CharsList    chars;            // One or more lists of chars and char classes, each attached to a 'Char' instruction, terminated by 'Match'.
   S_ClassesList  classes;          // Zero or more character classes, each a part or all of a S_CharsList
} S_Program;

PUBLIC BOOL regexlt_compileRegex(S_Program *prog, C8 const *regexStr);
PUBLIC void regexlt_printProgram(S_Program *prog);

typedef struct { void **mem; size_t numBytes; } S_TryMalloc;

PUBLIC void regexlt_safeFree(void *p);
PUBLIC void regexlt_safeFreeList(void **lst, U8 listSize);
PUBLIC BOOL regexlt_getMemMultiple(S_TryMalloc *lst, U8 listSize);

PUBLIC T_RegexRtn regexlt_runCompiledRegex(S_InstrList *prog, C8 const *str, RegexLT_S_MatchList **ml, U8 maxMatches, RegexLT_T_Flags flags);

extern RegexLT_S_Cfg const *regexlt_cfg;

PUBLIC BOOL regexlt_getMemMultiple(S_TryMalloc *lst, U8 listSize);
PUBLIC void regexlt_safeFreeList(void **lst, U8 listSize);
PUBLIC BOOL regexlt_getMemMultiple(S_TryMalloc *lst, U8 listSize);

PUBLIC void regexlt_sprintCharBox_partial(C8 *out, S_CharsBox const *cb);

#endif // REGEXLT_PRIVATE_H

// ---------------------------------------- eof ------------------------------------------
