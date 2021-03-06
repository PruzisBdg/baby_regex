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
   { 'd', "[0-9]"           },            // Numbers
   { 'w', "[0-9A-Za-z_]"    },            // Alphanumeric & '_'
   { 's', "[ \t\r\n\f\v]"   },            // Whitespace.
   // Negated classes.
   { 'D', "[^0-9]"          },
   { 'W', "[^0-9A-Za-z_]"   },
   { 'S', "[^ \t\r\n\f\v]"  }
};

PUBLIC C8 const *regexlt_getCharClassByKey(C8 key)
{
   U8 c;
   for(c = 0; c < RECORDS_IN(prebakedCharClasses); c++) {
      if(prebakedCharClasses[c].tag == key) {
         return  prebakedCharClasses[c].def; }}
   return NULL;
}

/* ------------------------------- nonPrintable -------------------------------------------

   Translate the 2nd char of a non-printables when the non-printable is presented as text
   e.g '\r' = [0x2D, 0x72].
*/
PRIVATE C8 nonPrintable(C8 ch)
{
   switch(ch) {
      case 'r':   return '\r';
      case 'n':   return '\n';
      case 't':   return '\t';
      case 'a':   return '\a';
      case 'e':   return '\e';
      case 'f':   return '\f';
      case 'v':   return '\v';
      default:    return 0; }
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
   p->negateCh = FALSE; // until at least one char after '^'.
   p->hex.step = 0;     // Until we hit 'x' in e.g '\x25'
   p->hex._1stDigit = 0;
}

/* ---------------------------------- addOrRemove -------------------------------------- */

PRIVATE void addOrRemove(S_ParseCharClass *p, S_C8bag *cc, C8 newCh)
{
   if(p->negate == TRUE) {             // To be removed from existing range?
      C8bag_RemoveOne(cc, newCh);
      p->negateCh = TRUE; }            // Mark that at least 1 char followed '[^'.
   else                                // else to be added to existing range
      { C8bag_AddOne(cc, newCh); }
}

PRIVATE BOOL addOrRemoveRange(S_ParseCharClass *p, S_C8bag *cc, C8 from, C8 to)
{
   if(p->negate == TRUE) {
      BOOL rtn = C8bag_RemoveRange(cc, from, to);
      return rtn; }
   else
      { return C8bag_AddRange(cc, from, to); }
}

