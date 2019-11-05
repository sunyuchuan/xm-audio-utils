#ifndef NOISE_SUPPRESSION_NOISE_ESTIMATION_H_
#define NOISE_SUPPRESSION_NOISE_ESTIMATION_H_

typedef struct NoiseEstimationT {
    float ad;
    float ap;
    float alpha;
    float beta;
    float gamma;
    float log_eng;
    float vad_floor;
    float eta;
    float eng_judge;
    float *pk;
    float *delta;
    float *noise_ps;
    float *pxk;
    float *pnk;
    float *pxk_old;
    float *pnk_old;
    float *sig_prev;
    float *gain;
} NoiseEstimation;

NoiseEstimation *NoiseEstimationCreate();
int NoiseEstimationInit(NoiseEstimation *inst);
void NoiseEstimationProcess(NoiseEstimation *inst, float *ns_ps,
                            short *is_first_frame);
void NoiseEstimationFree(NoiseEstimation **inst);

#endif  // NOISE_SUPPRESSION_NOISE_ESTIMATION_H
