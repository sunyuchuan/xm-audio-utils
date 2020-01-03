#include "json_parse.h"
#include "cJSON.h"
#include "effects/voice_effect.h"
#include "tools/util.h"
#include <string.h>
#include <stdlib.h>

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
        cJSON *start = cJSON_GetObjectItemCaseSensitive(sub, "startTimeMs");
        cJSON *end = cJSON_GetObjectItemCaseSensitive(sub, "endTimeMs");
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(sub, "volume");
        cJSON *fade_in_time = cJSON_GetObjectItemCaseSensitive(sub, "fadeInTimeMs");
        cJSON *fade_out_time = cJSON_GetObjectItemCaseSensitive(sub, "fadeOutTimeMs");
        cJSON *side_chain = cJSON_GetObjectItemCaseSensitive(sub, "sideChain");

        if (!cJSON_IsString(file_path) || !cJSON_IsNumber(start)
            || !cJSON_IsNumber(end) || !file_path->valuestring
            || !cJSON_IsNumber(fade_in_time) || !cJSON_IsNumber(fade_out_time)
            || !cJSON_IsNumber(vol) || !cJSON_IsString(side_chain))
        {
            LogError("%s failed\n", __func__);
            ret = -1;
            goto fail;
        }

        memset(&source, 0, sizeof(AudioSource));
        source.file_path = av_strdup(file_path->valuestring);
        source.start_time_ms = start->valuedouble;
        source.end_time_ms  = end->valuedouble;
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
            source.makeup_gain = 0.0;
        }
        source_queue_put(queue, &source);

        LogInfo("%s file_path %s\n", __func__, source.file_path);
        LogInfo("%s start time  %d\n", __func__, source.start_time_ms );
        LogInfo("%s end time %d\n", __func__, source.end_time_ms );
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

int effects_parse(VoiceEffcets *voice_effects, const char *json_file_addr, int sample_rate, int channels) {
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
             voice_effects->effects[NoiseSuppression] = create_effect(find_effect("noise_suppression"), sample_rate, channels);
             init_effect(voice_effects->effects[NoiseSuppression], 0, NULL);
             set_effect(voice_effects->effects[NoiseSuppression], "Switch", info->valuestring, 0);
        } else if (0 == strcasecmp(name->valuestring, "Beautify")) {
             LogInfo("%s effect Beautify\n", __func__);
             voice_effects->effects[Beautify] = create_effect(find_effect("beautify"), sample_rate, channels);
             init_effect(voice_effects->effects[Beautify], 0, NULL);
             set_effect(voice_effects->effects[Beautify], "mode", info->valuestring, 0);
        } else if (0 == strcasecmp(name->valuestring, "Reverb")) {
             LogInfo("%s effect Reverb\n", __func__);
             voice_effects->effects[Reverb] = create_effect(find_effect("reverb"), sample_rate, channels);
             init_effect(voice_effects->effects[Reverb], 0, NULL);
             set_effect(voice_effects->effects[Reverb], "mode", info->valuestring, 0);
        } else if (0 == strcasecmp(name->valuestring, "VolumeLimiter")) {
             LogInfo("%s effect VolumeLimiter\n", __func__);
             voice_effects->effects[VolumeLimiter] = create_effect(find_effect("limiter"), sample_rate, channels);
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