/* ------------------------------------ regexlt_classParser_AddCh ------------------------------------

   Process first/another 'newCh' for the character class being built in 'cc' using the
   (stateful) parser 'p'.  The Parser starts at the 1st char after an opening '['
   and ends on ']' or earlier the payload in '[xxxxx]' is illegal

   Return E_Complete if 'newCh' completes the class definition, 'E_Fail' if 'newCh' is
   illegal, or E_Continue if 'newCh' has been added and we need more input to complete the
   class.
*/
PUBLIC T_ParseRtn regexlt_classParser_AddCh(S_ParseCharClass *p, S_C8bag *cc, C8 const *src)
{
   C8 newCh = *src;

   /* 'closedRange' means e.g '0-9'. If 'newCh' is '-' then it's treated as a literal below.
      For all other chars clear the flag here.
   */
   if(newCh != '-')
      { p->closedRange = FALSE; }

   /* If already got 1 HexASCII digit e.g '\x4' and the next char is not HexASCII, then the
      the number is from the 1st digit only e.g '\x4z' -> 0x04 + 'z'.
   */
   if(p->hex.step == 2 && !IsHexASCII(newCh))
   {
      addOrRemove(p, cc, p->hex._1stDigit);
      p->prevCh = p->hex._1stDigit;
      p->hex.step = 0;
   }

   switch(newCh)
   {
      case '\0':                                      // ... Exhausted 'src' but incomplete class expression -> fail
         return E_Fail;

      case '[':                                       // Parsing this class but got another '['
         if(p->esc == TRUE) {                                  // Escaped i.e '\[' ?
            p->esc = FALSE;
            addOrRemove(p, cc, '[');                           // then add as literal.
            break; }
         else
            { return E_Fail; }                                 // else cannot nest classes... Fail

      case ']':                                       // ']' (usually) closes a class
         if(p->esc == TRUE) {                                  // Is escaped i.e '\]' ?
            p->esc = FALSE;
            addOrRemove(p, cc, ']');                           // then add as a literal.
            break; }                                           // Closing a class?...

         else if(p->negate == TRUE && p->negateCh == FALSE)    // No char after a '[^' now '[^]' whicb is illegal?
            { return E_Fail; }                                 // then fail

         else if(p->range == TRUE)                             // Closed class with unfinished range? e.g '[^0-]'?
         {
            addOrRemove(p, cc, p->prevCh);                     // then both '-' and the char preceding it are literals...
            addOrRemove(p, cc, '-');                           // ...add both or remove both if negated ('^').
            return E_Complete;                                 // and we are done.
         }
         else                                                  // else legal completion of a class
            { return E_Complete; }
         break;

      case '-':                                       // ...Range char?
         p->negateCh = TRUE;     // Mark at least 1 char after as '^'

         if(p->range == TRUE)
         {
            p->range = FALSE;
            p->closedRange = TRUE;                             // and mark that we closed it.

            /* Add or remove range depending on 'p->negate'. Will fail if range is upside down
               i.e 'newCh > 'hexascii' or if either of these is < 0.
            */
            if(FALSE == addOrRemoveRange(p, cc, p->prevCh, '-')) {
               return E_Fail; }
         }
         /* If '[-' or '\-' (esc) or e.g '0-9-' (closedRange) then this '-' is literal; not part
            of a range.
         */
         else if(p->esc == TRUE || p->prevCh == '[' || p->closedRange == TRUE)  // '-' is a literal?
         {
            C8bag_AddOne(cc, '-');                             // then it's a literal '-'. Add it to current class
            p->closedRange = FALSE;                            // If closed range then that is past now.
            p->esc = FALSE;                                    // and we have used the ESC.
            p->prevCh = '-';
         }
         else
            { p->range = TRUE; }                               // else we are specifying a range now; continue.
         break;

      case '^':                                       // ... Negation? (maybe)
         if(p->range == TRUE)
         {
            p->range = FALSE;
            p->closedRange = TRUE;                             // and mark that we closed it.

            /* Add or remove range depending on 'p->negate'. Will fail if range is upside down
               i.e 'newCh > 'hexascii' or if either of these is < 0.
            */
            if(FALSE == addOrRemoveRange(p, cc, p->prevCh, '^')) {
               return E_Fail; }
         }
         else if(p->prevCh == '[' && p->negate == FALSE && p->esc == FALSE)   // '^' right after '['? (not escaped, not already '^'ed)
         {
            p->negate = TRUE;                                  // else we are negating now; continue.
            C8bag_Invert(cc);                                  // so invert the initial empty class to make a full one.
         }
         else                                                  // else not '[^'; treat as a literal '^'.
         {
            addOrRemove(p, cc, '^');                           // Add or remove '^'
            p->esc = FALSE;
            p->prevCh = '^';
         }
         break;

      case '\\':                                      // ... Opens a pre-defined character class or an in-range escaped literal.
         if(p->esc == TRUE)
         {
            addOrRemove(p, cc, '\\');
            p->esc = FALSE;
         }
         else if(p->range == TRUE && *(src+1) != 'x')
         {
            p->range = FALSE;
            p->closedRange = TRUE;
            p->esc = FALSE;
            if(FALSE == addOrRemoveRange(p, cc, p->prevCh, '\\')) {
               return E_Fail; }
         }
         else
            { p->esc = TRUE; }                                 // else mark the escape, to be followed, presumably, by the class specifier.
         break;

      default:                                        // ... some other letter/number whatever.
      {
         if(p->hex.step > 0)                                   // Parsing a hex e.g '\x4c'?
         {
            if (!IsHexASCII(newCh) )                           // But this next char isn't Hex ASCII?
               { return E_Fail; }                              // So fail rightaway
            else
            {
               if(p->hex.step == 1)                            // HexASCII for high nibble
               {
                  p->hex._1stDigit = HexToNibble(newCh);       // read it.
                  p->hex.step = 2;
                  break;
               }
               else                                            // else HexASCII for low nibble.
               {                                               // Convert to byte and add/remove from bag.
                  C8 hexascii = (p->hex._1stDigit << 4) + HexToNibble(newCh);

                  /* This Regex only supports ASCII 0...7F inside a range. PCRE handles 0...FF but limiting
                     to 0x7F keeps the store needed for a range (C8bag) to 8 bytes, instead of 16 (U8bag).
                  */
                  if(hexascii < 0)                             // Outside 0...0x7F?
                     { return E_Fail; }                        // then fail?
                  else                                         // else hexascii is 0...7F
                  {
                     if(p->range == TRUE)                      // Were specifying a range?
                     {
                        p->range = FALSE;                      // then leave it now
                        p->closedRange = TRUE;                 // and mark that we left.

                        /* Add or remove range depending on 'p->negate'. Will fail if range is upside down
                           i.e 'prevCh > 'hexascii' or if either of these is < 0.
                        */
                        if(FALSE == addOrRemoveRange(p, cc, p->prevCh, hexascii)) {
                           return E_Fail; }
                     }
                     else
                     {
                        p->hex.step = 0;                       // and we are done reading hex.
                        p->prevCh = hexascii;                  // Record this hexascii, even though we added it. It may be the start of a range e.g '\x33-\x44'.

                        if(*(src+1) != '-')
                           { addOrRemove(p, cc, hexascii); }   // So add 'hexascii' or remove it if it is negated.
                        else
                           { break; }
                     }
                  }
               }
            }
         } // if(p->hex.step > 0)

         else if(p->esc == TRUE)                               // Some escaped char?
         {
            p->esc = FALSE;                                    // Will handle ESC; clear flag

            if(newCh == 'x')                                   // '\x'?
            {
               p->hex.step = 1;                                // this starts a hex number e.g '\x9a'
               return E_Continue;
            }
            else if( nonPrintable(newCh) != 0 )                // else non-printable, presented as text?  e.g e.g '\r' = [0x2D, 0x72].
            {
               addOrRemove(p, cc, nonPrintable(newCh));        // Then translate to ASCII  and add to list.
            }
            else                                               // else it should be some 'escaped' class e.g '\D'
            {
               if(*(src+1) == '-')                             // Next char is a range?
               {                                               // then, because a Char Class cannot open a range. Preceding '\\' must be a literal
                  addOrRemove(p, cc, '\\');                    // So add '\\' now.
               }
               else                                            // else e.g '\d' a Char Class. Try to add it.
               {
                  C8 const *def;

                  if((def = getCharClassByKey(newCh)) != NULL)    // Is a legal class-specifier
                  {                                               // then will make a new class corresponding to the char
                     S_ParseCharClass pc;                         // We are already inside a class parser; make a fresh private one.
                     classParser_Init(&pc);
                     if( classParser_AddDef(&pc, cc, def) == TRUE ) {   // Made the new class?
                        p->prevCh = pc.prevCh;
                        break; }                                  // success, leave 'default:' without doing 'p->prevCh = newCh'
                  }
                  return E_Fail;                                  // 'newCh' was not a Class Specifier OR could add the class it described.
               }
            }
         }
         else if(p->range == TRUE) {                           // We were specifying a range?
            p->range = FALSE;                                  // then we close it now.
            p->closedRange = TRUE;                             // and mark that we closed it.

            /* Add or remove range depending on 'p->negate'. Will fail if range is upside down
               i.e 'newCh > 'hexascii' or if either of these is < 0.
            */
            if(FALSE == addOrRemoveRange(p, cc, p->prevCh, newCh)) {
               return E_Fail; }}

         else if(*(src+1) != '-')                              // else not end of a range. NOT the start of a range either?
            { addOrRemove(p, cc, newCh); }                     // then just a lone char; add or remove it.

      // Got here if processed an actual char, not '-', '^' or '\'. Remember it - e.g if it's start of a range.
      p->prevCh = newCh;
      } // default:
   } // switch(newCh)

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
      while( (rtn = classParser_AddCh(p, cc, def)) == E_Continue)
         { def++;}                              // ... a char at a time, until Fail or Done
      return rtn == E_Complete ? TRUE : FALSE;
   }
}



// ------------------------------------------------- eof -------------------------------------------
