#ifndef _SOX_H_
#define _SOX_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define SOX_SAMPLE_MAX (32767)
#define SOX_SAMPLE_MIN (-32768)
#define MAX_SAMPLE_SIZE 2048
typedef int16_t sample_type;

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef M_PI
#define M_PI    3.14159265358979323846
#endif
double lsx_parse_frequency_k(char const * text, char * * end_ptr, int key);
#define lsx_parse_frequency(a, b) lsx_parse_frequency_k(a, b, INT_MAX)

#define SOX_SAMPLE_CLIP(samp) \
  do { \
    if (samp > SOX_SAMPLE_MAX) \
      { samp = SOX_SAMPLE_MAX; } \
    else if (samp < SOX_SAMPLE_MIN) \
      { samp = SOX_SAMPLE_MIN; } \
  } while (0)

#define SOX_ROUND_CLIP(d) \
  ((d) < 0? (d) <= SOX_SAMPLE_MIN - 0.5? SOX_SAMPLE_MIN: (d) - 0.5 \
        : (d) >= SOX_SAMPLE_MAX + 0.5? SOX_SAMPLE_MAX: (d) + 0.5)

#if defined(__cplusplus)
}
#endif

#endif /* _SOX_H_ */
