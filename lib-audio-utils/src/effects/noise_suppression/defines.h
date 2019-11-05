#ifndef NOISE_SUPPRESSION_DEFINES_H_
#define NOISE_SUPPRESSION_DEFINES_H_

#define SAMPLE_RATE_IN_HZ 44100
#define FFT_LEN 1024
#define HALF_FFT_LEN 512
#define ACTUAL_LEN 513      // ((FFT_LEN >> 1) + 1)
#define FRAME_LEN 512       // (FFT_LEN >> 1)
#define HALF_FRAME_LEN 256  // (FRAME_LEN >> 1)

#define THETA 3.162f
#define MIN_FLT 1.175494e-30F
#define MAX_FLT 3.402823466e+38F
#define MAX_15BIT 32767.0f

#define FLP_INV_FFT_LEN (2.0f / 1024.0f)

#define FFMAX(a, b) ((a) > (b) ? (a) : (b))
#define FFMIN(a, b) ((a) > (b) ? (b) : (a))

#endif  // NOISE_SUPPRESSION_DEFINES_H_
