/* ------------------------------------------------------------------------------
|
| Test Shell for RegexLT_
|
--------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "libs_support.h"
#include "util.h"
#include "unity.h"


/* ------------------------------ getMemCleared ---------------------------------- */

#define _memclr(addr, size)  memset((addr), 0, (size_t)(size))

//#define SHOW_MALLOCS

   #ifdef SHOW_MALLOCS
PRIVATE S16 mallocCnt = 0;
   #endif

PRIVATE void * getMemCleared(size_t numBytes)
{
   void *p;
   if( (p = malloc(numBytes)) != NULL)
   {
      _memclr(p, numBytes);
         #ifdef SHOW_MALLOCS
      printf("++%d %d / %lx\r\n", mallocCnt++, numBytes, p);
         #endif
      return p;
   }
   return NULL;
}

PRIVATE void myFree(void *p)
{
   if( p != NULL)
      { free(p); }

      #ifdef SHOW_MALLOCS
  printf("--%d %lx\r\n", --mallocCnt, p );
      #endif

}

#define _MaxMatchChks 5

typedef struct {
   RegexLT_T_MatchIdx idx;
   RegexLT_T_MatchLen len;
} S_OneMatchChk;

typedef struct {
   U8 numMatches;
   S_OneMatchChk ms[_MaxMatchChks];
} S_MatchesCheck;


typedef struct {
   C8 const        * regex;         // Search expression.
   C8 const        * src;           // String being searched.
   T_RegexRtn        rtn;           // Return from RegexLT_Match() or RegexLT_Replace().
   S_MatchesCheck    matchChk;      // The matches
   RegexLT_T_Flags   flags;         // For RegexLT_Match().
   C8 const        * replace;       // (optional) replace specifier. RegexLT_Replace() will be called if this exists; else RegexLT_Match()
   C8 const        * outChk;        // Expected result of replacement.
} S_Test;


#if 1
PRIVATE RegexLT_S_Cfg cfg = {
   .getMem        = getMemCleared,
   .free          = myFree,
   .printEnable   = FALSE,
   .maxSubmatches = 9,
   .maxRegexLen   = MAX_U8,
   .maxStrLen     = MAX_U8
};

PRIVATE C8 const matchPhone1[] = "\\d{3}[ \\-]?\\d{3}[ \\-]?\\d{4}";

