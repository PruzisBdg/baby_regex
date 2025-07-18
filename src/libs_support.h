#ifndef LIBS_SUPPORT_H_INCLUDED
#define LIBS_SUPPORT_H_INCLUDED

#include "spj_stdint.h"
#define RAM_IS
#define BOOLEAN BOOL

#define REGEXLT_PRINT_STDIO

// ================================== Targets ======================================

// --- Targets
#define _TARGET_LIB_ARM_GCC         1
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
            #define _TARGET_IS _TARGET_LIB_ARM_GCC
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

// =========================== Tools / Compilers =======================================

// --- Tools; under the Targets
#define TOOL_GCC_X86    1
#define TOOL_CC430      2
#define TOOL_STM32_GCC  3

// Complain if _TOOL_IS not defined under __TARGET (above)?

#if _TARGET_IS == _TARGET_X86_CONSOLE || _TARGET_IS == _TARGET_X86_STATIC_LIB || _TARGET_IS == _TARGET_UNITY_TDD
   #define _TOOL_IS TOOL_GCC_X86
#elif _TARGET_IS == _TARGET_LIB_ARM_GCC
   #define _TOOL_IS TOOL_STM32_GCC
#else
   #error "_TOOL_IS must be defined"
#endif



#endif // LIBS_SUPPORT_H_INCLUDED
