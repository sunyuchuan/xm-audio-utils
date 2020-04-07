#include "json_parse.h"
#include "cJSON.h"
#include "effects/voice_effect.h"
#include "tools/util.h"
#include <string.h>
#include <stdlib.h>
#include "codec/audio_decoder_factory.h"
#include "codec/ffmpeg_utils.h"

#define DEFAULT_SAMPLE_RATE 44100
#define DEFAULT_CHANNEL_NUMBER 1

static int parse_audio_record_source(cJSON *root_json, AudioRecordSource *record) {
    int ret = -1;
    if (!root_json || !record) {
        return ret;
    }

    cJSON *record_json = NULL;
    cJSON *record_childs = NULL;
    record_json = cJSON_GetObjectItemCaseSensitive(root_json, "record");
    if (!record_json)
    {
        LogError("%s get record_json failed\n", __func__);
        ret = -1;
        goto fail;
    }
    if (NULL != record_json->valuestring) {
        LogInfo("%s record_json->valuestring %s\n", __func__, record_json->valuestring);
    }

    if (record_json->child == NULL && NULL != record_json->valuestring) {
        record_childs = cJSON_Parse(record_json->valuestring);
        if (record_childs == NULL) {
            LogError("%s cJSON_Parse record_json->valuestring failed\n", __func__);
            ret = -1;
            goto fail;
        }
        record_json= record_childs;
    }

    const cJSON *sub = NULL;
    cJSON_ArrayForEach(sub, record_json)
    {
        cJSON *file_path = cJSON_GetObjectItemCaseSensitive(sub, "file_path");
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(sub, "volume");
        cJSON *crop_start = cJSON_GetObjectItemCaseSensitive(sub, "cropStartTimeMs");
        cJSON *crop_end = cJSON_GetObjectItemCaseSensitive(sub, "cropEndTimeMs");
        cJSON *start = cJSON_GetObjectItemCaseSensitive(sub, "startTimeMs");
        cJSON *end = cJSON_GetObjectItemCaseSensitive(sub, "endTimeMs");
        cJSON *is_pcm = cJSON_GetObjectItemCaseSensitive(sub, "isPcm");
        cJSON *sample_rate = cJSON_GetObjectItemCaseSensitive(sub, "sampleRate");
        cJSON *nb_channel = cJSON_GetObjectItemCaseSensitive(sub, "nbChannels");
        if (!cJSON_IsString(file_path) || !cJSON_IsNumber(vol)
            || !cJSON_IsNumber(crop_start) || !cJSON_IsNumber(crop_end)
            || !cJSON_IsNumber(start) || !cJSON_IsNumber(end)
            || !file_path->valuestring || !cJSON_IsNumber(sample_rate)
            || !cJSON_IsNumber(nb_channel) || !cJSON_IsString(is_pcm))
        {
            LogError("%s failed\n", __func__);
            ret = -1;
            goto fail;
        }
        if (record->file_path) free(record->file_path);
        if (record->decoder) IAudioDecoder_freep(&record->decoder);
        record->file_path = av_strdup(file_path->valuestring);
        record->volume = vol->valuedouble / (float)100;
        record->crop_start_time_ms = crop_start->valuedouble;
        record->crop_end_time_ms = crop_end->valuedouble;
        record->start_time_ms = start->valuedouble;
        record->end_time_ms = end->valuedouble;
        record->sample_rate = sample_rate->valuedouble;
        record->nb_channels = nb_channel->valuedouble;
        if (0 == strcasecmp(is_pcm->valuestring, "True"))
            record->decoder_type = DECODER_PCM;
        else
            record->decoder_type = DECODER_FFMPEG;

        record->decoder = audio_decoder_create(record->file_path,
                record->sample_rate, record->nb_channels,
                DEFAULT_SAMPLE_RATE, DEFAULT_CHANNEL_NUMBER,
                record->volume, record->decoder_type);
        if (record->decoder == NULL) {
            LogError("%s record audio_decoder_create failed.\n", __func__);
            ret = -1;
            goto fail;
        }

        if ((ret = IAudioDecoder_set_crop_pos(record->decoder,
            record->crop_start_time_ms, record->crop_end_time_ms)) < 0) {
            LogError("%s IAudioDecoder_set_crop_pos failed, ret = %d.\n", __func__, ret);
            goto fail;
        }

        LogInfo("%s file_path %s\n", __func__, record->file_path);
        LogInfo("%s volume %f\n", __func__, record->volume);
        LogInfo("%s crop start time  %d\n", __func__, record->crop_start_time_ms);
        LogInfo("%s crop end time %d\n", __func__, record->crop_end_time_ms);
        LogInfo("%s start time  %d\n", __func__, record->start_time_ms);
        LogInfo("%s end time %d\n", __func__, record->end_time_ms);
        LogInfo("%s sample_rate %d\n", __func__, record->sample_rate);
        LogInfo("%s nb_channels %d\n", __func__, record->nb_channels);
        LogInfo("%s decoder_type %d\n", __func__, record->decoder_type);
        LogInfo("%s out sample_rate %d\n", __func__, record->decoder->out_sample_rate);
        LogInfo("%s out nb_channels %d\n", __func__, record->decoder->out_nb_channels);
    }

    if (record_childs != NULL) {
        cJSON_Delete(record_childs);
    }
    return 0;
fail:
    if (record_childs != NULL) {
        cJSON_Delete(record_childs);
    }
    audio_record_source_free(record);
    return ret;
}

