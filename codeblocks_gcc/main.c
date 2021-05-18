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


// -------------------------------- main --------------------------------------

extern void test_CharClass(void);

int main()
{
   test_CharClass();
   return 1;
}

// ---------------------------------------- end -----------------------------------------------------
