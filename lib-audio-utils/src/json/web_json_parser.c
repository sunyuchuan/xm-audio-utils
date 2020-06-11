#include "web_json_parser.h"
#include "cJSON.h"
#include "tools/util.h"
#include <string.h>
#include <stdlib.h>
#include "codec/ffmpeg_utils.h"

static const char *tracks_name[MAX_NB_TRACKS] = {
    "track0", "track1", "track2", "track3", "track4"
};

#define KEY_FILE_PATH "file_path"
#define KEY_START_TIME "startTimeMs"
#define KEY_END_TIME "endTimeMs"
#define KEY_VOLUME "volume"
#define KEY_CROP_START_TIME "cropStartTimeMs"
#define KEY_CROP_END_TIME "cropEndTimeMs"
#define KEY_FADE_IN_TIME "fadeInTimeMs"
#define KEY_FADE_OUT_TIME "fadeOutTimeMs"
#define KEY_SIDE_CHAIN "sideChain"
#define KEY_MAKE_UP_GAIN "makeUpGain"
#define KEY_EFFECTS "effects"
#define KEY_EFFECTS_NAME "name"
#define KEY_EFFECTS_INFO "info"

#define NOISE_SUPPRESSION "NoiseSuppression"
#define BEAUTIFY "Beautify"
#define REVERB "Reverb"
#define VOLUME_LIMITER "VolumeLimiter"

static int parse_voice_effects(cJSON *effects, AudioSource *source)
{
    if (!effects || !source) {
        return -1;
    }

    for (int i = 0; i < MAX_NB_EFFECTS; ++i) {
        if (source->effects_info[i]) {
            free(source->effects_info[i]);
            source->effects_info[i] = NULL;
        }
    }

    const cJSON *effect = NULL;
    cJSON_ArrayForEach(effect, effects)
    {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(
            effect, KEY_EFFECTS_NAME);
        cJSON *info = cJSON_GetObjectItemCaseSensitive(
            effect, KEY_EFFECTS_INFO);

        if (!cJSON_IsString(name) || !cJSON_IsString(info)
            || name->valuestring == NULL || info->valuestring == NULL)
        {
            LogError("%s get effect failed, continue.\n", __func__);
            continue;
        }
        LogInfo("%s name->valuestring %s\n", __func__, name->valuestring);
        LogInfo("%s info->valuestring %s\n", __func__, info->valuestring);

        if (0 == strcasecmp(name->valuestring, NOISE_SUPPRESSION)) {
            LogInfo("%s effect NoiseSuppression\n", __func__);
            source->effects_info[NoiseSuppression] =
                av_strdup(info->valuestring);
        } else if (0 == strcasecmp(name->valuestring, BEAUTIFY)) {
            LogInfo("%s effect Beautify\n", __func__);
            source->effects_info[Beautify] = av_strdup(info->valuestring);
        } else if (0 == strcasecmp(name->valuestring, REVERB)) {
            LogInfo("%s effect Reverb\n", __func__);
            source->effects_info[Reverb] = av_strdup(info->valuestring);
        } else if (0 == strcasecmp(name->valuestring, VOLUME_LIMITER)) {
            LogInfo("%s effect VolumeLimiter\n", __func__);
            source->effects_info[VolumeLimiter] = av_strdup(info->valuestring);
        } else {
            LogWarning("%s unsupported effect %s\n", __func__, name->valuestring);
        }
    }

    return 0;
}

