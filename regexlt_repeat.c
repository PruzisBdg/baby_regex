/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Repeat Specifier Parsing
|
--------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "libs_support.h"
#include "util.h"
#include "arith.h"
#include "regexlt_private.h"

/* ----------------------------- regexlt_parseRepeat ----------------------------------

   Parses a regex repeat specifier from 'ch' into 'r'. 'ch' starts past the
   opening '{'

   Legal constructions (ignoring opening '}') are:

        chars             min      max
        ------------------------------------
         {3}               3        3
         {4,}              4        _MaxRepeats
         {4,6}             4        6

   The parser ignores up to 10 whitespace i.e {7,10} == {7, 10} == { 7 , 10   }  etc

   Return E_Fail if can't parse OR if min > max; else returns E_Complete. If
   success then 'ch' is moved to the char after the closing '}'; otherwise it
   is left unchanged.

   If fail then r.min, r.max are zeroed.
*/

PUBLIC T_ParseRtn regexlt_parseRepeat(S_RepeatSpec *r, C8 const **ch)
{
   S16 n;
   U8 c;
   C8 const *p = *ch;

   if( (p = (C8*)ReadDirtyASCIIInt((U8 const*)p, &n)) == NULL)    // Didn't snag 1st number?
   {
      goto Fail;                                                  // then we fail rightaway.
   }
   else                                                           // else we got a number
   {
      if( n < 0 || n > _MaxRepeats)                               // But number isn't legal? ( ReadDirtyASCIIInt() will parse e.g '-45')
      {
         goto Fail;                                               // then fail
      }
      else                                                        // else got a legal 1st number...
      {
         for(c = 0; c < 10; c++, p++)                              // Now look for ',' or closing '}'.
         {
            if(*p == '\0')                                         // End of regex string?
            {
               goto Fail;                                         // Oops!
            }
            else if(*p == '}' )                                   // Got '{n}'
            {
               r->min = n; r->max = n;                            // then 'min' and 'max' are the same.
               goto Success;
            }
            else if( isspace(*p))                                 // Whitespace?
               { }                                                // continue (but no more than 10-of, above)
            else if(*p == ',')                                    // Comma?
            {
               r->min = n;                                        // then 1st number we got is 'min'

               // Got 1st number and ','. Look for 2nd number.
               C8 *p1;

               if( (p1 = (C8*)ReadDirtyASCIIInt( (U8 const*)p, &n)) == NULL)   // No 2nd number?
               {                                                  // so must close with '}'
                  for(c = 0, p++; c < 10; c++, p++)               // Starting beyond the ',', try up to 10 chars
                  {
                     if(*p == '}')                                // Closing '}'?
                     {
                        r->max = _Repeats_Unlimited;              // No number after ',' is implied 'no-maximum-repeats'
                        goto Success;
                     }
                     else if(*p == '\0' || !isspace(*p))          // end of regex OR some other printing char
                     {
                        goto Fail;
                     }
                  } // for(c = 0; c < 10; c++)
               }
               else                                               // else got a 2nd number
               {
                  p = p1;                                         //  Set current ptr to the provisional 'p1', now we know it's after the number we snagged.

                  if( n < 0 || n > _MaxRepeats)                   // 2nd number not legal?
                  {
                     goto Fail;
                  }
                  else                                            // else 2nd number is legal
                  {
                     for(c = 0; c < 10; c++, p++)                 // Look for legal close; try up to 10 chars.
                     {
                        if(*p == '}')                             // Got closing '}'?
                        {
                           if(r->min > n)                         // but 1st number (min) is greater then 2nd (max)?
                           {                                      // This gentlemen, cannot be, ...
                              goto Fail;                          // ...so fail.
                           }
                           else                                   // else legal sequence e.g {3,6]} or {8,8}
                           {
                              r->max = n;                         // So max is 2nd number.
                              goto Success;
                           }
                        }
                        else if(*p == '\0' || !isspace(*p))       // End of regex or some printing char?
                        {
                           goto Fail;                             // Illegal format. Fail.
                        }
                     } // for(c = 0; c < 10; c++)
                     goto Fail;
                  }
               }
            }
            else if(isprint(*p))       // Was looking for ',' or closing '}' (above). But got something else printable?
            {
               goto Fail;              // which is illegal -> fail.
            }
         } // for(c = 0; c < 10; c++)
        goto Fail;                     // More than 10 whitespace? Too many; Fail.
      }
   }

Fail:
   r->min = 0; r->max = 0;             // ... so set them both to zero...
   r->valid = FALSE;
   return E_Fail;                      // ...and fail.

Success:
   r->valid = TRUE;
   *ch = p+1;                          // We succeeded so 'p' is at closing '}'. Advance source ptr to one-past that.
   return E_Complete;
}


// --------------------------------------------- eof --------------------------------------------