static int parse_audio_source(cJSON *json, AudioSourceQueue *queue) {
    int ret = -1;
    if (!json || !queue) {
        return ret;
    }

    AudioSource source;
    const cJSON *sub = NULL;
    cJSON_ArrayForEach(sub, json)
    {
        cJSON *file_path = cJSON_GetObjectItemCaseSensitive(sub, "file_path");
        cJSON *crop_start = cJSON_GetObjectItemCaseSensitive(sub, "cropStartTimeMs");
        cJSON *crop_end = cJSON_GetObjectItemCaseSensitive(sub, "cropEndTimeMs");
        cJSON *start = cJSON_GetObjectItemCaseSensitive(sub, "startTimeMs");
        cJSON *end = cJSON_GetObjectItemCaseSensitive(sub, "endTimeMs");
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(sub, "volume");
        cJSON *fade_in_time = cJSON_GetObjectItemCaseSensitive(sub, "fadeInTimeMs");
        cJSON *fade_out_time = cJSON_GetObjectItemCaseSensitive(sub, "fadeOutTimeMs");
        cJSON *side_chain = cJSON_GetObjectItemCaseSensitive(sub, "sideChain");

        if (!cJSON_IsString(file_path) || !cJSON_IsNumber(start)
            || !cJSON_IsNumber(end) || !file_path->valuestring
            || !cJSON_IsNumber(crop_start) || !cJSON_IsNumber(crop_end)
            || !cJSON_IsNumber(fade_in_time) || !cJSON_IsNumber(fade_out_time)
            || !cJSON_IsNumber(vol) || !cJSON_IsString(side_chain))
        {
            LogError("%s failed\n", __func__);
            ret = -1;
            goto fail;
        }

        memset(&source, 0, sizeof(AudioSource));
        source.file_path = av_strdup(file_path->valuestring);
        source.crop_start_time_ms = crop_start->valuedouble;
        source.crop_end_time_ms = crop_end->valuedouble;
        source.start_time_ms = start->valuedouble;
        source.end_time_ms = end->valuedouble;
        source.volume = vol->valuedouble / (float)100;
        source.fade_io.fade_in_time_ms = fade_in_time->valuedouble;
        source.fade_io.fade_out_time_ms = fade_out_time->valuedouble;
        source.left_factor = 1.0f;
        source.right_factor = 1.0f;
        if (0 == strcasecmp(side_chain->valuestring, "On")) {
            source.side_chain_enable = true;
            cJSON *makeup_g = cJSON_GetObjectItemCaseSensitive(sub, "makeUpGain");
            if(!cJSON_IsNumber(makeup_g))
            {
                LogError("%s makeUpGain parse failed\n", __func__);
                ret = -1;
                goto fail;
            }
            source.makeup_gain = makeup_g->valuedouble / (float)100;
        } else {
            source.side_chain_enable = false;
            source.makeup_gain = 0.0f;
        }
        source_queue_put(queue, &source);

        LogInfo("%s file_path %s\n", __func__, source.file_path);
        LogInfo("%s crop start time  %d\n", __func__, source.crop_start_time_ms);
        LogInfo("%s crop end time %d\n", __func__, source.crop_end_time_ms);
        LogInfo("%s start time  %d\n", __func__, source.start_time_ms);
        LogInfo("%s end time %d\n", __func__, source.end_time_ms);
        LogInfo("%s volume %f\n", __func__, source.volume);
        LogInfo("%s fade_in_time_ms %d\n", __func__, source.fade_io.fade_in_time_ms);
        LogInfo("%s fade_out_time_ms %d\n", __func__, source.fade_io.fade_out_time_ms);
        LogInfo("%s side_chain_enable %d\n", __func__, source.side_chain_enable);
        LogInfo("%s makeup_gain %f\n", __func__, source.makeup_gain);
    }

    source_queue_bubble_sort(queue);
    return 0;
fail:
    source_queue_flush(queue);
    return ret;
}

