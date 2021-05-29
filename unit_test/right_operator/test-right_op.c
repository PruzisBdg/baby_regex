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

// --------------------------------------------------------------------------------------------
static void failwMsg(U16 tstNum, C8 const *msg) {
   printf("tst #%d\r\n   %s\r\n", tstNum, msg);
   TEST_FAIL(); }

// -------------------------------- test_RightOperator --------------------------------------

void test_RightOperator(void)
{
   typedef struct { C8 const *tstStr; C8 rtn; } S_Tst;

   S_Tst const tsts[] = {
      { "abc",    '$' },
      { "a+",     '+' },
      { "a?",     '?' },
      { "a*",     '*' },
      { "a{2}",   '{' },

      // Escaped chars
      { "\\a+",     '+' },
      { "a\\b+",    'E' },
      { "a\\\\+",   'E' },
      { "a\\\\",    '$' },

      { "a\\+",    '$' },
      { "a\\?",    '$' },
      { "a\\*",    '$' },
      { "a\\{",    '$' },

      // Post operator captures previous char.
      { "ab+",     'E' },
      { "ab?",     'E' },

      { "ab*",     'E' },
      { "ab{2}",   'E' },

      { "a+b",     '+' },
      { "ab+c",    'E' },

      { "a(b)+",   'E' },
      { "(a)b+",   'E' },

      // Nested subs
      { "(a(b))+", '+' },

      // Post operator captures group.
      { "(a)+",    '+' },
      { "((a))+",  '+' },
      { "([0-9])+",        '+' },
      { "(([0-9])abc)+",   '+' },
      { "(a){3,4}",        '{' },

      // Escaped backslash.
      { "a\\\\",    '$' },

      // Subgroups
      { "a(b)",   'E' },
      { "a(b+)",   'E' },
      { "aa(b)",  'E' },
      { "(a)b",   'E' },
      { "(a)bb",  'E' },
      { "(a)",    '$' },

      { "(\\d)",    '$' },
      { "\\(\\d{3}", 'E' },

      // Alternation
      { "a|b",       '|' },
      { "cat|dog",   '|' },            // '|' is right-associative.
      { "cat|dog|mouse",   '|' },
      { "(a|b)+",     '+' },           // encompassed by a group.

      // Repeats
      { "a{3,5}b",   '{' },
      { "a{3}b",   '{' },
      { "[0-9]{3}d",   '{' },


      // Nesting
      { "(ab(cd))", '$' },
      { "(ab(cd))e", 'E' },
      { "(ab(cd))e", 'E' },


      // With char classes
      { "[A-Z]bc",  '$' },
      { "[A-Z]",    '$' },

      { "[A-Z]+",   '+' },
      { "ab[A-Z]+", 'E' },
      { "ab[A-Z]",  '$' },

      { "a[\\d]b",   '$' },

      // Incompletes
      { "a)b",    '$' },
      { "a)b+",   'E' },
      { "a)+",    '+' },
      { "abc)+",  '+' },
      { "abc))+", '+' },      // From inside group(s) capture the whole group.
   };

   U8 i, fails; C8 rtn;

   for(i = 0, fails = 0; i < RECORDS_IN(tsts); i++)
   {
      S_Tst const *t = &tsts[i];

      if( (rtn = rightOperator( t->tstStr)) != t->rtn )
      {
         printf("right operator fail #%u: \"%s\" -> %c but got %c\r\n", i, t->tstStr, t->rtn, rtn);
         fails++;
      }
   }

   if(fails > 0)
   {
      TEST_FAIL();
   }
}


