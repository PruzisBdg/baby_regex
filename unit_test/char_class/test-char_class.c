#include "libs_support.h"
   #if _TARGET_IS == _TARGET_UNITY_TDD
#include "unity.h"
#define _TRACE_PRINTS_ON false
   #else
#define TEST_FAIL()
#define _TRACE_PRINTS_ON true
   #endif // _TARGET_IS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "util.h"
#include "regexlt_private.h"

PUBLIC U16 tdd_TestNum;    // For labeling error messages with the test that failed.

// =============================== Tests start here ==================================


/* -------------------------------------- setUp ------------------------------------------- */

void setUp(void) {
}

/* -------------------------------------- tearDown ------------------------------------------- */

void tearDown(void) {
}


/* ---------------------------- test_CharClass ----------------------------------------- */
void test_CharClass(void) {

   typedef struct { C8 const *expr; S_C8bag cClass; T_ParseRtn rtn; } S_Tst;

   S_Tst const tsts[] = {
      // Chained '-' does NOT start a new range; is read as a literal.
      //{.expr = "[0-9-A]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF2000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      {.expr = "[\\x00-\\x09]", .cClass = (S_C8bag){.lines = {0x000003FF, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Empty class is legal. This isn't a PCRE standard; but it's a good default.
      {.expr = "[]",          .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      {.expr = "[0123]",      .cClass = (S_C8bag){.lines = {0x00000000, 0x000F0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // Duplicates are ignored.
      {.expr = "[30123]",     .cClass = (S_C8bag){.lines = {0x00000000, 0x000F0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},


      // ---- Ranges

      {.expr = "[0-9]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // 1 character range is legal; if superfluous
      {.expr = "[0-0]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00010000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Reversed range is a fail.
      {.expr = "[9-0]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF0000, 0x00000000, 0x00000000}}, .rtn = E_Fail},

      // Escaped '-' is a literal, so the chars before and after also literals, not ends of a range.
      {.expr = "[0\\-9]",      .cClass = (S_C8bag){.lines = {0x00000000, 0x02012000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Because they are not ends of range their order does not matter.
      {.expr = "[9\\-0]",      .cClass = (S_C8bag){.lines = {0x00000000, 0x02012000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // Can mix a Range and a list. Or multiple ranges.
      {.expr = "[0-34567]",   .cClass = (S_C8bag){.lines = {0x00000000, 0x00FF0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[01234-7]",   .cClass = (S_C8bag){.lines = {0x00000000, 0x00FF0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[0-36-9]",    .cClass = (S_C8bag){.lines = {0x00000000, 0x03CF0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // Longer class OK? - and negated.
      {.expr = "[ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789]",
                              // A-Z = 0x41-0x5A; a-z = 0x61-0x7A; 0-9 = 0x30-0x39
                              .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF0000, 0x07FFFFFE, 0x07FFFFFE}}, .rtn = E_Complete},
      {.expr = "[^ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789]",
                              .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFC00FFFF, 0xF8000001, 0xF8000001}}, .rtn = E_Complete},

      // ---- Negations

      {.expr = "[^0]",        .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFFFEFFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},
      {.expr = "[^01]",       .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFFFCFFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},
      {.expr = "[^0-9]",      .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFC00FFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},

      // Negation must be followed by char or range.
      {.expr = "[^]",         .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},
      // '^' is only recognised as negation if it's the 1st char. Otherwise it's a literal ('^' = 0x5E)
      {.expr = "[0^]",        .cClass = (S_C8bag){.lines = {0x00000000, 0x00010000, 0x40000000, 0x00000000}}, .rtn = E_Complete},
      // So '^' can negate itself.
      {.expr = "[^^]",        .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFFFFFFFF, 0xBFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},
      // Escaped '^' is literal.
      {.expr = "[\\^]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x40000000, 0x00000000}}, .rtn = E_Complete},

      // Unprepended '-' is literal '-'
      {.expr = "[-]",         .cClass = (S_C8bag){.lines = {0x00000000, 0x00002000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // Opened a Range but did not close it. Then '-' and preceding char '0' are literals.
      {.expr = "[0-]",         .cClass = (S_C8bag){.lines = {0x00000000, 0x00012000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Same as '[0-]' but negated.
      {.expr = "[^0-]",        .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFFFEDFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},
      // '-' after a range is a literal
      {.expr = "[0-9-]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF2000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      /* ---- HexASCII Literals

         This regex encodes 0...7F, 0x80...0xFF are illegal.  This is unlike PCRE which accepts 0...0xFF. The smaller range
         save storage for Ranges.
      */
      // 0 - 0x&F accepted
      {.expr = "[\\x00]",           .cClass = (S_C8bag){.lines = {0x00000001, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\x7F]",           .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x80000000}}, .rtn = E_Complete},
      // 0x80 - 0xFF Fail.
      {.expr = "[\\x80]",           .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x80000000}}, .rtn = E_Fail},
      {.expr = "[\\xFF]",           .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x80000000}}, .rtn = E_Fail},
      // Multiples... to be sure sub-parser is reset
      {.expr = "[\\x00\\x01\\x02]", .cClass = (S_C8bag){.lines = {0x00000007, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // 'A' after 0x00 is a literal.
      {.expr = "[\\x00A\\x02]",     .cClass = (S_C8bag){.lines = {0x00000005, 0x00000000, 0x00000002, 0x00000000}}, .rtn = E_Complete},
      // Range A-F between HexASCII
      {.expr = "[\\x00A-F\\x02]",   .cClass = (S_C8bag){.lines = {0x00000005, 0x00000000, 0x0000007E, 0x00000000}}, .rtn = E_Complete},
      // Negate multiple HexASCII
      {.expr = "[^\\x00\\x01\\x02]",.cClass = (S_C8bag){.lines = {0xFFFFFFF8, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},

   };

   BOOL testOne(S_Tst const *t, C8 *msg)
   {
      msg[0] = '\0';                                        // If no 'msg' return "".

      S_ParseCharClass *p = &(S_ParseCharClass){};          // Make and initialise a Parser;
      regexlt_classParser_Init(p);

      S_C8bag *cc = &(S_C8bag){0};                          // Char class from "[nnn]" goes here.

      C8 const *src;
      T_ParseRtn rtn;
      U8 i;

      for(i = 0, src = &t->expr[1]; *src != '\0'; src++, i++)
      {
         rtn = regexlt_classParser_AddCh(p, cc, src);

         if(rtn == E_Fail)
         {
            if(t->rtn == E_Fail)
            {
               return TRUE;
            }
            else
            {
               C8 e0[20];
               strcpy(e0, t->expr);
               e0[i] = '\0';
               sprintf(msg, "E_Fail at \'%s\'[%u]\r\n", t->expr, i+1);
               break;
            }
         }
         else if(rtn == E_Complete)
         {
            if(*(src+1) != '\0')
            {
               sprintf(msg, "Completed early: %s -> %s\r\n", t->expr, C8bag_PrintLine( (C8[_C8bag_PrintLine_Size]){}, cc));
               break;
            }
         }
         else if(rtn == E_Continue)
         {
            // Good - parse next char.
         }
         else
         {
            sprintf(msg, "Illegal return (%u) from regexlt_classParser_AddCh\r\n");
         }
      }

      if(rtn == E_Fail)
      {
         if(t->rtn != E_Fail)
         {
            if(msg[0] == '\0')
               { sprintf(msg, "%s -> E_Fail\r\n", t->expr); }
         }
      }
      else if(rtn == E_Complete)
      {
         if(t->rtn != E_Complete)
         {
            sprintf(msg, "Wrong return: expected %s got E_Complete", t->rtn == E_Fail ? "E_Fail" : "E_Continue");
         }
         else if(C8bag_Equal(&t->cClass, cc) && *src == '\0')
         {
            return TRUE;
         }
         else if(!C8bag_Equal(&t->cClass, cc))
         {
            sprintf(msg, "Wrong output: %s ->\r\n   expected: %s\r\n        got: %s\r\n",
                     t->expr,
                     C8bag_PrintLine((C8[_C8bag_PrintLine_Size]){}, &t->cClass),
                     C8bag_PrintLine((C8[_C8bag_PrintLine_Size]){}, cc));
         }
      }
      else if(rtn == E_Continue)
      {
         sprintf(msg, "Parse not complete %s -> ???\r\n", t->expr);
      }
      return FALSE;     // Did not return TRUE above, so some fail.
   } // testOne().

   BOOL fails = FALSE;

   for(U8 i = 0; i < RECORDS_IN(tsts); i++)
   {
      C8 msg[200];
      if(testOne(&tsts[i], msg) == FALSE)
      {
         printf("tst #%d  %s\r\n   %s\r\n", i, tsts[i].expr, msg);
         fails = true;
      }
   }

   if(fails == FALSE)
   {
      printf("Char Class; %u tests succeeded\r\n", RECORDS_IN(tsts));
   }
}

// ----------------------------------------- eof --------------------------------------------
