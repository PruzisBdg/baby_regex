/* ------------------------------------------------------------------------------
|
| Non-backtracking Lite Regex - Memory Management
|
--------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "libs_support.h"
#include "regexlt_private.h"

// Private to RegexLT_'.
#define dbgPrint     regexlt_dbgPrint
#define br           regexlt_cfg


/* ----------------------------- regexlt_safeFree(List) -------------------------------------- */

PUBLIC void regexlt_safeFree(void *p)
   { if(br->free != NULL && p != NULL)
      { br->free(p); }}

#define safeFree           regexlt_safeFree

PUBLIC void regexlt_safeFreeList(void **lst, U8 listSize)
{
   U8 c;
   for(c = 0; c < listSize; c++) {
      if(lst[c] != NULL) {
         safeFree(lst[c]); }}
}

#define safeFreeList       regexlt_safeFreeList

/* --------------------------- regexlt_getMemMultiple ------------------------------- */


PUBLIC BOOL regexlt_getMemMultiple(S_TryMalloc *lst, U8 listSize)
{
   U8 c;
   for(c = 0; c < listSize; c++)          // For each item in the malloc() list
   {
      // Try each malloc(). If a malloc fails then walk back the list and undo all wot we did.
      void **tgt = lst[c].mem;

      if(tgt != NULL) {
         if(lst[c].numBytes == 0) {
            *tgt = NULL;
         }
         else {
            if( (*tgt = br->getMem(lst[c].numBytes)) == NULL) {
               do {                          // For this malloc and each previous one
                  safeFree(*tgt);            // free()
                  *tgt = NULL;               // and NULL the mem ptr.
                  c--;
               } while(c > 0);
               return FALSE; } }}             // Return failure.
   }
   return TRUE;                           // else all mallocs done. Success!
}


// --------------------------------------------- eof -----------------------------------------------
