#include <sys/time.h>
#include "codec/audio_decoder.h"
#include "error_def.h"
#include "file_helper.h"
#include "log.h"

int main(int argc, char **argv) {
    AeSetLogLevel(LOG_LEVEL_TRACE);
    AeSetLogMode(LOG_MODE_SCREEN);
    if (argc < 3) {
        LogError("Usage %s input_decode_file output_pcm_file\n", argv[0]);
        return 0;
    }

    int ret = 0;
    int buffer_size_in_short = 1024;
    short *buffer = NULL;
    struct timeval start;
    struct timeval end;
    unsigned long timer;
    gettimeofday(&start, NULL);

    FILE *pcm_writer = NULL;
    OpenFile(&pcm_writer, argv[2], true);

    buffer = (short *)calloc(sizeof(short), buffer_size_in_short);
    if (!buffer) goto end;

    // Set Log
    RegisterFFmpeg();
    AudioDecoder *decoder = xm_audio_decoder_create(argv[1],
        atoi(argv[3]), atoi(argv[4]));
    if (decoder == NULL) {
        LogError("xm_audio_decoder_create failed\n");
        goto end;
    }

    while (1) {
        ret = xm_audio_decoder_get_decoded_frame(decoder, buffer, buffer_size_in_short, false);
        if (ret <= 0) break;
        fwrite(buffer, sizeof(short), ret, pcm_writer);
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
    xm_audio_decoder_freep(&decoder);

    gettimeofday(&end, NULL);
    timer = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    printf("time consuming %ld us\n", timer);
    return 0;
}
