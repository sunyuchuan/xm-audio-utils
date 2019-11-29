#include "util.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "tools/mem.h"

int ae_strcasecmp(const char* s1, const char* s2) {
#if defined(HAVE_STRCASECMP)
    return strcasecmp(s1, s2);
#elif defined(_MSC_VER)
    return _stricmp(s1, s2);
#else
    while (*s1 && (toupper(*s1) == toupper(*s2))) s1++, s2++;
    return toupper(*s1) - toupper(*s2);
#endif
}

int ae_strncasecmp(char const* s1, char const* s2, size_t n) {
#if defined(HAVE_STRCASECMP)
    return strncasecmp(s1, s2, n);
#elif defined(_MSC_VER)
    return _strnicmp(s1, s2, n);
#else
    while (--n && *s1 && (toupper(*s1) == toupper(*s2))) s1++, s2++;
    return toupper(*s1) - toupper(*s2);
#endif
}

char *ae_read_file_to_string(const char *filename) {
    FILE *file = NULL;
    long length = 0;
    char *content = NULL;
    size_t read_chars = 0;

    /* open in read binary mode */
    file = fopen(filename, "rb");
    if (file == NULL)
    {
        goto cleanup;
    }

    /* get the length */
    if (fseek(file, 0, SEEK_END) != 0)
    {
        goto cleanup;
    }
    length = ftell(file);
    if (length < 0)
    {
        goto cleanup;
    }
    if (fseek(file, 0, SEEK_SET) != 0)
    {
        goto cleanup;
    }

    /* allocate content buffer */
    content = (char*)malloc((size_t)length + sizeof(""));
    if (content == NULL)
    {
        goto cleanup;
    }

    /* read the file into memory */
    read_chars = fread(content, sizeof(char), (size_t)length, file);
    if ((long)read_chars != length)
    {
        free(content);
        content = NULL;
        goto cleanup;
    }
    content[read_chars] = '\0';

cleanup:
    if (file != NULL)
    {
        fclose(file);
    }

    return content;
}

int ae_open_file(FILE **fp, const char *file_name, const int is_write) {
    int ret = 0;
    if (*fp) {
        fclose(*fp);
        *fp = NULL;
    }

    if (is_write)
        *fp = fopen(file_name, "wb");
    else
        *fp = fopen(file_name, "rb");

    if (!*fp) {
        ret = -1;
    }

    return ret;
}

int CopyString(const char* src, char** dst) {
    if (NULL != *dst) {
        if (strcmp(src, *dst) == 0) return 0;
        av_freep(dst);
        *dst = NULL;
    }
    *dst = av_strdup(src);
    if (NULL == *dst) {
        return -1;
    }
    return 0;
}

static float UpdateFactorS16(const float factor, const int sum) {
    float result = factor;
    if (sum > 32767) {
        result = 32767.0f / (float)(sum);
    } else if (sum < -32768) {
        result = -32768.0f / (float)(sum);
    }
    if (factor < 1.0f) {
        result += (1.0f - factor) / 32.0f;
    }
    return result;
}

static short GetSumS16(const int sum) {
    return sum < 0 ? (-32768 < sum ? sum : -32768)
                   : (sum < 32767 ? sum : 32767);
}

void MixBufferS16(const short* src_buffer1, const short* src_buffer2,
                  const int nb_mix_samples, const int nb_channels,
                  short* dst_buffer, float* left_factor, float* right_factor) {
    int sum = 0;
    for (int i = 0; i < nb_mix_samples; ++i) {
        if (1 == nb_channels) {
            sum = (src_buffer1[i] + src_buffer2[i]) * (*left_factor);
            *left_factor = UpdateFactorS16(*left_factor, sum);
            dst_buffer[i] = GetSumS16(sum);
        } else if (2 == nb_channels) {
            sum = (src_buffer1[i << 1] + src_buffer2[i << 1]) * (*left_factor);
            *left_factor = UpdateFactorS16(*left_factor, sum);
            dst_buffer[i << 1] = GetSumS16(sum);
            sum = (src_buffer1[(i << 1) + 1] + src_buffer2[(i << 1) + 1]) *
                  (*right_factor);
            *right_factor = UpdateFactorS16(*right_factor, sum);
            dst_buffer[(i << 1) + 1] = GetSumS16(sum);
        }
    }
}

void StereoToMonoS16(short* dst, short* src, const int nb_samples) {
    short* p = src;
    short* q = dst;
    int n = nb_samples;

    while (n >= 4) {
        q[0] = (p[0] + p[1]) >> 1;
        q[1] = (p[2] + p[3]) >> 1;
        q[2] = (p[4] + p[5]) >> 1;
        q[3] = (p[6] + p[7]) >> 1;
        q += 4;
        p += 8;
        n -= 4;
    }
    while (n > 0) {
        q[0] = (p[0] + p[1]) >> 1;
        q++;
        p += 2;
        n--;
    }
}

void MonoToStereoS16(short* dst, short* src, const int nb_samples) {
    short* p = src;
    short* q = dst;
    short v = 0;
    int n = nb_samples;
    //double sqrt2_div2 = 0.70710678118;

    while (n >= 4) {
        v = p[0];
        q[0] = v;
        q[1] = v;
        v = p[1];
        q[2] = v;
        q[3] = v;
        v = p[2];
        q[4] = v;
        q[5] = v;
        v = p[3];
        q[6] = v;
        q[7] = v;
        q += 8;
        p += 4;
        n -= 4;
    }
    while (n > 0) {
        v = p[0];
        q[0] = v;
        q[1] = v;
        q += 2;
        p += 1;
        n--;
    }
}

