#include "xm_wav_utils.h"
#include "log.h"
#include "tools/util.h"

#define NB_SAMPLES 1024

bool xm_wav_utils_crop(const char *in_wav_path,
    long crop_start_ms, long crop_end_ms, const char *out_wav_path)
{
    bool ret = false;
    if (!in_wav_path || !out_wav_path) return ret;
    if (crop_start_ms < 0 || crop_end_ms < 0) return ret;

    WavContext wav_ctx;
    if(wav_read_header(in_wav_path, &wav_ctx) < 0) {
        LogError("%s read wav header failed, in_wav_path %s.\n", __func__, in_wav_path);
        goto fail;
    }

    FILE *reader = NULL;
    if (ae_open_file(&reader, in_wav_path, false) < 0) {
	LogError("%s open in_wav_path %s failed\n", __func__, in_wav_path);
	goto fail;
    }

    uint32_t offset = (crop_start_ms /(float) 1000) *
        wav_ctx.header.sample_rate * wav_ctx.header.nb_channels *
        (wav_ctx.header.bits_per_sample / 8) + wav_ctx.pcm_data_offset;
    if (fseek(reader, offset, SEEK_SET) < 0) {
        LogError("%s seek to 0x%x failed\n", __func__, offset);
        goto fail;
    }

    FILE *writer = NULL;
    if (ae_open_file(&writer, out_wav_path, true) < 0) {
	LogError("%s open out_wav_path %s failed\n", __func__, out_wav_path);
	goto fail;
    }

    if (wav_write_header(writer, &wav_ctx) < 0) {
        LogError("%s 1 write wav header failed, out_wav_path %s\n", __func__, out_wav_path);
        goto fail;
    }

    short buffer[NB_SAMPLES];
    volatile uint32_t data_size = 0;
    uint32_t copy_size = ((crop_end_ms - crop_start_ms) /(float) 1000) *
        wav_ctx.header.sample_rate * wav_ctx.header.nb_channels *
        (wav_ctx.header.bits_per_sample / 8);
    while(data_size < copy_size) {
        if (feof(reader) || ferror(reader)) {
            goto end;
        }

        int read_len = fread(buffer, sizeof(*buffer), NB_SAMPLES, reader);
        if (read_len <= 0) {
            goto end;
        }

        data_size += (read_len * sizeof(*buffer));
        fwrite(buffer, sizeof(*buffer), read_len, writer);
    }

    wav_ctx.header.data_size = data_size;
    // total file size minus the size of riff_id(4 byte) and riff_size(4 byte) itself
    wav_ctx.header.riff_size = wav_ctx.header.data_size +
        sizeof(wav_ctx.header) - 8;
    if (wav_write_header(writer, &wav_ctx) < 0) {
        LogError("%s 2 write wav header failed, out_wav_path %s\n", __func__, out_wav_path);
        goto fail;
    }

end:
    ret = true;
fail:
    return ret;
}

long xm_wav_utils_get_duration(const char *in_wav_path)
{
    long ret = -1;
    if (!in_wav_path) return ret;

    WavContext wav_ctx;
    if((ret = wav_read_header(in_wav_path, &wav_ctx)) < 0) {
        LogError("%s read wav header failed, in_wav_path %s.\n", __func__, in_wav_path);
        goto end;
    }

    ret = (wav_ctx.header.data_size / (wav_ctx.header.bits_per_sample / 8) /
        wav_ctx.header.nb_channels / (float) wav_ctx.header.sample_rate) * 1000;

end:
    return ret;
}
