#include "libs_support.h"
   #if _TARGET_IS == _TARGET_UNITY_TDD
#include "unity.h"
#define _TRACE_PRINTS_ON false
   #else
#define TEST_FAIL()
#define TEST_ASSERT_TRUE(...)
#define TEST_ASSERT_FALSE(...)
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

/* ---------------------------- test_EmptyStr ----------------------------------------- */

void test_EmptyStr(void)
{
   S_ParseCharClass *p = &(S_ParseCharClass){};          // Make and initialise a Parser;
   regexlt_classParser_Init(p);
   S_C8bag *cc = &(S_C8bag){0};                          // Char class from "[nnn]" goes here.

   // Empty string returns Fail.
   TEST_ASSERT_TRUE( E_Fail == regexlt_classParser_AddCh(p, cc, ""));
}


/* ---------------------------- test_AddDef_Fails ---------------------------------------------

   All legal Char Classes are tested in test_CharClass() below. Check some fails here.
*/
void test_AddDef_Fails(void)
{
   S_ParseCharClass *p = &(S_ParseCharClass){};          // Make and initialise a Parser;
   regexlt_classParser_Init(p);
   S_C8bag *cc = &(S_C8bag){0};                          // Char class from "[nnn]" goes here.

   // The Char Class definition must start with '['. Empty string or other chars fail.
   TEST_ASSERT_FALSE(regexlt_classParser_AddDef(p, cc, ""));
   TEST_ASSERT_FALSE(regexlt_classParser_AddDef(p, cc, "ab"));
}


