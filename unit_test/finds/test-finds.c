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


/* ----------------------------------- matchesOK ---------------------------------

    Check matches [start,len] in 'ml' against checklist 'chk'. Print out any
    that are wrong; also if there are not the same number of matches as in 'chk'.
*/

PRIVATE BOOL matchesOK(C8 *out, RegexLT_S_MatchList const *ml, S_MatchesCheck const *chk)
{
   U8 c;
   BOOL rtn;
   out[0] = '\0';

   if(ml->put > chk->numMatches)
   {
      sprintf(out, "\tToo many matches: reqd %d got %d\r\n", chk->numMatches, ml->put);
   }

   for(c = 0, rtn = TRUE; c < chk->numMatches; c++)
   {
      if( c >= ml->put)             // More matches in check-list than were found (in 'ml')?
      {
         sprintf(out, "\tToo few matches: got %d reqd %d\r\n", ml->put, chk->numMatches);
         rtn = FALSE;
         break;
      }
      RegexLT_S_Match const *m = &ml->matches[c];
      S_OneMatchChk const *ck = &chk->ms[c];

      if(m->idx != ck->idx || m->len != ck->len)
      {
         sprintf(out, "\tWrong match: %d reqd [%d %d] got [%d %d]  [start,len]!\r\n",
                  c,  ck->idx, ck->len, m->idx,  m->len);
         rtn = FALSE;
      }
   }
   return rtn;
}



/* -------------------------------- runOneTest_PrintOneLine --------------------------------------*/

#define _PrintFailsOnly false
#define _PrintAllTests  true

