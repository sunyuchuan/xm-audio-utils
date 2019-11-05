#include "noise_estimation.h"
#include <stdlib.h>
#include <string.h>
#include "defines.h"

NoiseEstimation *NoiseEstimationCreate() {
    int ret = 0;

    NoiseEstimation *self =
        (NoiseEstimation *)calloc(1, sizeof(NoiseEstimation));
    if (NULL == self) return NULL;

    self->pk = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->pk) {
        ret = -1;
        goto end;
    }

    self->delta = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->delta) {
        ret = -1;
        goto end;
    }

    self->noise_ps = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->noise_ps) {
        ret = -1;
        goto end;
    }

    self->pxk = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->pxk) {
        ret = -1;
        goto end;
    }

    self->pnk = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->pnk) {
        ret = -1;
        goto end;
    }

    self->pxk_old = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->pxk_old) {
        ret = -1;
        goto end;
    }

    self->pnk_old = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->pnk_old) {
        ret = -1;
        goto end;
    }

    self->sig_prev = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->sig_prev) {
        ret = -1;
        goto end;
    }

    self->gain = (float *)calloc(ACTUAL_LEN, sizeof(float));
    if (NULL == self->gain) {
        ret = -1;
        goto end;
    }

end:
    if (ret < 0) NoiseEstimationFree(&self);
    return self;
}

int NoiseEstimationInit(NoiseEstimation *inst) {
    if (NULL == inst) return -2;

    inst->ad = 0.95f;
    inst->ap = 0.2f;
    inst->alpha = 0.7f;
    inst->beta = 0.8f;
    inst->gamma = 0.998f;
    inst->log_eng = 0.0f;
    inst->vad_floor = 1.0f;
    inst->eta = 0.5f;
    inst->eng_judge = -2.0f;

    memset(inst->pk, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->delta, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->noise_ps, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->pxk, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->pnk, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->pxk_old, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->pnk_old, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->sig_prev, 0, ACTUAL_LEN * sizeof(float));
    memset(inst->gain, 0, ACTUAL_LEN * sizeof(float));

    for (int i = 0; i < 39; ++i) {
        inst->delta[i] = 1.0f;
    }
    for (int i = 39; i < ACTUAL_LEN; ++i) {
        inst->delta[i] = 1.5f;
    }

    return 0;
}

void NoiseEstimationProcess(NoiseEstimation *inst, float *ns_ps,
                            short *is_first_frame) {
    float *pxk, *pxk_old, *pnk, *pnk_old, *tPs, *delta, *pk;
    float tmp1, tmp2, tmp3, alpha, srk, adk, ad, ap, comp_ad, comp_ap;

    if (*is_first_frame) {
        *is_first_frame = 0;
        memcpy(inst->noise_ps, ns_ps, sizeof(float) * ACTUAL_LEN);
        memcpy(inst->pxk, ns_ps, sizeof(float) * ACTUAL_LEN);
        memcpy(inst->pnk, ns_ps, sizeof(float) * ACTUAL_LEN);
        memcpy(inst->pxk_old, ns_ps, sizeof(float) * ACTUAL_LEN);
        memcpy(inst->pnk_old, ns_ps, sizeof(float) * ACTUAL_LEN);
        return;
    }

    ap = inst->ap;
    comp_ap = 1.0f - ap;
    ad = inst->ad;
    comp_ad = 1.0f - ad;

    pxk = inst->pxk;
    pxk_old = inst->pxk_old;
    pnk = inst->pnk;
    pnk_old = inst->pnk_old;
    tPs = ns_ps;
    alpha = inst->alpha;

    for (int i = 0; i < ACTUAL_LEN; i++) {
        tmp1 = *pxk_old - *tPs;
        *pxk = tmp1 * alpha + *tPs++;
        if (*pnk_old < *pxk) {
            tmp2 = 0.97f * (*pnk_old);
            tmp3 = 0.02f * (*pxk) + tmp2;
            *pnk = (-0.008f) * (*pxk_old) + tmp3;
        } else {
            *pnk = *pxk;
        }
        *pxk_old++ = *pxk++;
        *pnk_old++ = *pnk++;
    }

    pxk = inst->pxk;
    pnk = inst->pnk;
    tPs = inst->noise_ps;
    delta = inst->delta;
    pk = inst->pk;
    for (int i = 0; i < ACTUAL_LEN; i++) {
        srk = *delta++ * *pnk++ - *pxk;
        tmp1 = ap * (*pk);
        *pk = (srk < 0.0f) ? (tmp1 + comp_ap) : (tmp1);
        adk = comp_ad * *pk++ + ad;
        tmp2 = *tPs - *pxk;
        *tPs++ = tmp2 * adk + *pxk++;
    }
}

void NoiseEstimationFree(NoiseEstimation **inst) {
    if (NULL == inst || NULL == *inst) return;

    NoiseEstimation *self = *inst;
    if (self->pk) {
        free(self->pk);
        self->pk = NULL;
    }
    if (self->delta) {
        free(self->delta);
        self->delta = NULL;
    }
    if (self->noise_ps) {
        free(self->noise_ps);
        self->noise_ps = NULL;
    }
    if (self->pxk) {
        free(self->pxk);
        self->pxk = NULL;
    }
    if (self->pnk) {
        free(self->pnk);
        self->pnk = NULL;
    }
    if (self->pxk_old) {
        free(self->pxk_old);
        self->pxk_old = NULL;
    }
    if (self->pnk_old) {
        free(self->pnk_old);
        self->pnk_old = NULL;
    }
    if (self->sig_prev) {
        free(self->sig_prev);
        self->sig_prev = NULL;
    }
    if (self->gain) {
        free(self->gain);
        self->gain = NULL;
    }
    free(*inst);
    *inst = NULL;
}
