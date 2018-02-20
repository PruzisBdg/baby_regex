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
   { return ch == '{' || ch == '?' || ch == '*' || ch == '+'; }

//#define TRACE_RIGHT_OP


PUBLIC C8 rightOperator(C8 const *rgx)
{
   C8 const *rp = rgx;
   C8 ch;
   U8 grpCnt, inSub;                 // Counts chars, char-classes and groups for purposes of operator left-associativity.
   BOOL esc, inClass, inCh;

   for(grpCnt = 0, esc = FALSE, inClass = FALSE, inCh = FALSE, inSub = 0; *rp != '\0'; rp++) // Until the end of regex maybe.
   {
      ch = *rp;

      if(ch == '\\') {                          // Backslash?
         esc = !esc;                            // If prev char was NOT backslash, next char will be escaped, and vice versa.
         if(esc) {                              // Escaped now?
                  #ifdef TRACE_RIGHT_OP
               dbgPrint("ch %c inCh %d esc %d inClass %d grpCnt %d inSub %d\r\n",
                     ch, inCh, esc, inClass, grpCnt, inSub);
                  #endif
            continue;}}                         // then continue to the next (escaped) char

      if(esc){                                  // This char is escaped?
         esc = FALSE;                           // then cancel escape; it applies just to this char, which we process now.
         if(inSub == 0) {                       // Not currently nested?
            if(!inCh) {                         // Starting char sequence?
               inCh = TRUE;
               grpCnt++;                        // then we have one more char-group
            }
            else {                              // otherwise already in char-sequence.
               if( isPostOpCh(*(rp+1))) {       // Next char is a post-op, '+','*' etc?
                  grpCnt++; }}                  // then post-operator binds this char, so previous chars make one more char-group.
            }
         }
      else {                                    // else this char is not escaped; must look for groups, classes etc.
         if(inClass) {                          // In a '[A-Z]'?
            if(ch == ']') {                     // Closing ']'?
               inClass = FALSE;                 // then we are out of the class-specifier.
               if( isPostOpCh(*(rp+1)))         // Next char is a post-op, '+','*' etc?
                  { grpCnt++; }}                // then post-operator binds this char-class, so previous chars make one more char-group.
         }
         else {                                 // else not in class specifier
            if(ch == '[') {                     // Opening '['?
               inClass = TRUE;                  // then we are in class specifier now.
            }
            else {
               if(inSub > 0) {                  // else... Nested in subgroup(s)?
                  if(ch == ')'){                // Closing ')'?
                     inSub--;                   // then up one
                     if(inSub == 0){            // Now out of subgroup(s)?
                        grpCnt++;}              // then exiting to top adds another char-group
                  }
                  else if(ch == '(') {          // Opening?
                     inSub++;                   // then we go deeper
                  }
               }
               else {                           // else didn't enter subgroup(s)
                  if(ch == ')') {               // But, got a ')'? so must have started inside one.
                     if(inSub > 0) {
                        inSub--; }
                  }
                  else if(ch == '(') {          // Opening '('
                     inCh = FALSE;              // then not counting chars any more
                     inSub = 1;                 // we are instead down in a subgroup
                  }
                  else if(ch == '|') {          // Alternation, '|'?
                     return '|';                // Is right associative, so binds to rgx[0], no matter what's in between
                  }
                  else if(isPostOpCh(ch)) {     // else '+','*' etc?
                     return grpCnt > 1          // then, if there's at least one group between rgx[0] and '+'?
                        ? 'E'                   // rgx[0] is 'free'; it's not bound to the post-operator
                        : ch;                   // else rgx[0] is boud to the post-operator, so return the char '+','*' etc.
                  }
                  else {                        // else none of the above.
                     if(!inCh) {                // Starting char sequence?
                        inCh = TRUE;
                        grpCnt++;               // then we have one more char group.
                     }
                     else if( isPostOpCh(*(rp+1))) // else if already in char sequence AND next char is a post-op?
                        { grpCnt++; }           // then post-operator binds this char, so previous chars make one more char-group.
                     } }}}}

                  #ifdef TRACE_RIGHT_OP
               dbgPrint("ch %c inCh %d esc %d inClass %d grpCnt %d inSub %d\r\n",
                     ch, inCh, esc, inClass, grpCnt, inSub);
                  #endif
   }
   return
      grpCnt > 1     // Passed one or more subgroups on the way to '\0'?
         ? 'E'       // then 'free' 0, 'E'
         : '$';      // else 'free' '$' <> EOS.
}


// ---------------------------------------------- eof --------------------------------------------------