PRIVATE BOOL runOneTest_PrintOneLine(U16 idx, S_Test const *t, bool printAllTests)
{
   RegexLT_S_MatchList *ml = NULL;

   C8 rgx[200];
   sprintf(rgx, "%-2d:   \'%-15s\' <- \'%-15s\'", idx, t->regex, t->src);           // Print the  regex and test string.

   if(t->replace == NULL)                                                           // This test is a match-only, no replace?
   {
      T_RegexRtn rtn = RegexLT_Match(t->regex, t->src, &ml, t->flags);              // Run match.

      if(rtn != t->rtn)                                                             // Incorrect result code?
      {
         printf("%s incorrect return:  expected \'%s\', \'got\' %s\r\n", rgx, RegexLT_RtnStr(t->rtn), RegexLT_RtnStr(rtn) );
         return FALSE;
      }
      else                                                                          // else result code was correct.
      {
         if(rtn == E_RegexRtn_Match)                                                // Result code was 'match'?
         {

            C8 b0[100];
            bool rtn = matchesOK(b0, ml, &t->matchChk);                             // if matches don't agree with those listed in the test, print the discrepancy

            if(rtn == false || printAllTests == true)
            {
               printf("%s", rgx);
               RegexLT_PrintMatchList_OnOneLine(ml);                                // then print matches
               printf("%s\r\n", b0);                                                // Drop a line, for the next test, but...
            }

            return rtn;
         }
         else                                                                       // else result code was something other than 'match'
         {
            if(printAllTests == true)
            {
               printf("%s %s  (expected result, correct)\r\n", rgx, RegexLT_RtnStr(rtn));      // print that result, and not that it was correct.
            }
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
            if(printAllTests == true)
            {
               printf("%s \'%s\' -> \'%s\'\r\n", rgx, t->replace, out);
            }
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

// -------------------------------- test_Finds --------------------------------------

void test_Finds(void)
{
   C8 const matchPhone1[] = "\\d{3}[ \\-]?\\d{3}[ \\-]?\\d{4}";

   S_Test const tests[] = {
      // Regex            Test string        Result code           Matches (if any)
      //                                                      {how_many [start, len]..}
      // ----------------------------------------------------------------------------
      //{ "fob_([\\d]{5,10})_([\\d]{1,3})\\.log", "fob_098765432_1.log",    E_RegexRtn_Match,  {3, {{0,19}, {4,9}, {14,1}}}             },       // The empty string is no-match
#if 1
      { "abc",          "",                     E_RegexRtn_NoMatch,  {0, {}}              },       // The empty string is no-match
      { "",             "abc",                  E_RegexRtn_Match,    {1, {{0,3}}}         },       // An empty regex matches everything

      { "^abc$",        "abc",                  E_RegexRtn_Match,    {1, {{0,3}}}         },
      { "^abc$",        "abcd",                 E_RegexRtn_NoMatch,  {0, {}}              },
      { "^bcd$",        "abcd",                 E_RegexRtn_NoMatch,  {0, {}}              },

      // Word boundary (\b)
      { "\\bcat\\b",       "cat",               E_RegexRtn_Match,    {1, {{0,3}}}         },       // Start of string is also start of word
      { "\\bcat",          "a cat",             E_RegexRtn_Match,    {1, {{2,3}}}         },       // Start of word inside a string.
      { "\\bcat\\d",       "acat1 cat2",        E_RegexRtn_Match,    {1, {{6,4}}}         },       // Ignore 'cat1' because it's not at start of word.

      { ".*def",        "abcdefghij",           E_RegexRtn_Match,    {1, {{0,6}}}         },       // start to 'def' -> 'abcdef'
      { ".{2}def",      "aaadefghij",           E_RegexRtn_Match,    {1, {{1,5}}}         },       // Exactly 2 + def -> 'bcdef'
      { "a{2}def",      "aaadefghi",            E_RegexRtn_Match,    {1, {{1,5}}}         },       // Exactly 2 + def -> 'bcdef'

      // Repeat counts
      //{ "a{2}def",      "abcdefghij",           E_RegexRtn_Match,    {1, {{1,6}}}         },       // Exactly 2 + def -> 'bcdef'
      { ".{0,}def",     "abcdefghij",           E_RegexRtn_Match,    {1, {{0,6}}}         },       // 0 or more + 'def' -> 'abcdef'
      { ".{2,}def",     "abcdefghij",           E_RegexRtn_Match,    {1, {{0,6}}}         },       // Exactly 2 + def -> 'bcdef'
      { ".{3,}def",     "abcdefghij",           E_RegexRtn_Match,    {1, {{0,6}}}         },       // 3 or more + 'def' -> 'abcdef'
      { ".{4,}def",     "abcdefghij",           E_RegexRtn_NoMatch,  {1, {}}              },       // 4 or more + 'def', can't satisfy -> no match

      { ".*dex{0}f",     "abcdefghij",          E_RegexRtn_Match,    {1, {{0,6}}}         },       // Zero repeats of 'x' within 'def' matches 'def'.

      { ".*de{2}f",     "abcdeefghij",          E_RegexRtn_Match,    {1, {{0,7}}}         },       // Exactly 2  'e' in 'deef' -> Match
      { ".*de{1,2}f",   "abcdeefghij",          E_RegexRtn_Match,    {1, {{0,7}}}         },       // 1 or 2  'e' in 'deef' -> Match
      { ".*de{1,3}f",   "abcdeefghij",          E_RegexRtn_Match,    {1, {{0,7}}}         },       // 1..3  'e'  in 'deef' -> Match
      { ".*de{2,4}f",   "abcdeefghij",          E_RegexRtn_Match,    {1, {{0,7}}}         },       // 2..4  'e'  in 'deef' -> Match

      { ".*de{1}f",     "abcdeefghij",          E_RegexRtn_NoMatch,  {0, {}}              },       // Exactly 1 'e' in 'deef' -> no match.
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

      // Indexed filename.
      { "fob_([\\d]{6})_([\\d]{1,3})\\.log",    "fob_123456_123.log",    E_RegexRtn_Match,  {3, {{0,18}, {4,6}, {11,3}}}             },       // The empty string is no-match
      { "fob_([\\d]{5,10})_([\\d]{1,3})\\.log", "fob_098765432_1.log",    E_RegexRtn_Match,  {3, {{0,19}, {4,9}, {14,1}}}             },       // The empty string is no-match
#endif
   };

   RegexLT_S_Cfg cfg = {
      .getMem        = getMemCleared,
      .free          = myFree,
      .printEnable   = _TRACE_PRINTS_ON,
      .maxSubmatches = 9,
      .maxRegexLen   = MAX_U8,
      .maxStrLen     = MAX_U8
   };

   RegexLT_Init(&cfg);

   U8 c, fails;
   for(c = 0, fails = 0; c < RECORDS_IN(tests); c++)
   {
      tdd_TestNum = c;
      if( runOneTest_PrintOneLine(c, &tests[c], _PrintFailsOnly) == FALSE)
         { fails++; }
   }
   if(fails > 0)
   {
      printf("\r\n------- %d Fail(s) --------\r\n", fails);
      TEST_FAIL();
   }
}


/* -------------------------------- test_IPAddr -------------------------------------------- */

void test_IPAddr(void)
{
   S_Test const tests[] = {
      // Regex            Test string        Result code           Matches (if any)
      //                                                      {how_many [start, len]..}
      // ----------------------------------------------------------------------------

      // ---- Simple match - any of 1-3 digits for each byte.
#if 0
      { "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}",        "0.0.0.0",            E_RegexRtn_Match,    {1, {{0,7}}}},
      { "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}",        "255.255.255.255",    E_RegexRtn_Match,    {1, {{0,15}}}},
      { "[\\d]{1,3}\\.[\\d]{1,3}\\.[\\d]{1,3}\\.[\\d]{1,3}",        "255.255.255.255",    E_RegexRtn_Match,    {1, {{0,15}}}},

      // Fails, wrong number of internal digits
      { "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}",        "255.255.255.",       E_RegexRtn_NoMatch,    {0, {}}   },
      { "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}",        "255.2550.255.255",   E_RegexRtn_NoMatch,    {0, {}}   },

      // Disallow extra digits at each end.
      //{ "[^\\d][0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}[^\\d]",    "255.255.255.255",   E_RegexRtn_Match,    {1, {{0,15}}}},
      { "[^0-9][0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}[^0-9]",    "0255.255.255.255",   E_RegexRtn_NoMatch,    {0, {}}   },
#endif
      { "[^0-9][0-9]{1,3}\\.[0-9]{1,3}",        "123.4",    E_RegexRtn_NoMatch,    {0, {}}},
   };

   RegexLT_S_Cfg cfg = {
      .getMem        = getMemCleared,
      .free          = myFree,
      .printEnable   = _TRACE_PRINTS_ON,
      .maxSubmatches = 9,
      .maxRegexLen   = MAX_U8,
      .maxStrLen     = MAX_U8 };

   RegexLT_Init(&cfg);

   U8 c, fails;
   for(c = 0, fails = 0; c < RECORDS_IN(tests); c++)
   {
      tdd_TestNum = c;
      if( runOneTest_PrintOneLine(c, &tests[c], _PrintFailsOnly) == FALSE)
         { fails++; }
   }
   if(fails > 0)
   {
      printf("\r\n------- %d Fail(s) --------\r\n", fails);
      TEST_FAIL();
   }
}

// ----------------------------------------- eof --------------------------------------------