int mixer_parse(MixerEffcets *mixer_effects, const char *json_file_addr) {
    int ret = -1;
    if (NULL == json_file_addr || NULL == mixer_effects) {
        return ret;
    }
    if (!mixer_effects->bgmQueue || !mixer_effects->musicQueue) {
        LogError("%s bgmQueue or musicQueue is NULL\n", __func__);
        return ret;
    }

    char *content = NULL;
    cJSON *root_json = NULL;
    cJSON *bgms = NULL;
    cJSON *bgms_childs = NULL;
    cJSON *musics = NULL;
    cJSON *musics_childs = NULL;
    content = ae_read_file_to_string(json_file_addr);
    if (NULL == content) {
        ret = -1;
        LogError("%s json_file_addr %s read file failed\n", __func__, json_file_addr);
        goto fail;
    }

    root_json = cJSON_Parse(content);
    if (root_json == NULL) {
        LogError("%s cJSON_Parse failed\n", __func__);
        ret = -1;
        goto fail;
    }

    /*if ((ret = parse_audio_record_source(root_json, mixer_effects->record)) < 0) {
        LogError("%s parse_audio_record_source failed\n", __func__);
        goto fail;
    }
    IAudioDecoder *record_decoder = mixer_effects->record->decoder;
    if (record_decoder->out_bits_per_sample <= 0) {
        record_decoder->out_bits_per_sample = BITS_PER_SAMPLE_16;
    }
    mixer_effects->mix_duration_ms = record_decoder->duration_ms;*/

    bgms = cJSON_GetObjectItemCaseSensitive(root_json, "bgm");
    if (!bgms)
    {
        LogError("%s get bgms failed\n", __func__);
        ret = -1;
        goto fail;
    }
    if (NULL != bgms->valuestring) {
        LogInfo("%s bgms->valuestring %s\n", __func__, bgms->valuestring);
    }

    if (bgms->child == NULL && NULL != bgms->valuestring) {
        bgms_childs = cJSON_Parse(bgms->valuestring);
        if (bgms_childs == NULL) {
            LogError("%s cJSON_Parse bgms_childs->valuestring failed\n", __func__);
            ret = -1;
            goto fail;
        }
        bgms= bgms_childs;
    }

    if ((ret = parse_audio_source(bgms, mixer_effects->bgmQueue)) < 0) {
        LogError("%s parse bgms source failed\n", __func__);
        goto fail;
    }
    int end_time_ms = source_queue_get_end_time_ms(mixer_effects->bgmQueue);
    if (mixer_effects->mix_duration_ms < end_time_ms) {
        mixer_effects->mix_duration_ms = end_time_ms;
    }

    musics = cJSON_GetObjectItemCaseSensitive(root_json, "music");
    if (musics == NULL) {
        LogError("%s get musics failed\n", __func__);
        ret = -1;
        goto fail;
    }
    if (NULL != musics->valuestring) {
        LogInfo("%s musics->valuestring %s\n", __func__, musics->valuestring);
    }

    if (musics->child == NULL && NULL != musics->valuestring) {
        musics_childs = cJSON_Parse(musics->valuestring);
        if (musics_childs == NULL) {
            LogError("%s cJSON_Parse musics->valuestring failed\n", __func__);
            ret = -1;
            goto fail;
        }
        musics = musics_childs;
    }

    if ((ret = parse_audio_source(musics, mixer_effects->musicQueue)) < 0) {
        LogError("%s parse musics source failed\n", __func__);
        goto fail;
    }
    end_time_ms = source_queue_get_end_time_ms(mixer_effects->musicQueue);
    if (mixer_effects->mix_duration_ms < end_time_ms) {
        mixer_effects->mix_duration_ms = end_time_ms;
    }

    ret = 0;
fail:
    if (content != NULL)
    {
        free(content);
    }
    if (root_json != NULL)
    {
        cJSON_Delete(root_json);
    }
    if (bgms_childs != NULL)
    {
        cJSON_Delete(bgms_childs);
    }
    if (musics_childs != NULL)
    {
        cJSON_Delete(musics_childs);
    }
    return ret;
}