PRIVATE S_Test const tests[] = {
   // Regex            Test string        Result code           Matches (if any)
   //                                                      {how_many [start, len]..}
   // ----------------------------------------------------------------------------
   { "abc",          "",                     E_RegexRtn_NoMatch,  {0, {}}              },       // The empty string is no-match
   { "",             "abc",                  E_RegexRtn_Match,    {1, {{0,3}}}         },       // An empty regex matches everything

   { "^abc$",        "abc",                  E_RegexRtn_Match,    {1, {{0,3}}}         },
   { "^abc$",        "abcd",                 E_RegexRtn_NoMatch,  {0, {}}              },
   { "^bcd$",        "abcd",                 E_RegexRtn_NoMatch,  {0, {}}              },

   // Word boundary (\b)
   { "\\bcat\\b",       "cat",               E_RegexRtn_Match,    {1, {{0,3}}}         },
   { "\\bcat",          "a cat",             E_RegexRtn_Match,    {1, {{2,3}}}         },
   { "\\bcat\\d",       "acat1 cat2",        E_RegexRtn_Match,    {1, {{6,4}}}         },

   { ".*def",        "abcdefghij",           E_RegexRtn_Match,    {1, {{0,6}}}         },
   { ".{2,}def",     "abcdefghij",           E_RegexRtn_Match,    {1, {{0,6}}}         },
   { ".*de{1}f",     "abcdeefghij",          E_RegexRtn_NoMatch,  {0, {}}              },
   { ".*de{2}f",     "abcdeefghij",          E_RegexRtn_Match,    {1, {{0,7}}}         },
   { ".*de{3}f",     "abcdeefghij",          E_RegexRtn_NoMatch,  {0, {}}              },
   { "def",          "abcdefghij",           E_RegexRtn_Match,    {1, {{3,3}}}         },
   { ".*d(e*)f",     "abcdeefghij",          E_RegexRtn_Match,    {2, {{0,7}, {4,2}}}  },
   { ".*d(ef)+",     "abcdefefghij",         E_RegexRtn_Match,    {2, {{0,8}, {4,4}}}  },
   { ".*de+f",       "abcdeefghij",          E_RegexRtn_Match,    {1, {{0,7}}}         },
   { "ab+",          "abbbbefghij",          E_RegexRtn_Match,    {1, {{0,5}}}         },
   { "b+",           "abbbbefghij",          E_RegexRtn_Match,    {1, {{1,4}}}         },
   { "ab+z",         "abbbbefghij",          E_RegexRtn_NoMatch,  {0, {}}              },
   { "b+z",          "abbbbefghij",          E_RegexRtn_NoMatch,  {0, {}}              },
   { "(dog)|cat",    "bigdogs",              E_RegexRtn_Match,    {2, {{3,3}, {3,3}}}  },
   { "dog|cat",      "pussycats",            E_RegexRtn_Match,    {1, {{5,3}}}         },
   { "cat|dog",      "bigdogs",              E_RegexRtn_Match,    {1, {{3,3}}}         },
   { "cat|(dog)",    "bigdogs",              E_RegexRtn_Match,    {2, {{3,3}, {3,3}}}  },
   { "dog|cat|pig",  "pussycats",            E_RegexRtn_Match,    {1, {{5,3}}}         },
   { "dog|cat|pig",  "porkypigs",            E_RegexRtn_Match,    {1, {{5,3}}}         },
   { "(do)g|cat",    "dobigdogs",            E_RegexRtn_Match,    {2, {{5,3}, {5,2}}}  },
   { "cat|(do)g",    "bigdogs",              E_RegexRtn_Match,    {2, {{3,3}, {3,2}}}  },
   { "cat|d(og)",    "bigdogs",              E_RegexRtn_Match,    {2, {{3,3}, {4,2}}}  },
   { "cat|d(og)",    "cutecats",             E_RegexRtn_Match,    {1, {{4,3}}}         },
   { "(cat|dog)",    "cutecats",             E_RegexRtn_Match,    {1, {{4,3}}}         },
   { "(cat)|dog",    "cutecats",             E_RegexRtn_Match,    {2, {{4,3}, {4,3}}}  },
   { "(cat|dog)s",   "cutecats",             E_RegexRtn_Match,    {1, {{4,4}}}         },
   { "[ps]dog",      "lapdogs",              E_RegexRtn_Match,    {1, {{2,4}}}         },
   { "(dog)|cat",    "bigdogs",              E_RegexRtn_Match,    {2, {{3,3}, {3,3}}}, 0,  "My pet is a $1",    "My pet is a dog"  },
   { "34+",          "2344456344448",        E_RegexRtn_Match,    {1, {{1,4}}}         },
   { "34+",          "2344456344448123445",  E_RegexRtn_Match,    {1, {{7,5}}},  _RegexLT_Flags_MatchLongest   },
   { "34+",          "2344456344448123445",  E_RegexRtn_Match,    {1, {{15,3}}}, _RegexLT_Flags_MatchLast      },
   { matchPhone1,    "414 777 9214",         E_RegexRtn_Match,    {1, {{0,12}}}  },
   { matchPhone1,    "414-777-9214",         E_RegexRtn_Match,    {1, {{0,12}}}  },
   { matchPhone1,    "tel 414-777-9214 nn",  E_RegexRtn_Match,    {1, {{4,12}}}  },
   { "\\(\\d{3}\\)[ \\-]?\\d{3}[ \\-]?\\d{4}",    "(414) 777 9214",      E_RegexRtn_Match,    {1, {{0,14}}}  },
   { "\\(?\\d{3}\\)?[ \\-]?\\d{3}[ \\-]?\\d{4}",    "(414)-777-9214 nn",  E_RegexRtn_Match,    {1, {{0,14}}}  },
   { "\\(?\\d{3}\\)?[ \\-]?\\d{3}[ \\-]?\\d{4}",    "414-777-9214 nn",  E_RegexRtn_Match,    {1, {{0,12}}}  },

   // Explosive quantifier
   { "^(a+)*b",      "aaab",                 E_RegexRtn_Match,    {2, {{0,4},{0,3}}}},
};