static int parse_audio_source(cJSON *json, AudioSourceQueue *queue) {
    int ret = -1;
    if (!json || !queue) {
        return ret;
    }

    AudioSourceQueue_flush(queue);
    AudioSource source;
    const cJSON *sub = NULL;
    cJSON *effects = NULL;
    cJSON *effects_childs = NULL;
    cJSON_ArrayForEach(sub, json)
    {
        cJSON *file_path = cJSON_GetObjectItemCaseSensitive(sub, KEY_FILE_PATH);
        cJSON *crop_start =
            cJSON_GetObjectItemCaseSensitive(sub, KEY_CROP_START_TIME);
        cJSON *crop_end =
            cJSON_GetObjectItemCaseSensitive(sub, KEY_CROP_END_TIME);
        cJSON *start = cJSON_GetObjectItemCaseSensitive(sub, KEY_START_TIME);
        cJSON *end = cJSON_GetObjectItemCaseSensitive(sub, KEY_END_TIME);
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(sub, KEY_VOLUME);
        cJSON *fade_in_time =
            cJSON_GetObjectItemCaseSensitive(sub, KEY_FADE_IN_TIME);
        cJSON *fade_out_time =
            cJSON_GetObjectItemCaseSensitive(sub, KEY_FADE_OUT_TIME);
        cJSON *side_chain = cJSON_GetObjectItemCaseSensitive(
            sub, KEY_SIDE_CHAIN);

        if (!cJSON_IsString(file_path) || !cJSON_IsNumber(start)
            || !cJSON_IsNumber(end) || !file_path->valuestring)
        {
            LogError("%s failed, parse next source.\n", __func__);
            continue;
        }

        memset(&source, 0, sizeof(AudioSource));
        source.file_path = av_strdup(file_path->valuestring);
        source.start_time_ms = start->valuedouble;
        source.end_time_ms = end->valuedouble;
        source.crop_start_time_ms =
            cJSON_IsNumber(crop_start) ? crop_start->valuedouble : 0;
        source.crop_end_time_ms =
            cJSON_IsNumber(crop_end) ? crop_end->valuedouble : -1;
        source.volume =
            cJSON_IsNumber(vol) ? (vol->valuedouble / (float)100) : 1.0f;
        source.fade_io.fade_in_time_ms =
            cJSON_IsNumber(fade_in_time) ? fade_in_time->valuedouble : 0;
        source.fade_io.fade_out_time_ms =
            cJSON_IsNumber(fade_out_time) ? fade_out_time->valuedouble : 0;
        source.left_factor = 1.0f;
        source.right_factor = 1.0f;
        if (cJSON_IsString(side_chain) && side_chain->valuestring &&
                0 == strcasecmp(side_chain->valuestring, "On")) {
            source.side_chain_enable = true;
            cJSON *makeup_g =
                cJSON_GetObjectItemCaseSensitive(sub, KEY_MAKE_UP_GAIN);
            if(!cJSON_IsNumber(makeup_g))
                source.makeup_gain = 0.5f;
            else
                source.makeup_gain = makeup_g->valuedouble / (float)100;
        } else {
            source.side_chain_enable = false;
            source.makeup_gain = 0.0f;
        }

        effects = cJSON_GetObjectItemCaseSensitive(sub, KEY_EFFECTS);
        if (effects == NULL) {
            LogWarning("%s get effects failed.\n", __func__);
            goto end;
        }
        if (NULL != effects->valuestring) {
            LogInfo("%s effects->valuestring %s\n",
                __func__, effects->valuestring);
        }
        if (effects->child == NULL && NULL != effects->valuestring) {
            effects_childs = cJSON_Parse(effects->valuestring);
            if (effects_childs == NULL) {
                LogError("%s cJSON_Parse effects->valuestring failed\n",
                    __func__);
                goto end;
            }
            effects = effects_childs;
        }
        if ((ret = parse_voice_effects(effects, &source)) < 0) {
            LogError("%s parse_voice_effects failed\n", __func__);
            goto end;
        }

end:
        if (effects_childs != NULL) {
            cJSON_Delete(effects_childs);
        }
        effects_childs = NULL;

        for (int i = 0; i < MAX_NB_EFFECTS; i++) {
            if (source.effects_info[i] != NULL) {
                source.has_effects = true;
                break;
            }
        }
        AudioSourceQueue_put(queue, &source);

        LogInfo("%s file_path %s\n", __func__, source.file_path);
        LogInfo("%s start time  %d\n", __func__, source.start_time_ms);
        LogInfo("%s end time %d\n", __func__, source.end_time_ms);
        LogInfo("%s volume %f\n", __func__, source.volume);
        LogInfo("%s crop start time  %d\n",
            __func__, source.crop_start_time_ms);
        LogInfo("%s crop end time %d\n",
            __func__, source.crop_end_time_ms);
        LogInfo("%s fade_in_time_ms %d\n",
            __func__, source.fade_io.fade_in_time_ms);
        LogInfo("%s fade_out_time_ms %d\n",
            __func__, source.fade_io.fade_out_time_ms);
        LogInfo("%s side_chain_enable %d\n",
            __func__, source.side_chain_enable);
        LogInfo("%s makeup_gain %f\n",
            __func__, source.makeup_gain);
    }

    AudioSourceQueue_bubble_sort(queue);
    return 0;
}

int web_json_parse(MixerEffects *mixer_effects, const char *json_file_addr) {
    int ret = -1;
    if (!json_file_addr || !mixer_effects) {
        return ret;
    }

    char *content = NULL;
    cJSON *root_json = NULL;
    cJSON *tracks[MAX_NB_TRACKS];
    cJSON *tracks_childs[MAX_NB_TRACKS];
    memset(tracks, 0, sizeof(tracks));
    memset(tracks_childs, 0, sizeof(tracks_childs));

    content = ae_read_file_to_string(json_file_addr);
    if (NULL == content) {
        ret = -1;
        LogError("%s json_file_addr %s read file failed\n",
            __func__, json_file_addr);
        goto fail;
    }

    root_json = cJSON_Parse(content);
    if (root_json == NULL) {
        LogError("%s cJSON_Parse failed\n", __func__);
        ret = -1;
        goto fail;
    }

    for (int i = 0; i < MAX_NB_TRACKS; i++) {
        tracks[i] = cJSON_GetObjectItemCaseSensitive(root_json, tracks_name[i]);
        if (!tracks[i]) {
            LogWarning("%s get %s failed, continue.\n",
                __func__, tracks_name[i]);
            continue;
        }
        if (NULL != tracks[i]->valuestring) {
            LogInfo("%s %s valuestring %s\n", __func__,
                tracks_name[i], tracks[i]->valuestring);
        }

        if (tracks[i]->child == NULL && NULL != tracks[i]->valuestring) {
            tracks_childs[i] = cJSON_Parse(tracks[i]->valuestring);
            if (tracks_childs[i] == NULL) {
                LogError("%s cJSON_Parse %s valuestring failed, continue.\n",
                    __func__, tracks_name[i]);
                continue;
            }
            tracks[i] = tracks_childs[i];
        }

        if (parse_audio_source(
                tracks[i], mixer_effects->sourceQueue[i]) < 0) {
            LogError("%s parse %s source failed, continue.\n",
                __func__, tracks_name[i]);
            continue;
        }

        int end_time_ms = AudioSourceQueue_get_end_time_ms(
            mixer_effects->sourceQueue[i]);
        if (mixer_effects->duration_ms < end_time_ms) {
            mixer_effects->duration_ms = end_time_ms;
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
    for (int i = 0; i < MAX_NB_TRACKS; i++) {
        if (tracks_childs[i] != NULL) {
            cJSON_Delete(tracks_childs[i]);
        }
    }
    return ret;
}
