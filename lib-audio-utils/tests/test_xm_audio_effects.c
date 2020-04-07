#include "effects/xm_audio_effects.h"
#include "log.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

extern void RegisterFFmpeg();

static volatile bool abort_request = false;

void *get_progress(void *arg) {
    int progress = 0;
    XmEffectContext *ctx = arg;
    while (!abort_request) {
        usleep(100000);
        progress = xm_audio_effect_get_progress(ctx);
        LogInfo("%s get_progress : %d\n", __func__, progress);
    }
    return NULL;
}

int main(int argc, char **argv) {
    struct timeval start;
    struct timeval end;
    unsigned long timer;
    gettimeofday(&start, NULL);

    AeSetLogLevel(LOG_LEVEL_TRACE);
    AeSetLogMode(LOG_MODE_SCREEN);

    for (int i = 0; i < argc; i++) {
        LogInfo("argv[%d] %s\n", i, argv[i]);
    }

    if (argc < 2) {
        LogWarning("Usage %s param invalid\n", argv[0]);
        return 0;
    }

    RegisterFFmpeg();
    XmEffectContext *ctx = xm_audio_effect_create();
    if (!ctx) {
        LogError("%s xm_audio_effect_create failed\n", __func__);
        goto end;
    }

    pthread_t get_progress_tid = 0;
    if (pthread_create(&get_progress_tid, NULL, get_progress, ctx)) {
        LogError("Error:unable to create get_progress thread\n");
        goto end;
    }

    if (xm_audio_effect_init(ctx, argv[1]) < 0) {
        LogError("Error: xm_audio_effect_init failed\n");
        goto end;
    }

    if (xm_audio_effect_add_effects(ctx, argv[2]) < 0) {
        LogError("Error: xm_audio_effect_add_effects failed\n");
        goto end;
    }

end:
    abort_request = true;
    pthread_join(get_progress_tid, NULL);
    // free xmly effects
    xm_audio_effect_stop(ctx);
    xm_audio_effect_freep(&ctx);
    gettimeofday(&end, NULL);
    timer = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    LogInfo("time consuming %ld us\n", timer);

    return 0;
}