#else

PRIVATE RegexLT_S_Cfg cfg = {
   .getMem        = getMemCleared,
   .free          = myFree,
   .printEnable   = TRUE,
   .maxSubmatches = 9,
   .maxRegexLen   = MAX_U8,
   .maxStrLen     = 500
};

PRIVATE S_Test const tests[] = {
//   { "\\D\\d{5}(-\\d{4})?",          "Rustic 34 Rise, Oakfield 12345-6789",                     E_RegexRtn_Match,  {1, {{0,4}}}              },       // The empty string is no-match
   //{ "\\D\\d{5}(-\\d{4})?",          "Rustic 34 Rise, Oakfield 12345",                     E_RegexRtn_Match,  {1, {{0,4}}}              },       // The empty string is no-match
//   { "[ps]dog",      "lapdogs",              E_RegexRtn_Match,    {1, {{2,4}}}         },
   { "cat|d(og)",    "cutecats",             E_RegexRtn_Match,    {1, {{4,3}}}         },
};

#endif

/* ----------------------------- Checks rightOperator() ------------------------------ */

//#define TEST_RIGHT_OPERATOR

   #ifdef TEST_RIGHT_OPERATOR
extern C8 rightOperator(C8 const *rgx);

typedef struct {
   C8 const *tstStr;
   C8 rtn;
} S_TestRightOperator;

PRIVATE S_TestRightOperator const rightOpTests[] = {
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

   // Post operator capture group.
   { "(a)+",    '+' },
   { "(a){3,4}",'{' },

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

};

PRIVATE S_TestRightOperator const ropTstB[] = {
   { "abc",    '$' },
   { "ab+",     'E' },
};

//#define _ropTsts ropTstB
#define _ropTsts rightOpTests

PRIVATE void testRightOp(S_TestRightOperator const *tbl, U16 tblSize)
{
   U8 c, fails; C8 rtn;

   for(c = 0, fails = 0; c < tblSize; c++)
   {
      if( (rtn = rightOperator( tbl[c].tstStr)) != tbl[c].rtn )
      {
         printf("right operator fail: %s -> %c but got %c\r\n", tbl[c].tstStr, tbl[c].rtn, rtn);
         fails++;
      }
   }
   if(fails == 0)
   {
      printf("right operator - Passed %d tests\r\n", c);
   }
}
   #endif // #ifdef TEST_RIGHT_OPERATOR

/* -------------------------- end: Checks rightOperator() ------------------------------ */


// -------------------------------- dbgPrint --------------------------------------
#if 0
PRIVATE void dbgPrint(C8 const *fmt, ...)
{
   va_list argptr;
   va_start(argptr,fmt);

   if(cfg.printEnable)
   {
      vfprintf(stdout, fmt, argptr);
   }
   va_end(argptr);
}
#endif

/* ----------------------------------- matchesOK ---------------------------------

    Check matches [start,len] in 'ml' against checklist 'chk'. Print out any
    that are wrong; also if there are not the same number of matches as in 'chk'.
*/

