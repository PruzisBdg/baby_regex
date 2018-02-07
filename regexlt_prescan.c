/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Search expression Pre-scan
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


/* ----------------------------------- Regex Prescan ---------------------------------------

   Before compiling a regular expression, RegexLT_Match() does basic legality checks and
   tallys the amount of memory it will need to malloc() to the expression.
*/

#define _MaxRegexLen 200

PRIVATE BOOL isAnOperator(C8 ch)
   { return ch == '|' || ch == '?' || ch == '*' || ch == '+'; }

PRIVATE BOOL isRangeContents(C8 ch)
   { return isdigit(ch) || ch == ',' || ch == ' '; }      // e.g {3,4}, {3, 12} etc

PRIVATE C8 const classSpecifiers[] = "dws";

PRIVATE BOOL isClassSpecifier(C8 ch)
   { return  strchr(classSpecifiers, ch) != NULL; }

/* ------------------------------------ countRegexParts ---------------------------------

   Counts (into 'ctx') the number of regex sub-parts which will have to be malloced()
   for when the regex is compiled. Also does basic legality checks.

   'ch' is the next regex char. Returns a 'T_RegexParts_Rtn' error if there's an obvious
   error in the regex, else E_Continue, meaning continue counting.
*/

typedef enum {E_ContinuePrescan = 0, E_PrescanOK, E_EndsOnOpen, E_NestedClass, E_MissingOpenClass } T_RegexParts_Rtn;

PRIVATE T_RegexParts_Rtn countRegexParts(S_CntRegexParts *ctx, C8 ch)
{
   if(ch == '\0')                               // End of regex?
   {
      return
         ctx->inClass || ctx->esc || ch == '|'  // if incomplete escape OR inside class OR ended on alternate i.e '|'?
            ? E_EndsOnOpen                      // then regex is bad
            : E_PrescanOK;                      // else OK.
   }
   else if(ctx->inClass)                        // Inside a class?
   {
      if(ch == '[' && !ctx->esc)                // Got a (non-escaped) open-class?
         return FALSE;                          // which is illegal; classes can't be nested, fail
      else if(ch == ']')                        // Close class?
      {
         ctx->inClass = FALSE;                  // then no in-class anymore.
         ctx->classCnt++;                       // and we have one more char class.
      }
   }
   else if(ctx->inRange)                        // Got an opening '{'?
   {
      if(ch == '}')                             // Now got a closing '}'
      {
         ctx->inRange = FALSE;                  // not on a '{n,n}' any more.
         ctx->operators++;                      // Count this range specifier as an operator, same as '*', '?' etc.
         ctx->uneatenSubGrp = FALSE;            // If there was a preceding subgroup(s) we have consumed (them) now.
      }
      else if( !isRangeContents(ch))            // Wasn't a number. space or ','?
      {
         return FALSE;                          // then not a (legal) range specifier.
      }
   }
   else                                         // else outside a class AND outside a range
   {
      if(ch == '[' && !ctx->esc)                // Open-class?  ('[' not preceded by '\')
      {
         ctx->inClass = TRUE;                   // we are now in-class.
         ctx->charSeg = FALSE;                  // and likely leaving a char-segment.
      }
      else if(ch == '\\' && !ctx->inClass)     // Escape AND outside a char-class?
      {
         if(ctx->esc)                           // Already got '\', so we are '\\'
         {
            ctx->esc = FALSE;                   // so escape is done
            ctx->escCnt++;                      // and got another escaped char
         }
         else                                   // else not a '\'
         {
            ctx->esc = TRUE;                    // so next char will be what we escape.
            ctx->charSeg = FALSE;               // If we were in a char segment, we have left it now.
         }
      }
      else                                      // This char is not open-class or escape?
      {
         if(ctx->esc)                           // Previous was '\'?
         {
            ctx->esc = FALSE;                   // so we complete escape

            if( isClassSpecifier(ch))           // Is a class specifier e.g '\d'?
               { ctx->classCnt++; }             // then count one more class
            else
               { ctx->escCnt++; }               // otherwise count one more escaped char.
         }
         else if(ch == ']')                     // end-class?
         {                                      // which wasn't preceded by '[' (ctx->inClass == FALSE, above)
            return FALSE;                       // Illegal - fail.
         }
         else if(ch == '(')                     // Opens a subgroup?
         {
            if(ctx->inGroup == TRUE)            // But we are already in a subgroup?
            {
               return FALSE;                    // ... which is illegal. Fail.
            }
            else
            {
               ctx->inGroup = TRUE;             // else mark

               if(ctx->charSeg)                 // Opened this group inside a char segment? ...we have left that segment now
               {
                  ctx->charSeg = FALSE;         // then we have left it now...
                  ctx->segCnt++;                // and we have one more chars segment.
                  ctx->leftCnt = 0;             // Closed out a segment, so reset the count of free-left chars
               }
            }
         }
         else if(ch == ')')                     // Closes a subgroup?
         {
            if(ctx->inGroup == FALSE)           // But we weren't in a subgroup.
            {
               return FALSE;                    // ... again illegal. Fail
            }
            else
            {
               ctx->inGroup = FALSE;            // else mark that we are out of the group.
               ctx->uneatenSubGrp = TRUE;       // and that there's to be consumed by an operator.
               ctx->subExprs++;                 // A completed subgroup is an additional sub expression.
            }
         }
         else                                   // else some other char.
         {
            if( isAnOperator(ch) )              // ?,*,| or + ?
            {
               ctx->charSeg = FALSE;            // then if we were in a char segment we are out of it now.

               if(ctx->leftCnt >= 2)            // 2 or more open chars?
               {
                  ctx->segCnt++;                // the operator takes the proximate, which needs it's own char-segment.
               }
               ctx->operators++;                // and count this operator.
               ctx->leftCnt = 0;                // Reset count of free-left chars.

               /* '|' is right-associative. If there's a subgroup ahead of '|' compiler reserves
                  an NOP ahead of the subgroup CharBox to place a 'Split' corresponding to the
                  '|'.
               */
               if(ch == '|' && ctx->uneatenSubGrp == TRUE)
                  { ctx->segCnt++; }            // and we have one more chars segment.

               ctx->uneatenSubGrp = FALSE;      // If there was a preceding subgroup(s) we have consumed (them) now.
            }
            else if(ch == '{')                  // Start of a range specifier i.e {3,9}?
            {
               ctx->inRange = TRUE;             // then parse through that, and...
               ctx->charSeg = FALSE;            // ...if we were in a char segment we are out of it now.
            }
            else                                // else a regular char
            {
               ctx->leftCnt++;                  // Count chars open since last operator

               if(ctx->charSeg == FALSE)        // Not marked as in a char segment.
               {
                  ctx->charSeg = TRUE;          // then we are now.
                  ctx->segCnt++;                // and bump the char segment count.
               }
            }
         }           // else some other char.
      }           // not open-class or escape?
   }           // else outside a class AND outside a range
   return E_ContinuePrescan;                    // Didn't fail above, this latest char is good.
}

