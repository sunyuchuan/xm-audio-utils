#include <sys/time.h>
#include "effects/voice_effect.h"
#include "file_helper.h"
#include "log.h"
#include "codec/audio_decoder_factory.h"
#include "error_def.h"

extern void RegisterFFmpeg();

int main(int argc, char **argv) {
    AeSetLogLevel(LOG_LEVEL_TRACE);
    AeSetLogMode(LOG_MODE_SCREEN);

    if (argc < 3) {
        LogWarning("Usage %s input_pcm_file output_pcm_file effect_name\n",
                   argv[0]);
        return 0;
    }

    int ret = 0;
    size_t buffer_size = 2048;
    short buffer[buffer_size];
    FILE *pcm_writer = NULL;
    EffectContext *ctx = NULL;
    IAudioDecoder *decoder = NULL;
    struct timeval start;
    struct timeval end;
    unsigned long timer;
    gettimeofday(&start, NULL);

    // open output file
    ret = OpenFile(&pcm_writer, argv[2], 1);
    if (ret < 0) goto end;

    RegisterFFmpeg();
    decoder = audio_decoder_create(argv[1], 0, 0,
        44100, 1, 1.0f, DECODER_FFMPEG);
    if (decoder == NULL) {
        LogError("audio_decoder_create failed\n");
        goto end;
    }

    // create effects
    ctx = create_effect(find_effect("mcompand"), 44100, 1);
    ret = init_effect(ctx, 0, NULL);
    if (ret < 0) goto end;
    ret = set_effect(ctx, "mcompand", MCOMPAND_PARAMS, 0);
    if (ret < 0) goto end;

    while (1) {
        ret = IAudioDecoder_get_pcm_frame(decoder, buffer, buffer_size, false);
        if (ret <= 0) break;
        // send data
        ret = send_samples(ctx, buffer, ret);
        if (ret < 0) break;
        // receive data
        ret = receive_samples(ctx, buffer, buffer_size);
        while (ret > 0) {
            fwrite(buffer, sizeof(short), ret, pcm_writer);
            ret = receive_samples(ctx, buffer, buffer_size);
        }
    }

    ret = flush_effect(ctx, buffer, buffer_size);
    while (ret > 0) {
        fwrite(buffer, sizeof(short), ret, pcm_writer);
        ret = flush_effect(ctx, buffer, buffer_size);
    }

end:
    if (pcm_writer) {
        fclose(pcm_writer);
        pcm_writer = NULL;
    }
    IAudioDecoder_freep(&decoder);
    if (ctx) {
        free_effect(ctx);
        ctx = NULL;
    }
    gettimeofday(&end, NULL);
    timer = 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec - start.tv_usec;
    LogInfo("time consuming %ld us\n", timer);
    return 0;
}