PRIVATE BOOL matchesOK(RegexLT_S_MatchList const *ml, S_MatchesCheck const *chk)
{
   U8 c;
   BOOL rtn;

   if(ml->put > chk->numMatches)
   {
      printf("\tToo many matches: reqd %d got %d\r\n", chk->numMatches, ml->put);
   }

   for(c = 0, rtn = TRUE; c < chk->numMatches; c++)
   {
      if( c >= ml->put)             // More matches in check-list than were found (in 'ml')?
      {
         printf("\tToo few matches: got %d reqd %d\r\n", ml->put, chk->numMatches);
         rtn = FALSE;
         break;
      }
      RegexLT_S_Match const *m = &ml->matches[c];
      S_OneMatchChk const *ck = &chk->ms[c];

      if(m->idx != ck->idx || m->len != ck->len)
      {
         printf("\tWrong match: %d reqd [%d %d] got [%d %d]  [start,len]!\r\n",
                  c,  ck->idx, ck->len, m->idx,  m->len);
         rtn = FALSE;
      }
   }
   return rtn;
}



/* -------------------------------- runOneTest_PrintOneLine --------------------------------------*/

PRIVATE BOOL runOneTest_PrintOneLine(S_Test const *t)
{
   RegexLT_S_MatchList *ml;

   printf("   \'%-15s\' <- \'%-15s\'", t->regex, t->src);                           // Print the  regex and test string.

   if(t->replace == NULL)                                                           // This test is a match-only, no replace?
   {
      T_RegexRtn rtn = RegexLT_Match(t->regex, t->src, &ml, t->flags);              // Run match.

      if(rtn != t->rtn)                                                             // Incorrect result code?
      {
         printf("    incorrect return:  expected \'%s\', \'got\' %s\r\n", RegexLT_RtnStr(t->rtn), RegexLT_RtnStr(rtn) );
         return FALSE;
      }
      else                                                                          // else result code was correct.
      {
         if(rtn == E_RegexRtn_Match)                                                // Result code was 'match'?
         {
            RegexLT_PrintMatchList_OnOneLine(ml);                                   // then print matches
            printf("\r\n");                                                         // Drop a line, for the next test, but...
            return matchesOK(ml, &t->matchChk);                                            // if matches don't agree with those listed in the test, print the discrepancy
         }
         else                                                                       // else result code was something other than 'match'
         {
            printf("%s  (expected result, correct)\r\n", RegexLT_RtnStr(rtn));      // print that result, and not that it was correct.
         }
         RegexLT_FreeMatches(ml);                                                   // Done with matches.
         return TRUE;                                                               // Test passed
      }
   }
   else
   {
      #define _OutBufSize 100
      C8 out[_OutBufSize];

      T_RegexRtn rtn = RegexLT_Replace(t->regex, t->src, t->replace, out);

      if(rtn == E_RegexRtn_Match)
      {
         if( strncmp(out, t->outChk, _OutBufSize) != 0)
         {
            printf("    fail %-20s -> expected \'%s\' got \'%s\'", t->replace, t->outChk, out);
         }
         else
         {
            printf("\'%s\' -> \'%s\'\r\n", t->replace, out);
         }
         return TRUE;
      }
      else
      {
         printf("    fail:  returned: \'%s\'\r\n", RegexLT_RtnStr(rtn));
         return FALSE;
      }
   }

}

// -------------------------------- main --------------------------------------

int main()
{
   RegexLT_Init(&cfg);

      #ifdef TEST_RIGHT_OPERATOR
   //testRightOp(rightOpTests, RECORDS_IN(rightOpTests));
   testRightOp(_ropTsts, RECORDS_IN(_ropTsts));
   return 1;
      #endif

//   C8 out[100];
//
//   RegexLT_Replace( "(dog)|cat", "bigdogs", "My pet is a $1", out);
//   printf("Out: %s\r\n", out);
//   return 1;

   U8 c, fails;
   for(c = 0, fails = 0; c < RECORDS_IN(tests); c++)
   {
      printf("%-2d: ", c);

      if( runOneTest_PrintOneLine(&tests[c]) == FALSE)
         { fails++; }
   }
   if(fails == 0) { printf("\r\n----- All Passed -----\r\n"); } else { printf("\r\n------- %d Fail(s) --------\r\n", fails); }
   return 1;
}

// ---------------------------------------- end -----------------------------------------------------
