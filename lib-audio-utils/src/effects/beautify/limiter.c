#include "limiter.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FFMIN(a, b) ((a) > (b) ? (b) : (a))
#define FFMIN3(a, b, c) FFMIN(FFMIN(a, b), c)
#define FFMAX(a, b) (((a) > (b)) ? (a) : (b))

struct LimiterT {
    float output_gain;
    float attack_time;
    float decay_time;
    float limiter_threshold;
    float xpeak;
    float left_xpeak;
    float right_xpeak;
    float* left_delay_buf;
    float* right_delay_buf;
    float gain;
    float delay_in_sec;  /* Delay to apply before companding */
    float* delay_buf;    /* Old samples, used for delay processing */
    int delay_buf_size;  /* Size of delay_buf in samples */
    int delay_buf_index; /* Index into delay_buf */
    int delay_buf_cnt;   /* No. of active entries in delay_buf */
    int sample_rate;
    short limiter_switch;
    int channels;
};

Limiter* LimiterCreate(int sample_rate, int channels) {
    Limiter* self = (Limiter*)calloc(1, sizeof(Limiter));
    if (NULL == self) return NULL;

    self->sample_rate = sample_rate;
    self->channels = channels;
    self->xpeak = 0.0f;
    self->gain = 1.0f;
    self->limiter_switch = 0;
    self->delay_in_sec = 0.0f;
    self->delay_buf_size = self->delay_in_sec * self->sample_rate;
    self->delay_buf = (float*)calloc(self->delay_buf_size, sizeof(float));
    if (NULL == self->delay_buf) LimiterFree(&self);

    return self;
}

void LimiterFree(Limiter** inst) {
    if (NULL == inst || NULL == *inst) return;
    Limiter* self = *inst;

    if (self->delay_buf) {
        free(self->delay_buf);
        self->delay_buf = NULL;
    }
    free(*inst);
    *inst = NULL;
}

void LimiterSetSwitch(Limiter* inst, const int limiter_switch) {
    inst->limiter_switch = limiter_switch;
}

void LimiterSet(Limiter* inst, const float limiter_threshold_in_dB,
                const float attack_time_in_ms, const float decay_time_in_ms,
                const float output_gain_in_dB) {
    inst->limiter_threshold = powf(10.0f, limiter_threshold_in_dB / 20);
    inst->output_gain = powf(10.0f, output_gain_in_dB / 20);
    inst->attack_time =
        1.0f - expf(-2.2f / inst->sample_rate * 1000 / attack_time_in_ms);
    inst->decay_time =
        1.0f - expf(-2.2f / inst->sample_rate * 1000 / decay_time_in_ms);
}

int LimiterProcess(Limiter* inst, float* buffer, const int buffer_size) {
    if (NULL == inst || 0 == inst->limiter_switch) return buffer_size;
    int nb_samples = 0;

    for (int i = 0; i < buffer_size; ++i) {
        if (inst->channels == 2) {
            float left_a = fabs(buffer[i*2]);
            float right_a = fabs(buffer[i*2+1]);
            float left_coeff = left_a > inst->left_xpeak ?
                inst->attack_time : inst->decay_time;
            float right_coeff = right_a > inst->right_xpeak ?
                inst->attack_time : inst->decay_time;
            inst->left_xpeak = (1.0f - left_coeff) *
                inst->left_xpeak + left_coeff * left_a;
            inst->right_xpeak = (1.0f - right_coeff) *
                inst->right_xpeak + right_coeff * right_a;
            float peak = FFMAX(inst->left_xpeak, inst->right_xpeak);
            float f = (peak == 0.0f) ? 1.0f :
                FFMIN(1.0f, inst->limiter_threshold / peak);
            float coeff = f < inst->gain ? inst->attack_time : inst->decay_time;
            inst->gain = (1 - coeff) * inst->gain + coeff * f;
            if (inst->delay_buf_size <= 0) {
                buffer[2*nb_samples] *= inst->gain * inst->output_gain;
                buffer[1+2*(nb_samples++)] *= inst->gain * inst->output_gain;
            } else {
                float left_tmp = buffer[i * 2];
                float right_tmp = buffer[i * 2+1];
                if (inst->delay_buf_cnt < inst->delay_buf_size) {
                    inst->delay_buf_cnt++;
                } else {
                    buffer[2*nb_samples] =
                        inst->left_delay_buf[inst->delay_buf_index] *
                        inst->gain * inst->output_gain;
                    buffer[1+2*(nb_samples++)] =
                        inst->right_delay_buf[inst->delay_buf_index] *
                        inst->gain * inst->output_gain;
                }
                inst->left_delay_buf[inst->delay_buf_index] = left_tmp;
                inst->right_delay_buf[inst->delay_buf_index++] = right_tmp;
                inst->delay_buf_index %= inst->delay_buf_size;
            }
        } else {
            float a = fabs(buffer[i]);
            float coeff = a > inst->xpeak ?
                inst->attack_time : inst->decay_time;
            inst->xpeak = (1.0f - coeff) * inst->xpeak + coeff * a;
            float f = FFMIN(1.0f, inst->limiter_threshold / inst->xpeak);
            coeff = f < inst->gain ? inst->attack_time : inst->decay_time;
            inst->gain = (1 - coeff) * inst->gain + coeff * f;
            if (inst->delay_buf_size <= 0) {
                buffer[nb_samples++] *= inst->gain * inst->output_gain;
            } else {
                float tmp = buffer[i];
                if (inst->delay_buf_cnt < inst->delay_buf_size) {
                    inst->delay_buf_cnt++;
                } else {
                    buffer[nb_samples++] =
                        inst->delay_buf[inst->delay_buf_index] *
                        inst->gain * inst->output_gain;
                }
                inst->delay_buf[inst->delay_buf_index++] = tmp;
                inst->delay_buf_index %= inst->delay_buf_size;
            }
        }
    }
    return nb_samples;
}