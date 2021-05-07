# --------------------------------------------------------------------------
#
# ---------------------------------------------------------------------------

# Import TDD harness defs
include ../../../unity_tdd/tdd_common_pre_build.mak

# (Additional) compiler flags
CFLAGS := $(CFLAGS) -D__COMPILER_IS_GENERIC__ -D__SYSTEM_IS_ANY__ -DUNITY_TDD

# File of tests is here.
SRCDIR = ../../src/$(TARGET_BASE_DIR)/

# Most test harnesses need arith support.
INTEGER_BASIC_FULLPATH = $(SRCDIR)arith_integer_basic$(CEXT)

OUT_FILE = -o $(TARGET)
SYMBOLS=-DTEST

# All arith tests reference the same includes
INC_DIRS := $(INC_DIRS) -I. -I../../src -I$(SPJ_SWR_LOC)/util/public -I$(SPJ_SWR_LOC)/arith/public -I$(SPJ_SWR_LOC)/tiny2/GenericTypes

LIBS := -L$(SPJ_SWR_LOC)/baby_regex/codeblocks_gcc -lBabyRegex $(LIBS)

# --------------------------------- eof ------------------------------------


