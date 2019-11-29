#include "xm_audio_utils.h"
#include <sys/time.h>
#include <stdlib.h>
#include "error_def.h"
#include "file_helper.h"
#include "log.h"

int main(int argc, char **argv) {
    AeSetLogLevel(LOG_LEVEL_TRACE);
    AeSetLogMode(LOG_MODE_SCREEN);

    for (int i = 0; i < argc; i++) {
        LogInfo("argv[%d] %s\n", i, argv[i]);
    }

    int ret = 0;
    int buffer_size_in_short = 1024;
    short *buffer = NULL;
    struct timeval start;
    struct timeval end;
    unsigned long timer;
    gettimeofday(&start, NULL);

    FILE *pcm_writer = NULL;
    OpenFile(&pcm_writer, argv[6], true);

    buffer = (short *)calloc(sizeof(short), buffer_size_in_short);
    if (!buffer) goto end;

    XmAudioUtils *utils = xm_audio_utils_create();
    if (utils == NULL) {
        LogError("xm_audio_utils_create failed\n");
        goto end;
    }

    xm_audio_utils_parser_init(utils, argv[1], atoi(argv[2]), atoi(argv[3]),
        atoi(argv[4]), atoi(argv[5]), BGM);
    xm_audio_utils_parser_seekTo(utils, 10000, BGM);
    int bgm_start_time_ms = 0;
    int bgm_end_time_ms = 60000;
    int volume = 80;
    int fade_in_time_ms = 3000;
    int fade_out_time_ms = 3000;
    xm_audio_utils_fade_init(utils, atoi(argv[4]), atoi(argv[5]), bgm_start_time_ms,
        bgm_end_time_ms, volume, fade_in_time_ms, fade_out_time_ms);

    int64_t cur_size = 0;
    while (1) {
        ret = xm_audio_utils_get_parser_frame(utils, buffer, buffer_size_in_short, true, BGM);
        if (ret <= 0) break;
        int buffer_start_time = (float)(1000 * cur_size) / atoi(argv[5]) / atoi(argv[4]);
        cur_size += ret;
        xm_audio_utils_fade(utils, buffer, ret, buffer_start_time);
        fwrite(buffer, sizeof(short), ret, pcm_writer);
        int duration = (float)(1000 * ret) / atoi(argv[5]) / atoi(argv[4]);
        if (buffer_start_time + duration > bgm_end_time_ms) break;
    }

end:
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    if (pcm_writer) {
        fclose(pcm_writer);
        pcm_writer = NULL;
    }
    xm_audio_utils_freep(&utils);

    gettimeofday(&end, NULL);
    timer = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    printf("time consuming %ld us\n", timer);
    return 0;
}

