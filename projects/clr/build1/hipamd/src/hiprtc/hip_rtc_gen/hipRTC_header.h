#pragma push_macro("CHAR_BIT")
#pragma push_macro("INT_MAX")
#define CHAR_BIT __CHAR_BIT__
#define INT_MAX __INTMAX_MAX__
#include "hip/hip_runtime.h"
#include "hip/hip_bfloat16.h"
#pragma pop_macro("CHAR_BIT")
#pragma pop_macro("INT_MAX")