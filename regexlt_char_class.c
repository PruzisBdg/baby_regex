/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Character-class parsing
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

#define dbgPrint regexlt_dbgPrint

#define classParser_Init   regexlt_classParser_Init
#define classParser_AddDef regexlt_classParser_AddDef
#define classParser_AddCh  regexlt_classParser_AddCh
#define getCharClassByKey  regexlt_getCharClassByKey

/* -------------------------------- regexlt_getCharClassByKey --------------------------- */

typedef struct {
   C8        tag;    // e.g the 'd' in '\d'.
   C8 const *def;    // which becomes '[0-9]'
} S_ClassMap;

// These char class definitions are legal OUTSIDE a '[... ]'.
PRIVATE S_ClassMap const prebakedCharClasses[] = {
   { 'd', "[0-9]"          },
   { 'w', "[0-9A-Za-z_]"   },
   { 's', "[ \t\r\n]"      }
};

PUBLIC C8 const *regexlt_getCharClassByKey(C8 key)
{
   U8 c;
   for(c = 0; c < RECORDS_IN(prebakedCharClasses); c++) {
      if(prebakedCharClasses[c].tag == key) {
         return  prebakedCharClasses[c].def; }}
   return NULL;
}

// ' -~' is everything from SPC (0x20) to tilde (0x7E).
//PRIVATE C8 const allPrintablesAndWhtSpc[] = "[\t\r\n -~]";

/* ------------------------ Character class Parser ------------------------------------

   Reads e.g [A-P0-8as], puts chars in the class into a S_C8bag.
*/
/* ------------------------------- classParser_Init --------------------------------- */

PUBLIC void regexlt_classParser_Init(S_ParseCharClass *p)
{
   p->prevCh = '[';     // Class was opened with '['.
   p->negate = FALSE;   // Until there's a '^'
   p->range = FALSE;    // Until there's e.g A-Z
   p->esc = FALSE;      // Until there's a '\'.
}



/* ------------------------------------ regexlt_classParser_AddCh ------------------------------------

   Process first/another 'newCh' for the character class being built in 'cc' using the
   (stateful) parser 'p';

   Return E_Complete if 'newCh' completes the class definition, 'E_Fail' if 'newCh' is
   illegal, or E_Continue if 'newCh' has been added and we need more input to complete the
   class.
*/

PUBLIC T_ParseRtn regexlt_classParser_AddCh(S_ParseCharClass *p, S_C8bag *cc, C8 newCh)
{
   switch(newCh)
   {
      case '\0':                                      // Incomplete class expression -> always fail
      case '[':                                       // Cannot nest classes -> always fail
         return E_Fail;

      case ']':                                       // Closing a class?...
         if(p->range == TRUE || p->negate == TRUE)    // ...but did not complete range or negation?
         {
            return E_Fail;                            // then fail
         }
         else                                         // else legal completion of a class
         {
            return E_Complete;
         }
         break;

      case '-':                                       // ...Range char?
         if(p->esc == TRUE)                           // Prev was esc?
         {
            C8bag_AddOne(cc, '-');                    // then it's a literal '-'. Add it to current class
            p->esc = FALSE;                           // and we have used the ESC.
         }
         else if(p->prevCh == '[' || p->prevCh == '-' || p->prevCh == '^')   // But previous char was not a letter/number whatever?
         {
            return E_Fail;                            // so range is missing it's start -> fail
         }
         else
         {
            p->range = TRUE;                          // else we are specifying a range now; continue.
         }
         break;

      case '^':                                       // ...Negation?
         if(p->esc == TRUE)                           // Prev was esc?
         {
            C8bag_AddOne(cc, '^');                    // then it's a literal '^'. Add it to current class
            p->esc = FALSE;                           // and we have used the ESC.
         }
         else if(p->prevCh == '-' || p->prevCh == '^' || // But just opened a negation or range? OR
            p->negate == TRUE)                        // were already negating (a range)?
         {
            return E_Fail;                            // then illegal construction -> fail
         }
         else
         {
            p->negate = TRUE;                          // else we are negating now; continue.
         }
         break;

      case '\\':                                      // Opens a pre-defined character class or an in-range escaped literal.
         if(p->prevCh == '-' || p->range == TRUE)     // But we are specifying a range?... can't put a pre-defined class within that..
         {
            return E_Fail;                            // so fail
         }
         else
         {
            p->esc = TRUE;                            // else mark the escape, to be followed, presumably, by the class specifier.
         }
         break;
#if 0
      case '.':                                       // Includes everything
         if(p->prevCh == '-' || p->range == TRUE)     // Again, not legal in a range-specifier
         {
            return E_Fail;                            // so fail
         }
         else                                         // otherwise good... make a class with all printables & whitespace.
         {
            S_ParseCharClass pc;
            classParser_Init(&pc);
            if( classParser_AddDef(&pc, cc, allPrintablesAndWhtSpc) == TRUE ) {  // Made the new class?
               break;                                 // ... success, continue
            }
            else                                      // else failed to make new class
            {
               return E_Fail;                         // ejected!!
            }
         }
         break;
#endif
      default:                                        // ...just a letter/number whatever.
         if(p->esc == TRUE)                           // Which was escaped?
         {
            p->esc = FALSE;                           // Will handle ESC; clear flag
            C8 const *def;

            if((def = getCharClassByKey(newCh)) != NULL)    // Is a legal escape char?
            {                                               // then will make a new class corresponding to the char
               S_ParseCharClass pc;                         // We are already inside a class parser; make a fresh private one.
               classParser_Init(&pc);
               if( classParser_AddDef(&pc, cc, def) == TRUE ) {   // Made the new class?
                  break;                                    // success, continue
               }
            }
            return FALSE;
         }
         else if(p->range == TRUE)                    // We were specifying a range?
         {
            p->range = FALSE;                         // then we close it now.

            if(newCh < p->prevCh)                     // End-of-range is less than start?
            {
               return E_Fail;                         // that's a fail right there.
            }
            else                                      // else legal range.
            {
               if(p->negate == TRUE)                  // Remove these chars from the range specified so far?
                  { C8bag_RemoveRange(cc, p->prevCh, newCh); p->negate = FALSE; }    // This negation is done.
               else                                   // else add to any specified so far.
                  { C8bag_AddRange(cc, p->prevCh, newCh); }
            }
         }
         else                                         // else not a range; just a lone char.
         {
            if(p->negate == TRUE)                     // To be removed from existing range?
               { C8bag_RemoveOne(cc, newCh); p->negate = FALSE; }   // We are done with this negation.
            else                                      // else to be added to existing range
               { C8bag_AddOne(cc, newCh); }
         }
         p->prevCh = newCh;            // Remember this char for next time.
   }
   return E_Continue;   // Didn't fail or complete above, so continue.
}


/* -------------------------------------- regexlt_classParser_AddDef ---------------------------- */

PUBLIC BOOL regexlt_classParser_AddDef(S_ParseCharClass *p, S_C8bag *cc, C8 const *def)
{
   if(def[0] != '[')                            // Must open with '['?
      { return FALSE; }                         // Didn't so fail.
   else
   {
      def++;                                    // else proceed to 1st char past opening '['...
      T_ParseRtn rtn;                           // and process the class specifier...
      while( (rtn = classParser_AddCh(p, cc, *def)) == E_Continue)
         { def++;}                              // ... a char at a time, until Fail or Done
      return rtn == E_Complete ? TRUE : FALSE;
   }
}



// ------------------------------------------------- eof -------------------------------------------
