//
// Created by sunyc on 19-10-10.
//
#include "xm_android_jni.h"
#include "log.h"
#include "xm_audio_utils.h"
#include "utils.h"
#include <assert.h>
#include <pthread.h>

#define JNI_CLASS_AUDIO_UTILS "com/xmly/audio/utils/XMAudioUtils"

extern bool J4A_ExceptionCheck__catchAll(JNIEnv *env);

typedef struct xm_audio_utils_fields_t {
    pthread_mutex_t mutex;
    jclass clazz;
    jfieldID field_mNativeXMAudioUtils;
} xm_audio_utils_fields_t;

static xm_audio_utils_fields_t g_clazz;
static JavaVM* g_jvm;

jlong jni_mNativeXMAudioUtils_get(JNIEnv *env, jobject thiz)
{
    return (*env)->GetLongField(env, thiz, g_clazz.field_mNativeXMAudioUtils);
}

static void jni_mNativeXMAudioUtils_set(JNIEnv *env, jobject thiz, jlong value)
{
    (*env)->SetLongField(env, thiz, g_clazz.field_mNativeXMAudioUtils, value);
}

static XmAudioUtils *jni_get_xm_audio_utils(JNIEnv *env, jobject thiz)
{
    pthread_mutex_lock(&g_clazz.mutex);

    XmAudioUtils *ctx = (XmAudioUtils *) (intptr_t) jni_mNativeXMAudioUtils_get(env, thiz);
    if (ctx) {
        xmau_inc_ref(ctx);
    }

    pthread_mutex_unlock(&g_clazz.mutex);
    return ctx;
}

static XmAudioUtils *jni_set_xm_audio_utils(JNIEnv *env, jobject thiz, XmAudioUtils *ctx)
{
    pthread_mutex_lock(&g_clazz.mutex);

    XmAudioUtils *oldctx = (XmAudioUtils *) (intptr_t) jni_mNativeXMAudioUtils_get(env, thiz);
    if (ctx) {
        xmau_inc_ref(ctx);
    }
    jni_mNativeXMAudioUtils_set(env, thiz, (intptr_t) ctx);

    pthread_mutex_unlock(&g_clazz.mutex);

    if (oldctx != NULL) {
        xmau_dec_ref_p(&oldctx);
    }

    return oldctx;
}

static void
XMAudioUtils_release(JNIEnv *env, jobject thiz)
{
    LOGI("%s\n", __func__);
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    if(ctx == NULL) {
        LOGW("XMAudioUtils_release ctx is NULL\n");
        goto LABEL_RETURN;
    }

    xm_audio_utils_freep(&ctx);
    jni_set_xm_audio_utils(env, thiz, NULL);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
}

static void
XMAudioUtils_stop_mix(JNIEnv *env, jobject thiz)
{
    LOGI("%s\n", __func__);
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: stop mix: null ctx", LABEL_RETURN);

    stop_mixer_mix(ctx);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
}

static int
XMAudioUtils_get_progress_mix(JNIEnv *env, jobject thiz)
{
    int progress = 0;
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: get_progress_mix: null ctx", LABEL_RETURN);

    progress = get_progress_mix(ctx);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
    return progress;
}

static int
XMAudioUtils_mixer_mix(JNIEnv *env, jobject thiz,
        jstring inPcmPath, jint sample_rate, jint channels,
        jstring inConfigFilePath, jstring outM4aPath, jint encode_type)
{
    LOGI("%s\n", __func__);
    int ret = 0;
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: mixer_mix: null ctx", LABEL_RETURN);

    const char *in_pcm_path = NULL;
    const char *in_config_path = NULL;
    const char *out_m4a_path = NULL;
    if (inPcmPath)
        in_pcm_path = (*env)->GetStringUTFChars(env, inPcmPath, 0);
    if (inConfigFilePath)
        in_config_path = (*env)->GetStringUTFChars(env, inConfigFilePath, 0);
    if (outM4aPath)
        out_m4a_path = (*env)->GetStringUTFChars(env, outM4aPath, 0);

    ret = mixer_mix(ctx, in_pcm_path, sample_rate, channels,
        in_config_path, out_m4a_path, encode_type);
    if (in_pcm_path)
        (*env)->ReleaseStringUTFChars(env, inPcmPath, in_pcm_path);
    if (in_config_path)
        (*env)->ReleaseStringUTFChars(env, inConfigFilePath, in_config_path);
    if (out_m4a_path)
        (*env)->ReleaseStringUTFChars(env, outM4aPath, out_m4a_path);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
    return ret;
}

static void
XMAudioUtils_stop_effects(JNIEnv *env, jobject thiz)
{
    LOGI("%s\n", __func__);
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: stop effects: null ctx", LABEL_RETURN);

    stop_add_effects(ctx);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
}

static int
XMAudioUtils_get_progress_effects(JNIEnv *env, jobject thiz)
{
    int progress = 0;
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: get_progress_effects: null ctx", LABEL_RETURN);

    progress = get_progress_effects(ctx);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
    return progress;
}

