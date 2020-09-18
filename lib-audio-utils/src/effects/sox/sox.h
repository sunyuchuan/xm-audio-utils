#ifndef _SOX_H_
#define _SOX_H_

#if defined(__cplusplus)
extern "C" {
#endif

#define SOX_SAMPLE_MAX (32767)
#define SOX_SAMPLE_MIN (-32768)
#define MAX_SAMPLE_SIZE 2048
typedef int16_t sample_type;

#define SOX_SAMPLE_CLIP(samp) \
  do { \
    if (samp > SOX_SAMPLE_MAX) \
      { samp = SOX_SAMPLE_MAX; } \
    else if (samp < SOX_SAMPLE_MIN) \
      { samp = SOX_SAMPLE_MIN; } \
  } while (0)

#if defined(__cplusplus)
}
#endif

#endif /* _SOX_H_ */
