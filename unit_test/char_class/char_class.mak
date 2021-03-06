# ------------------------------------------------------------------
#
# TDD makefile bits lib
#
# ---------------------------------------------------------------------

# Code folder, test folder and test file all get same name.
TARGET_BASE = char_class
TARGET_BASE_DIR =

# Defs common to the utils.
include ../baby_regex_common_pre.mak

# The complete files list
SRC_FILES := $(SRC_FILES) $(UNITYDIR)unity.c \
								$(SRCDIR)regexlt_char_class.c \
								$(HARNESS_TESTS_SRC) $(HARNESS_MAIN_SRC) $(LIBS)

# Clean and build
include ../baby_regex_common_build.mak

# ------------------------------- eof ------------------------------------

