#include "xm_audio_mixer.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include "log.h"
#include <stdlib.h>

extern void RegisterFFmpeg();

#define ENCODER_FFMPEG 0
#define ENCODER_MEDIA_CODEC 1
static volatile bool abort_request = false;

void *get_progress(void *arg) {
    int progress = 0;
    XmMixerContext *ctx = arg;
    while (!abort_request) {
        usleep(100000);
        progress = xm_audio_mixer_get_progress(ctx);
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
    XmMixerContext *mixer = xm_audio_mixer_create();
    if (!mixer) {
        LogError("%s xm_audio_mixer_create failed\n", __func__);
        goto end;
    }

    pthread_t get_progress_tid = 0;
    if (pthread_create(&get_progress_tid, NULL, get_progress, mixer)) {
        LogError("Error:unable to create get_progress thread\n");
        goto end;
    }

    int ret = xm_audio_mixer_init(mixer, argv[1]);
    if (ret < 0) {
        LogError("%s xm_audio_mixer_init failed\n", __func__);
        goto end;
    }

    ret = xm_audio_mixer_mix(mixer, argv[2], ENCODER_FFMPEG);
    if (ret < 0) {
	LogError("%s xm_audio_mixer_mix failed\n", __func__);
	goto end;
    }

end:
    abort_request = true;
    pthread_join(get_progress_tid, NULL);
    xm_audio_mixer_stop(mixer);
    xm_audio_mixer_freep(&mixer);
    gettimeofday(&end, NULL);
    timer = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    LogInfo("time consuming %ld us\n", timer);

    return 0;
}

