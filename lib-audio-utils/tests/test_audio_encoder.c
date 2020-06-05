#include <sys/time.h>
#include <stdio.h>
#include "error_def.h"
#include "log.h"
#include "xm_audio_transcode.h"

int main(int argc, char **argv) {
    AeSetLogLevel(LOG_LEVEL_TRACE);
    AeSetLogMode(LOG_MODE_SCREEN);
    if (argc < 3) {
        LogError("Usage %s input_decode_file output_pcm_file\n", argv[0]);
        return 0;
    }

    struct timeval start;
    struct timeval end;
    unsigned long timer;
    gettimeofday(&start, NULL);

    XmAudioTranscoder *transcoder = xm_audio_transcoder_create();
    if (!transcoder) {
        LogError("xm_audio_transcoder_create failed, exit\n");
        goto end;
    }

    int ret = xm_audio_transcoder_start(transcoder, argv[1], argv[2]);
    if (ret < 0) {
        LogError("transcode failed, exit\n");
        goto end;
    }

end:
    xm_audio_transcoder_stop(transcoder);
    xm_audio_transcoder_freep(transcoder);
    gettimeofday(&end, NULL);
    timer = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    printf("time consuming %ld us\n", timer);
    return 0;
}