static int
XMAudioUtils_add_effects(JNIEnv *env, jobject thiz,
        jstring inPcmPath, jint sample_rate, jint channels,
        jstring inConfigFilePath, jstring outPcmPath)
{
    LOGI("%s\n", __func__);
    int ret = 0;
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: add_voice_effects: null ctx", LABEL_RETURN);

    const char *in_pcm_path = NULL;
    const char *in_config_path = NULL;
    const char *out_pcm_path = NULL;
    if (inPcmPath)
        in_pcm_path = (*env)->GetStringUTFChars(env, inPcmPath, 0);
    if (inConfigFilePath)
        in_config_path = (*env)->GetStringUTFChars(env, inConfigFilePath, 0);
    if (outPcmPath)
        out_pcm_path = (*env)->GetStringUTFChars(env, outPcmPath, 0);

    ret = add_voice_effects(ctx, in_pcm_path, sample_rate, channels,
        in_config_path, out_pcm_path);
    if (in_pcm_path)
        (*env)->ReleaseStringUTFChars(env, inPcmPath, in_pcm_path);
    if (in_config_path)
        (*env)->ReleaseStringUTFChars(env, inConfigFilePath, in_config_path);
    if (out_pcm_path)
        (*env)->ReleaseStringUTFChars(env, outPcmPath, out_pcm_path);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
    return ret;
}

static void
XMAudioUtils_set_log(JNIEnv *env, jobject thiz,
        jint logMode, jint logLevel,  jstring outLogPath)
{
    LOGI("%s\n", __func__);
    AeSetLogMode(logMode);
    AeSetLogLevel(logLevel);
    SetFFmpegLogLevel(logLevel);

    if(logMode == LOG_MODE_FILE) {
        if(outLogPath == NULL) {
            LOGE("logMode is LOG_MODE_FILE, and outLogPath is NULL, return\n");
            return;
        } else {
            const char *out_log_path_ = (*env)->GetStringUTFChars(env, outLogPath, 0);
            AeSetLogPath(out_log_path_);
            if (out_log_path_)
                (*env)->ReleaseStringUTFChars(env, outLogPath, out_log_path_);
        }
    }
}

static void
XMAudioUtils_setup(JNIEnv *env, jobject thiz)
{
    LOGI("%s\n", __func__);
    XmAudioUtils *ctx = xm_audio_utils_create();
    JNI_CHECK_GOTO(ctx, env, "java/lang/OutOfMemoryError", "AUjni: native_setup: create failed", LABEL_RETURN);

    jni_set_xm_audio_utils(env, thiz, ctx);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
}

static int
XMAudioUtils_decode(JNIEnv *env, jobject thiz, jstring inAudioPath,
    jstring outPcmPath, jint outSampleRate, jint outChannels)
{
    LOGI("%s\n", __func__);
    int ret = -1;
    XmAudioUtils *ctx = jni_get_xm_audio_utils(env, thiz);
    JNI_CHECK_GOTO(ctx, env, "java/lang/IllegalStateException", "AUjni: decode: null ctx", LABEL_RETURN);

    const char *in_audio_path = NULL;
    const char *out_pcm_path = NULL;
    if (inAudioPath)
        in_audio_path = (*env)->GetStringUTFChars(env, inAudioPath, 0);
    if (outPcmPath)
        out_pcm_path = (*env)->GetStringUTFChars(env, outPcmPath, 0);

    ret = xm_audio_utils_decode(ctx, in_audio_path, out_pcm_path,
        outSampleRate, outChannels);

    if (in_audio_path)
        (*env)->ReleaseStringUTFChars(env, inAudioPath, in_audio_path);
    if (out_pcm_path)
        (*env)->ReleaseStringUTFChars(env, outPcmPath, out_pcm_path);
LABEL_RETURN:
    xmau_dec_ref_p(&ctx);
    return ret;
}

static JNINativeMethod g_methods[] = {
    { "native_decode", "(Ljava/lang/String;Ljava/lang/String;II)I", (void *) XMAudioUtils_decode },
    { "native_setup", "()V", (void *) XMAudioUtils_setup },
    { "native_set_log", "(IILjava/lang/String;)V", (void *) XMAudioUtils_set_log },
    { "native_add_effects", "(Ljava/lang/String;IILjava/lang/String;Ljava/lang/String;)I", (void *) XMAudioUtils_add_effects },
    { "native_get_progress_effects", "()I", (void *) XMAudioUtils_get_progress_effects },
    { "native_stop_effects", "()V", (void *) XMAudioUtils_stop_effects },
    { "native_mixer_mix", "(Ljava/lang/String;IILjava/lang/String;Ljava/lang/String;I)I", (void *) XMAudioUtils_mixer_mix },
    { "native_get_progress_mix", "()I", (void *) XMAudioUtils_get_progress_mix },
    { "native_stop_mix", "()V", (void *) XMAudioUtils_stop_mix },
    { "native_release", "()V", (void *) XMAudioUtils_release },
};

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv* env = NULL;

    g_jvm = vm;
    if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        return -1;
    }
    assert(env != NULL);

    pthread_mutex_init(&g_clazz.mutex, NULL);

    IJK_FIND_JAVA_CLASS(env, g_clazz.clazz, JNI_CLASS_AUDIO_UTILS);
    (*env)->RegisterNatives(env, g_clazz.clazz, g_methods, NELEM(g_methods));

    g_clazz.field_mNativeXMAudioUtils = (*env)->GetFieldID(env, g_clazz.clazz, "mNativeXMAudioUtils", "J");

    RegisterFFmpeg();

    ijksdl_android_global_init(g_jvm, env);
    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *jvm, void *reserved)
{
    ijksdl_android_global_uninit();
    pthread_mutex_destroy(&g_clazz.mutex);
}
