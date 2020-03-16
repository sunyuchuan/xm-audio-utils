#include "idecoder.h"

void IAudioDecoder_free(IAudioDecoder *decoder)
{
    if(!decoder)
        return;

    if(decoder->func_free)
        decoder->func_free(decoder->opaque);

    free(decoder->opaque);
}

void IAudioDecoder_freep(IAudioDecoder **decoder)
{
    if(!decoder || !*decoder)
        return;

    IAudioDecoder_free(*decoder);
    free(*decoder);
    *decoder = NULL;
}

int IAudioDecoder_get_pcm_frame(IAudioDecoder *decoder,
    short *buffer, int buffer_size_in_short, bool loop)
{
    if (!decoder || !buffer)
        return -1;

    if (decoder->func_get_pcm_frame)
        return decoder->func_get_pcm_frame(decoder->opaque,
            buffer, buffer_size_in_short, loop);

    return -1;
}

int IAudioDecoder_seekTo(IAudioDecoder *decoder, int seek_pos_ms)
{
    if (!decoder)
        return -1;

    if (decoder->func_seekTo)
        return decoder->func_seekTo(decoder->opaque, seek_pos_ms);

    return -1;
}

IAudioDecoder *IAudioDecoder_create(size_t opaque_size)
{
    IAudioDecoder *decoder = (IAudioDecoder *)calloc(1, sizeof(IAudioDecoder));
    if (!decoder)
        return NULL;

    decoder->opaque = calloc(1, opaque_size);
    if (!decoder->opaque)
        return NULL;

    return decoder;
}

