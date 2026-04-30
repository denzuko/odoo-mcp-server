/*
 * impl.c — stb-style implementation owner
 *
 * Exactly one translation unit defines all stb-style implementation macros.
 * Every other .c file includes only the headers (declarations).
 *
 * Production:  compiled and linked into all targets
 * Tests:       same object linked into build/tests
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */

#define ARENA_IMPLEMENTATION
#include "arena.h"

#define RC_IMPLEMENTATION
#include "rc.h"

#define SJ_IMPL
#include "sj.h"