/* ------------------------------------ prescanError -------------------------------- */

PRIVATE C8 const *prescanErrName(T_RegexParts_Rtn err)
{
   switch(err) {
      case E_EndsOnOpen:         return "Ends on open";
      case E_NestedClass:        return "Nested Class";
      case E_MissingOpenClass:   return "Missing Class open";
      default:                   return "";
   }
}

PRIVATE void prescanError(S_CntRegexParts const *ctx, T_RegexParts_Rtn err, U8 idx, C8 ch)
{
   C8 buf[10];
   if(isprint(ch)) { sprintf(buf, "\'%c\'", ch); } else { sprintf(buf, "\\x%x", ch); }
   dbgPrint("\r\n------ Prescan: error, %s at [%d %s]", prescanErrName(err), idx, buf);
}

/* ------------------------------------- regexlt_prescan ----------------------------------------

   Count the numbers of character segments, classes and operators in the 'regex'. From this
   estimate the number of compiled instructions and character elects which will need malloc()s.
*/
PUBLIC S_RegexStats regexlt_prescan(C8 const *regex)
{
   C8 const *p;
   U16 c;
   T_RegexParts_Rtn rtn;

   S_CntRegexParts ctx = {
      .inClass = FALSE, .inRange = FALSE, .charSeg = FALSE, .esc = FALSE,
      .classCnt = 0, .segCnt = 0, .leftCnt = 0, .escCnt = 0, .operators = 0,
      .subExprs = 1 };                                            // There's always at least one sub-expression, which is the whole regex.

   S_RegexStats s = {.len=0, .charboxes=0, .instructions=0, .classes = 0, .subExprs = 0, .legal=FALSE};

   for(c = 0, p = regex; c < regexlt_cfg->maxRegexLen; c++, p++)  // Until the end of the regex...
   {
      if( (rtn = countRegexParts(&ctx, *p)) == E_PrescanOK)       // Reached '\0' AND no errors?
      {
         s.legal = TRUE;                                          // then regex is (probably) legal

         // Each segment uses an instruction slot. Operators use either 1 or 2 (additional) slots.
         s.instructions = (2*ctx.operators) + ctx.segCnt + 1 + 5;
         s.classes = ctx.classCnt;
         // Each char-list, char-class and escaped char uses a seqment (i.e S_CharBox).
         s.charboxes = ctx.classCnt + ctx.escCnt + ctx.segCnt + (2 * ctx.operators) + 1;
//         s.charboxes = ctx.classCnt + ctx.escCnt + ctx.segCnt + 1;
         s.subExprs = ctx.subExprs;
         break;
      }
      else if( rtn != E_ContinuePrescan)                          // else there's some error
      {
         break;                                                   // so break with tagging regex as legal.
      }                    // else continue, examine next char(s)
   }

   s.len = c;              // Regex was this many chars (or it was too many)

   if(s.legal == FALSE)
      prescanError(&ctx, rtn, c, *p);
   else
      dbgPrint("\r\n------- Prescan:\r\n"
               "   len = %d   operators %d classes %d escapes %d charSegs %d subExprs %d\r\n"
               "      ->  %d chars slots & %d instruction slots & %d classes\r\n\r\n",
         s.len, ctx.operators, ctx.classCnt, ctx.escCnt, ctx.segCnt, ctx.subExprs,
            s.charboxes, s.instructions, s.classes);

   return s;
}

// ------------------------------------------- eof -------------------------------------------
