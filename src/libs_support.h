#ifndef LIBS_SUPPORT_H_INCLUDED
#define LIBS_SUPPORT_H_INCLUDED

#include "GenericTypeDefs.h"
#define RAM_IS
#define BOOLEAN BOOL
#define bool BOOL
#define false FALSE
#define true TRUE

#define REGEXLT_PRINT_STDIO


#define _TARGET_LIB_ARM             1
#define _TARGET_X86_CONSOLE         2
#define _TARGET_X86_STATIC_LIB      3
#define _TARGET_UNITY_TDD           4
#define _TARGET_TDD_ARM             5

// Target dependencies.
#ifdef __TARGET_IS_CONSOLE
   // Some statics will be exported.
   #define _EXPORT_FOR_TEST PUBLIC
   // For testing some 'const' will not be.
   #define _TDD_UNCONST
   #define _TARGET_IS _TARGET_X86_CONSOLE
#else
   #ifdef __TARGET_IS_X86_STATIC_LIB
      #define _TARGET_IS _TARGET_X86_STATIC_LIB
      #define _TDD_UNCONST const
      #define _EXPORT_FOR_TEST PRIVATE
   #else
      // For those private function which get checked by (Unity TDD) test harness.
      #ifdef UNITY_TDD
         #define _EXPORT_FOR_TEST
         #define _TDD_UNCONST
         #define _TARGET_IS _TARGET_UNITY_TDD
      #else
         #ifdef _TARGET_IS_LIB_ARM
            #define _TARGET_IS _TARGET_LIB_ARM
            #define _EXPORT_FOR_TEST PRIVATE
            #define _TDD_UNCONST const
            #define _TOOL_ASF_ARM_GCC
         #else
            #ifdef _TARGET_IS_TDD_ARM
               #define _TARGET_IS _TARGET_TDD_ARM
               #define _EXPORT_FOR_TEST PUBLIC
               #define _TDD_UNCONST
               #define _TOOL_ASF_ARM_GCC
            #else
               #error "_TARGET_IS must be defined"
            #endif
         #endif
      #endif
   #endif
#endif



#endif // LIBS_SUPPORT_H_INCLUDED
