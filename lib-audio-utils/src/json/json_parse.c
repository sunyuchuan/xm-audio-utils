#include "json_parse.h"
#include "cJSON.h"
#include "effects/voice_effect.h"
#include "tools/util.h"
#include <string.h>
#include <stdlib.h>

static void bubble_sort(int nb, BgmMusic **data) {
    int i, j;
    BgmMusic *temp;
    for (i = nb - 1; i > 0; i--) {
        for (j = 0; j < i; j++) {
            if (data[j]->start_time_ms > data[j + 1]->start_time_ms) {
                temp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = temp;
            }
        }
    }
}

static int parse_bgm_music(cJSON *json, int nb, BgmMusic **data) {
    int ret = -1;
    if (json == NULL) {
        return ret;
    }

    const cJSON *sub = NULL;
    int count = 0;
    cJSON_ArrayForEach(sub, json)
    {
        cJSON *url = cJSON_GetObjectItemCaseSensitive(sub, "url");
        cJSON *start = cJSON_GetObjectItemCaseSensitive(sub, "startTimeMs");
        cJSON *end = cJSON_GetObjectItemCaseSensitive(sub, "endTimeMs");
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(sub, "volume");
        cJSON *fade_in_time = cJSON_GetObjectItemCaseSensitive(sub, "fadeInTimeMs");
        cJSON *fade_out_time = cJSON_GetObjectItemCaseSensitive(sub, "fadeOutTimeMs");
        cJSON *side_chain = cJSON_GetObjectItemCaseSensitive(sub, "sideChain");
        cJSON *makeup_g = cJSON_GetObjectItemCaseSensitive(sub, "makeUpGain");

        if (!cJSON_IsString(url) || !cJSON_IsNumber(start)
            || !cJSON_IsNumber(end) || url->valuestring == NULL
            || !cJSON_IsNumber(fade_in_time) || !cJSON_IsNumber(fade_out_time)
            || !cJSON_IsNumber(vol) || !cJSON_IsString(side_chain)
            || !cJSON_IsNumber(makeup_g))
        {
            LogError("%s failed\n", __func__);
            ret = -1;
            goto fail;
        }
        if (count >= nb) {
            LogError("%s bgm or music number is greater than the set nb.\n", __func__);
            ret = 0;
            goto fail;
        }
        data[count] = (BgmMusic *)calloc(1, sizeof(BgmMusic));
        if (!data[count]) {
            LogError("%s calloc BgmMusic failed.\n", __func__);
            ret = -1;
            goto fail;
        }

        data[count]->url = av_strdup(url->valuestring);
        data[count]->start_time_ms = start->valuedouble;
        data[count]->end_time_ms  = end->valuedouble;
        data[count]->volume = vol->valuedouble / (float)100;
        data[count]->fade_io.fade_in_time_ms = fade_in_time->valuedouble;
        data[count]->fade_io.fade_out_time_ms = fade_out_time->valuedouble;
        data[count]->left_factor = 1.0f;
        data[count]->right_factor = 1.0f;
        if (0 == strcasecmp(side_chain->valuestring, "On")) {
            data[count]->side_chain_enable = true;
        } else {
            data[count]->side_chain_enable = false;
        }
        data[count]->makeup_gain = makeup_g->valuedouble / (float)100;

        LogInfo("%s url %s\n", __func__, data[count]->url);
        LogInfo("%s start time  %d\n", __func__, data[count]->start_time_ms );
        LogInfo("%s end time %d\n", __func__, data[count]->end_time_ms );
        LogInfo("%s volume %f\n", __func__, data[count]->volume);
        LogInfo("%s fade_in_time_ms %d\n", __func__, data[count]->fade_io.fade_in_time_ms);
        LogInfo("%s fade_out_time_ms %d\n", __func__, data[count]->fade_io.fade_out_time_ms);
        LogInfo("%s side_chain_enable %d\n", __func__, data[count]->side_chain_enable);
        LogInfo("%s makeup_gain %f\n", __func__, data[count]->makeup_gain);
        count ++;
    }

    return 0;
fail:
    for (int i = 0; i < nb; i++)
    {
        if (data[i]) {
            if (data[i]->url) av_free(data[i]->url);
            free(data[i]);
            data[i] = NULL;
        }
    }
    return ret;
}

int mixer_parse(MixerEffcets *mixer_effects, const char *json_file_addr) {
    int ret = -1;
    if (NULL == json_file_addr || NULL == mixer_effects) {
        return ret;
    }

    char *content = NULL;
    cJSON *root_json = NULL;
    cJSON *nb_bgms = NULL;
    cJSON *bgms = NULL;
    cJSON *bgms_childs = NULL;
    cJSON *nb_musics = NULL;
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

    nb_bgms = cJSON_GetObjectItemCaseSensitive(root_json, "nb_bgm");
    if (nb_bgms && cJSON_IsNumber(nb_bgms))
    {
        mixer_effects->nb_bgms = nb_bgms->valuedouble;
    } else {
        LogError("%s get nb_bgm failed\n", __func__);
        ret = -1;
        goto fail;
    }
    LogInfo("%s nb_bgm %d\n", __func__, mixer_effects->nb_bgms);
    mixer_effects->bgms = (BgmMusic **)calloc(1, mixer_effects->nb_bgms * sizeof(BgmMusic*));
    if (!mixer_effects->bgms)
    {
        LogError("%s mixer_effects->bgms calloc failed\n", __func__);
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
    if ((ret = parse_bgm_music(bgms, mixer_effects->nb_bgms, mixer_effects->bgms)) < 0) {
        LogError("%s parse bgms failed\n", __func__);
        goto fail;
    }
    bubble_sort(mixer_effects->nb_bgms, mixer_effects->bgms);

    nb_musics = cJSON_GetObjectItemCaseSensitive(root_json, "nb_music");
    if (nb_musics && cJSON_IsNumber(nb_musics))
    {
        mixer_effects->nb_musics = nb_musics->valuedouble;
    } else {
        LogError("%s get nb_musics failed\n", __func__);
        ret = -1;
        goto fail;
    }
    LogInfo("%s nb_music %d\n", __func__, mixer_effects->nb_musics);
    mixer_effects->musics = (BgmMusic **)calloc(1, mixer_effects->nb_musics * sizeof(BgmMusic*));
    if (!mixer_effects->musics)
    {
        LogError("%s mixer_effects->musics calloc failed\n", __func__);
        ret = -1;
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
    if ((ret = parse_bgm_music(musics, mixer_effects->nb_musics, mixer_effects->musics)) < 0) {
        LogError("%s parse musics failed\n", __func__);
        goto fail;
    }
    bubble_sort(mixer_effects->nb_musics, mixer_effects->musics);

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
    if (ret < 0) {
        if (mixer_effects->bgms != NULL)
        {
            free(mixer_effects->bgms);
            mixer_effects->bgms = NULL;
        }
        if (mixer_effects->musics != NULL)
        {
            free(mixer_effects->musics);
            mixer_effects->musics = NULL;
        }
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
    cJSON *nb_effects = NULL;
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

    nb_effects = cJSON_GetObjectItemCaseSensitive(root_json, "nb_effects");
    if (nb_effects && cJSON_IsNumber(nb_effects))
    {
        voice_effects->nb_effects = nb_effects->valuedouble;
    } else {
        LogError("%s get nb_effects failed\n", __func__);
        ret = -1;
        goto fail;
    }
    LogInfo("%s voice_effects->nb_effects %d\n", __func__, voice_effects->nb_effects);

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

