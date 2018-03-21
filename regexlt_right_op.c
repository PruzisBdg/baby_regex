/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Compile search expression.
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

// Private to RegexLT_'.
#define dbgPrint           regexlt_dbgPrint

/* ---------------------------- rightOperator -----------------------------

   Given a regex string 'rgx', return the char of the controlling operator for rgx[0]
   - the 1st char.

   Returns:
      - if there is no controlling operator, 'E' (meaning End).
      - if no operators before end-of-string, '$'
      - otherwise, '+','*','?','|' or '{', meaning a char class.

   Examples:
      'a?bc'      -> '?'
      'abc'       -> '$'       (end of string)
      'abc(def)+' ->  E       'abc' has no rightward operator; it's not controlled by the '+'.
      'ab+c'      ->  E       '+' does not apply to 'a'
      '(ab)*c     -> '*'      '*' applies to the 1st subgroup.
      'dog|cat'   -> '|'      ('|' is right-associative, applies to all of 'dog'.
      'a(dog|cat) ->  E       '|' does no apply to 'a'.
*/

PRIVATE BOOL isPostOpCh(C8 ch)
   { return ch == '{' || ch == '}' || ch == '?' || ch == '*' || ch == '+'; }

//#define TRACE_RIGHT_OP

PRIVATE C8 const * toClosesRptA(C8 const *p)
   { for(; *p != '}' && *p != '\0'; p++) {} return p; }


PRIVATE C8 const * bumper(C8 const *p)
   { return *(p+1) == '{' ?  toClosesRptA(p+1) : p+1; }

PRIVATE C8 const * toClosesRpt(C8 const *p)
   { for(; *p != '}' && *p != '\0'; p++) {} return p-1; }

PUBLIC C8 rightOperator(C8 const *rgx)
{
   C8 const *rp = rgx;
   C8 ch;
   U8 grpCnt, inSub;                 // Counts chars, char-classes and groups for purposes of operator left-associativity.
   BOOL esc, inClass, inCh;

   for(grpCnt = 0, esc = FALSE, inClass = FALSE, inCh = FALSE, inSub = 0; *rp != '\0'; rp++ ) // Until the end of regex maybe.
   {
      ch = *rp;

      if(ch == '\\') {                                // Backslash?
         esc = !esc;                                  // If prev char was NOT backslash, next char will be escaped, and vice versa.
         if(esc) {                                    // Escaped now?
                  #ifdef TRACE_RIGHT_OP
               dbgPrint("ch %c inCh %d esc %d inClass %d grpCnt %d inSub %d\r\n",
                     ch, inCh, esc, inClass, grpCnt, inSub);
                  #endif
            continue;}}                               // then continue to the next (escaped) char

      if(esc){                                        // This char is escaped?
         esc = FALSE;                                 // then cancel escape; it applies just to this char, which we process now.
         if(inSub == 0) {                             // Not currently nested?
            if(!inCh) {                               // Starting char sequence?
               inCh = TRUE;                           // then mark that we are in a char sequence...
               grpCnt++;                              // ...and we have one more char-group
            }
            else {                                    // otherwise already in char-sequence.
               if( isPostOpCh(*(rp+1))) {             // Next char is a post-op, '+','*' etc?
                  grpCnt++;	                          // then post-operator binds this char, so previous chars make one more char-group.
                  if( *(rp+1) == '{') {               // That post-op is a repeat e.g '{3,4}'?
                     rp = toClosesRpt(rp+1); }}}      // then advance to the closing '}'.
            }
         }
      else {                                          // else this char is not escaped; must look for groups, classes etc.
         if(inClass) {                                // In a '[A-Z]'?
            if(ch == ']') {                           // Closing ']'?
               inClass = FALSE;                       // then we are out of the class-specifier.
               if( isPostOpCh(*(rp+1))) {             // Next char is a post-op, '+','*' etc?
                  grpCnt++;                           // then post-operator binds this char-class, so previous chars make one more char-group.
                  if( *(rp+1) == '{') {               // That post-op is a repeat e.g '{3,4}'?
                     rp = toClosesRpt(rp+1); }}}      // then advance to the closing '}'.
         }
         else {                                       // else not in class specifier
            if(ch == '[') {                           // Opening '['?
               inClass = TRUE;                        // then we are in class specifier now.
            }
            else {
               if(inSub > 0) {                        // else... Nested in subgroup(s)?
                  if(ch == ')'){                      // Closing ')'?
                     inSub--;                         // then up one
                     if(inSub == 0){                  // Now out of subgroup(s)?
                        grpCnt++;}                    // then exiting to top adds another char-group
                  }
                  else if(ch == '(') {                // Opening?
                     inSub++;                         // then we go deeper
                  }
               }
               else {                                 // else didn't enter subgroup(s)
                  if(ch == ')') {                     // But, got a ')'? so must have started inside one.
                     if(inSub > 0) {                  
                        inSub--;}                     // Hmmm... think this is redundant 'inSub' is already 0 (above)
                  }
                  else if(ch == '(') {                // Opening '('
                     inSub = 1;                       // we are instead down in a subgroup
                     inCh = FALSE;
                  }
                  else if(ch == '|') {                // Alternation, '|'?
                     return '|';                      // Is right associative, so binds to rgx[0], no matter what's in between
                  }
                  else if(isPostOpCh(ch)) {           // else '+','*' etc?
                     return grpCnt > 1                // then, if there's at least one group between rgx[0] and '+'?
                        ? 'E'                         // rgx[0] is 'free'; it's not bound to the post-operator
                        : ch;                         // else rgx[0] is bound to the post-operator, so return the char '+','*' etc.
                  }
                  else {                              // else none of the above.
                     if(!inCh) {                      // Starting char sequence?
                        inCh = TRUE;
                        grpCnt++;                     // then we have one more char group.
                     }
                     else if( isPostOpCh(*(rp+1)))    // else if already in char sequence AND next char is a post-op?
                        { grpCnt++; }                 // then post-operator binds this char, so previous chars make one more char-group.
                     } }}}}

                  #ifdef TRACE_RIGHT_OP
               dbgPrint("ch %c inCh %d esc %d inClass %d grpCnt %d inSub %d\r\n",
                     ch, inCh, esc, inClass, grpCnt, inSub);
                  #endif
   }
   return
      grpCnt > 1     // Passed more than one group on the way to '\0'?
         ? 'E'       // then 1st group is 'free' -> 'E'
         : '$';      // else 1st group is bound to end-of-string -> '$'.
}


// ---------------------------------------------- eof --------------------------------------------------