int effects_parse(VoiceEffcets *voice_effects, const char *json_file_addr) {
    int ret = -1;
    if (NULL == json_file_addr || NULL == voice_effects) {
        return ret;
    }

    char *content = NULL;
    cJSON *root_json = NULL;
    cJSON *effects = NULL;
    cJSON *effects_childs = NULL;
    content = ae_read_file_to_string(json_file_addr);
    if (NULL == content) {
        ret = -1;
        LogError("%s json_file_addr %s read file failed\n", __func__, json_file_addr);
        goto fail;
    }

    root_json = cJSON_Parse(content);
    if (root_json == NULL) {
        LogError("%s cJSON_Parse failed\n", __func__);
        ret = -1;
        goto fail;
    }

    if ((ret = parse_audio_record_source(root_json, voice_effects->record)) < 0) {
        LogError("%s parse_audio_record_source failed\n", __func__);
        goto fail;
    }
    voice_effects->dst_sample_rate = voice_effects->record->decoder->out_sample_rate;
    voice_effects->dst_channels = voice_effects->record->decoder->out_nb_channels;

    effects = cJSON_GetObjectItemCaseSensitive(root_json, "effects");
    if (effects == NULL) {
        LogError("%s get effects failed\n", __func__);
        ret = -1;
        goto fail;
    }
    if (NULL != effects->valuestring) {
        LogInfo("%s effects->valuestring %s\n", __func__, effects->valuestring);
    }

    if (effects->child == NULL && NULL != effects->valuestring) {
        effects_childs = cJSON_Parse(effects->valuestring);
        if (effects_childs == NULL) {
            LogError("%s cJSON_Parse effects->valuestring failed\n", __func__);
            ret = -1;
            goto fail;
        }
        effects = effects_childs;
    }

    const cJSON *effect = NULL;
    cJSON_ArrayForEach(effect, effects)
    {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(effect, "name");
        cJSON *info = cJSON_GetObjectItemCaseSensitive(effect, "info");

        if (!cJSON_IsString(name) || !cJSON_IsString(info)
            || name->valuestring == NULL || info->valuestring == NULL)
        {
            LogError("%s get effect failed\n", __func__);
            ret = -1;
            goto fail;
        }
        LogInfo("%s name->valuestring %s\n", __func__, name->valuestring);
        LogInfo("%s info->valuestring %s\n", __func__, info->valuestring);

        if (0 == strcasecmp(name->valuestring, "NoiseSuppression")) {
             LogInfo("%s effect NoiseSuppression\n", __func__);
             voice_effects->effects[NoiseSuppression] = create_effect(find_effect("noise_suppression"),
                                  voice_effects->dst_sample_rate, voice_effects->dst_channels);
             init_effect(voice_effects->effects[NoiseSuppression], 0, NULL);
             set_effect(voice_effects->effects[NoiseSuppression], "Switch", info->valuestring, 0);
        } else if (0 == strcasecmp(name->valuestring, "Beautify")) {
             LogInfo("%s effect Beautify\n", __func__);
             voice_effects->effects[Beautify] = create_effect(find_effect("beautify"),
                                  voice_effects->dst_sample_rate, voice_effects->dst_channels);
             init_effect(voice_effects->effects[Beautify], 0, NULL);
             set_effect(voice_effects->effects[Beautify], "mode", info->valuestring, 0);
        } else if (0 == strcasecmp(name->valuestring, "Reverb")) {
             LogInfo("%s effect Reverb\n", __func__);
             voice_effects->effects[Reverb] = create_effect(find_effect("reverb"),
                                  voice_effects->dst_sample_rate, voice_effects->dst_channels);
             init_effect(voice_effects->effects[Reverb], 0, NULL);
             set_effect(voice_effects->effects[Reverb], "mode", info->valuestring, 0);
        } else if (0 == strcasecmp(name->valuestring, "VolumeLimiter")) {
             LogInfo("%s effect VolumeLimiter\n", __func__);
             voice_effects->effects[VolumeLimiter] = create_effect(find_effect("limiter"),
                                  voice_effects->dst_sample_rate, voice_effects->dst_channels);
             init_effect(voice_effects->effects[VolumeLimiter], 0, NULL);
             set_effect(voice_effects->effects[VolumeLimiter], "Switch", info->valuestring, 0);
        } else {
             LogWarning("%s unsupport effect %s\n", __func__, name->valuestring);
        }
    }

    ret = 0;
fail:
    if (content != NULL)
    {
        free(content);
    }
    if (root_json != NULL)
    {
        cJSON_Delete(root_json);
    }
    if (effects_childs != NULL)
    {
        cJSON_Delete(effects_childs);
    }
    return ret;
}