/* ---------------------------- test_CharClass ----------------------------------------- */
void test_CharClass(void) {

   typedef struct { C8 const *expr; S_C8bag cClass; T_ParseRtn rtn; } S_Tst;

   S_Tst const tsts[] = {
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

      // Chained '-' does NOT start a new range; is read as a literal.
      {.expr = "[0-9-A]",     .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF2000, 0x00000002, 0x00000000}}, .rtn = E_Complete},

      // Longer class OK? - and negated.
      {.expr = "[ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789]",
                              // A-Z = 0x41-0x5A; a-z = 0x61-0x7A; 0-9 = 0x30-0x39
                              .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF0000, 0x07FFFFFE, 0x07FFFFFE}}, .rtn = E_Complete},
      {.expr = "[^ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789]",
                              .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFC00FFFF, 0xF8000001, 0xF8000001}}, .rtn = E_Complete},

      // Range brackets can be escaped.
      {.expr = "[\\]]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x20000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\[]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x08000000, 0x00000000}}, .rtn = E_Complete},
      // Make sure OK if both included.
      {.expr = "[\\[\\]]",    .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x28000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\]\\[]",    .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x28000000, 0x00000000}}, .rtn = E_Complete},

      // Escaped '\', and it's inverse.
      {.expr = "[\\\\]",      .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x10000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[^\\\\]",     .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFFFFFFFF, 0xEFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},

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

      // Where '^' itself is one end of a range.

      // 'Z' thru '^' i.e 'Z[]\^'
      {.expr = "[Z-^]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x7C000000, 0x00000000}}, .rtn = E_Complete},
      // Range upside-down; fail.
      {.expr = "[a-^]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},
      {.expr = "[0^-Z]",      .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},

      // Unprepended '-' is literal '-'
      {.expr = "[-]",         .cClass = (S_C8bag){.lines = {0x00000000, 0x00002000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Repeated '-' is just '-' (2nd is not interpreted a range marker)
      {.expr = "[--]",        .cClass = (S_C8bag){.lines = {0x00000000, 0x00002000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // Opened a Range but did not close it. Then '-' and preceding char '0' are literals.
      {.expr = "[0-]",        .cClass = (S_C8bag){.lines = {0x00000000, 0x00012000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Same as '[0-]' but negated.
      {.expr = "[^0-]",       .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFFFEDFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},
      // '-' after a range is a literal
      {.expr = "[0-9-]",      .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF2000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // Where '-' itself is one end of the range

      // From '*' thru '-' i.e '*+.-'
      {.expr = "[*--]",        .cClass = (S_C8bag){.lines = {0x00000000, 0x00003C00, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // From '-' thru '0' i.e '-./0'
      {.expr = "[--0]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x0001E000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // However way it's read, is just '-'
      {.expr = "[---]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00002000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // Upside-down; fail.
      {.expr = "[0--]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},
      {.expr = "[--*]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},

      {.expr = "[-\\r]",      .cClass = (S_C8bag){.lines = {0x00002000, 0x00002000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

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

      // HexASCII range.
      {.expr = "[\\x00-\\x09]", .cClass = (S_C8bag){.lines = {0x000003FF, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // 1-digit, truncated by ']' -> 0x03
      {.expr = "[\\x3]",            .cClass = (S_C8bag){.lines = {0x00000008, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // 1 digit, trucated by any non Hex ASCII
      {.expr = "[\\x3z]",           .cClass = (S_C8bag){.lines = {0x00000008, 0x00000000, 0x00000000, 0x04000000}}, .rtn = E_Complete},
      {.expr = "[\\x3\\d]",         .cClass = (S_C8bag){.lines = {0x00000008, 0x03FF0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\x3-\\x09]",      .cClass = (S_C8bag){.lines = {0x000003F9, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // '\x' was not followed by HexASCII.
      {.expr = "[\\xz]",            .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x80000000}}, .rtn = E_Fail},
      // Upside-down range in HexASCII
      {.expr = "[\\x09-\\x00]",     .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x80000000}}, .rtn = E_Fail},

      // Multiples... to be sure sub-parser is reset
      {.expr = "[\\x00\\x01\\x02]", .cClass = (S_C8bag){.lines = {0x00000007, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      // 'A' after 0x00 is a literal.
      {.expr = "[\\x00A\\x02]",     .cClass = (S_C8bag){.lines = {0x00000005, 0x00000000, 0x00000002, 0x00000000}}, .rtn = E_Complete},
      // Range A-F between HexASCII
      {.expr = "[\\x00A-F\\x02]",   .cClass = (S_C8bag){.lines = {0x00000005, 0x00000000, 0x0000007E, 0x00000000}}, .rtn = E_Complete},
      // Negate multiple HexASCII
      {.expr = "[^\\x00\\x01\\x02]",.cClass = (S_C8bag){.lines = {0xFFFFFFF8, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},

      // ---- Char Classes.

      // Digits 0-9; and not.
      {.expr = "[\\d]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF0000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\D]",       .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFC00FFFF, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},
      // Alphanumeric + underscore and not.
      {.expr = "[\\w]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x03FF0000, 0x87FFFFFE, 0x07FFFFFE}}, .rtn = E_Complete},
      {.expr = "[\\W]",       .cClass = (S_C8bag){.lines = {0xFFFFFFFF, 0xFC00FFFF, 0x78000001, 0xF8000001}}, .rtn = E_Complete},
      // Whitespace and not whitespace
      {.expr = "[\\s]",       .cClass = (S_C8bag){.lines = {0x00003E00, 0x00000001, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\S]",       .cClass = (S_C8bag){.lines = {0xFFFFC1FF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF}}, .rtn = E_Complete},

      // Char class cannot close a range, so read as 0 thru '\' plus 'd' = 0x30-0x5C plus 0x64
      {.expr = "[0-\\d]",     .cClass = (S_C8bag){.lines = {0x00000000, 0xFFFF0000, 0x1FFFFFFF, 0x00000010}}, .rtn = E_Complete},
      // Range upside-down; fail.
      {.expr = "[a-\\d]",     .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},

      // Char class cannot open a range so '\\' is read as literal and the range is d-g.
      {.expr = "[\\d-g]",     .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x10000000, 0x000000F0}}, .rtn = E_Complete},
      // d-0 is upside-down; fail.
      {.expr = "[\\d-0]",     .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},

      // ---- Non-printables, as text.

      {.expr = "[\\r]",       .cClass = (S_C8bag){.lines = {0x00002000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\n]",       .cClass = (S_C8bag){.lines = {0x00000400, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\t]",       .cClass = (S_C8bag){.lines = {0x00000200, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\a]",       .cClass = (S_C8bag){.lines = {0x00000080, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\e]",       .cClass = (S_C8bag){.lines = {0x08000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\f]",       .cClass = (S_C8bag){.lines = {0x00001000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},
      {.expr = "[\\v]",       .cClass = (S_C8bag){.lines = {0x00000800, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Complete},

      // ---- Fails

      // Open again an already open class.
      {.expr = "[[",           .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},
      // Incomplete class. Parser is waiting for more (E_Continue).
      {.expr = "[b",           .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Continue},
      // Illegal / non-supported escape char.
      {.expr = "[\\z]",       .cClass = (S_C8bag){.lines = {0x00000000, 0x00000000, 0x00000000, 0x00000000}}, .rtn = E_Fail},
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
         if(t->rtn != E_Continue)
         {
            sprintf(msg, "Parse not complete '%s...'  Expected E_Continue, got %s\r\n",
                              t->expr, t->rtn == E_Fail ? "E_Fail" : "E_Complete");
         }
         else
         {
            return TRUE;
         }
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
   else
   {
      TEST_FAIL();
   }
}

// ----------------------------------------- eof --------------------------------------------
