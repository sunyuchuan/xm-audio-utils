package com.xmly.audio.utils;

import android.os.Build;
import android.util.Log;

/**
 * 实时获取pcm数据:添加特效\解码音频\淡入淡出\混音
 *
 * Created by sunyc on 19-10-10.
 */

public class XmAudioUtils {
    private static final String TAG = "XmAudioUtils";
    private static final int DefaultSampleRate = 44100;
    private static final int DefaultChannelNumber = 1;
    // 编码器类型:软件编码
    public static final int ENCODER_FFMPEG = 0;
    // 编码器类型:硬件编码
    public static final int ENCODER_MEDIA_CODEC = 1;
    // 添加特效
    private static final int ADD_EFFECTS = 0;
    // 混音
    private static final int MIXER_MIX = 1;
    // 日志输出模式
    // 不输出
    public static final int LOG_MODE_NONE = 0;
    // 输出到指定文件
    public static final int LOG_MODE_FILE = 1;
    // 输出到android日志系统logcat
    public static final int LOG_MODE_ANDROID = 2;
    // 在电脑上调试测试代码时,直接打印log到终端屏幕
    public static final int LOG_MODE_SCREEN = 3;
    //日志级别
    public static final int LOG_LEVEL_TRACE = 0;
    public static final int LOG_LEVEL_DEBUG = 1;
    public static final int LOG_LEVEL_VERBOSE = 2;
    public static final int LOG_LEVEL_INFO = 3;
    public static final int LOG_LEVEL_WARNING = 4;
    public static final int LOG_LEVEL_ERROR = 5;
    public static final int LOG_LEVEL_FATAL = 6;
    public static final int LOG_LEVEL_PANIC = 7;
    public static final int LOG_LEVEL_QUIET = 8;
    //是否加载过so
    private static boolean mIsLibLoaded = false;
    //本地XmAudioUtils对象实例
    private long mNativeXmAudioUtils = 0;

    private static final LibLoader sLocalLibLoader = new LibLoader() {
        @Override
        public void loadLibrary(String libName) throws UnsatisfiedLinkError, SecurityException {
            String ABI = Build.CPU_ABI;
            Log.i(TAG, "ABI " + ABI + " libName " +libName);
            System.loadLibrary(libName + "-" + ABI);
        }
    };

    private static void loadLibrariesOnce(LibLoader libLoader) {
        synchronized (XmAudioUtils.class) {
            if (!mIsLibLoaded) {
                if (libLoader == null)
                    libLoader = sLocalLibLoader;

                libLoader.loadLibrary("ijkffmpeg");
                libLoader.loadLibrary("xmaudio_utils");
                mIsLibLoaded = true;
            }
        }
    }

    private void init() {
        setLogModeAndLevel(LOG_MODE_ANDROID, LOG_LEVEL_DEBUG, null);
        try {
            native_setup();
        } catch (OutOfMemoryError e) {
            e.printStackTrace();
        }
    }

    public XmAudioUtils()
    {
        loadLibrariesOnce(sLocalLibLoader);
        init();
    }

    public XmAudioUtils(LibLoader libLoader)
    {
        loadLibrariesOnce(libLoader);
        init();
    }

    /**
     * native层代码的日志输出控制
     * @param logMode 日志打印模式:
     *                LOG_MODE_FILE 输出日志到文件
     *                LOG_MODE_ANDROID 输出到android的logcat系统
     *                LOG_MODE_SCREEN ubuntu等电脑操作系统打印在终端窗口上
     * @param logLevel 日志级别 LOG_LEVEL_DEBUG等
     * @param outLogPath 如果输出到文件,需要设置日志文件路径
     */
    public void setLogModeAndLevel(int logMode, int logLevel, String outLogPath) {
        if (logMode == LOG_MODE_FILE && outLogPath == null) {
            Log.e(TAG, "Input Params is inValid, exit");
            return;
        }

        native_set_log(logMode, logLevel, outLogPath);
    }

    /**
     * 实时添加声音特效,初始化
     * @param inConfigFilePath 声音特效参数的配置文件,json格式
     * @return 小于0表示初始化失败
     */
    public int add_effects_init(String inConfigFilePath) {
        int ret = -1;
        if (inConfigFilePath == null) {
            return ret;
        }

        try {
            ret = native_effects_init(inConfigFilePath, ADD_EFFECTS);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 快进快退到pcm文件指定位置
     * @param seekTimeMs 快进快退的目标时间,单位是毫秒
     * @return 小于0表示快进快退设置失败
     */
    public int add_effects_seekTo(int seekTimeMs) {
        int ret = -1;

        try {
            ret = native_effects_seekTo(seekTimeMs, ADD_EFFECTS);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 获取添加特效之后的pcm数据
     * @param buffer 目标buffer
     * @param bufferSizeInShort 目标buffer大小
     * @return 有效pcm数据的长度
     */
    public int get_effects_frame(short[] buffer, int bufferSizeInShort) {
        int ret = -1;
        if (buffer == null) {
            return ret;
        }

        try {
            ret = native_get_effects_frame(buffer, bufferSizeInShort, ADD_EFFECTS);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 实时混音初始化
     * @param inConfigFilePath 混音的配置文件路径,json格式
     * @return 小于0表示初始化失败
     */
    public int mixer_init(String inConfigFilePath) {
        int ret = -1;
        if (inConfigFilePath == null) {
            return ret;
        }

        try {
            ret = native_effects_init(inConfigFilePath, MIXER_MIX);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 快进快退到pcm文件指定位置
     * @param seekTimeMs 目标时间,单位是毫秒
     * @return 小于0表示快进快退设置失败
     */
    public int mixer_seekTo(int seekTimeMs) {
        int ret = -1;

        try {
            ret = native_effects_seekTo(seekTimeMs, MIXER_MIX);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 获取混音后的pcm数据
     * @param buffer 目标buffer
     * @param bufferSizeInShort 目标buffer大小
     * @return 有效pcm数据的长度
     */
    public int get_mixed_frame(short[] buffer, int bufferSizeInShort) {
        int ret = -1;
        if (buffer == null) {
            return ret;
        }

        try {
            ret = native_get_effects_frame(buffer, bufferSizeInShort, MIXER_MIX);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 淡入淡出初始化
     * @param pcmSampleRate 背景音pcm数据的采样率
     * @param pcmNbChannels 背景音pcm数据的声道数
     * @param audioStartTimeMs 背景音在人声pcm文件中的起始时间,单位毫秒
     * @param audioEndTimeMs 背景音在人声pcm文件中的结束时间,单位毫秒
     * @param fadeInTimeMs 背景音淡入时间长度
     * @param fadeOutTimeMs 背景音淡出时间长度
     * @return 小于0表示初始化失败
     */
    public int fadeInit(int pcmSampleRate, int pcmNbChannels, int audioStartTimeMs,
                    int audioEndTimeMs, int fadeInTimeMs, int fadeOutTimeMs) {
        int ret = -1;

        try {
            ret = native_fade_init(pcmSampleRate, pcmNbChannels, audioStartTimeMs,
                    audioEndTimeMs, fadeInTimeMs, fadeOutTimeMs);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 对pcm数据淡入淡出处理
     * @param buffer 需要淡入淡出的pcm数据buffer
     * @param bufferSize buffer大小
     * @param bufferStartTimeMs buffer在人声pcm文件中的起始时间,单位毫秒
     * @return 小于0表示失败
     */
    public int fade(short[] buffer, int bufferSize, int bufferStartTimeMs) {
        int ret = -1;
        if (buffer == null) {
            return ret;
        }

        try {
            ret = native_fade(buffer, bufferSize, bufferStartTimeMs);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 解码背景音或音效,解码器创建
     * @param inAudioPath 解码文件路径
     * @param outSampleRate 解码器输出的pcm数据采样率,建议和人声pcm数据的采样率一致
     * @param outChannels 解码输出的pcm数据声道数,建议双声道
     * @param isPcm 是否是PCM文件
     * @param volume 背景音音量大小,范围0到100
     * @return 小于0表示失败
     */
    public int decoder_create(String inAudioPath, int outSampleRate, int outChannels,
                              boolean isPcm, int volume) {
        int ret = -1;
        if (inAudioPath == null) {
            return ret;
        }

        try {
            ret = native_decoder_create(inAudioPath, outSampleRate, outChannels, isPcm, volume);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 快进快退到解码文件的指定位置
     * @param seekTimeMs 目标时间,单位毫秒
     */
    public void decoder_seekTo(int seekTimeMs) {
        try {
            native_decoder_seekTo(seekTimeMs);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }
    }

    /**
     * 获取解码后的pcm数据
     * @param buffer 目标buffer
     * @param bufferSize 目标buffer大小
     * @param loop 当音频文件解码到文件末尾时,是否从文件头开始重新解码
     * @return 有效pcm数据的长度
     */
    public int get_decoded_frame(short[] buffer, int bufferSize, boolean loop) {
        int ret = -1;
        if (buffer == null) {
            return ret;
        }

        try {
            ret = native_get_decoded_frame(buffer, bufferSize, loop);
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }

        return ret;
    }

    /**
     * 释放native占用的内存
     */
    public void release() {
        try {
            native_release();
        } catch (IllegalStateException e) {
            e.printStackTrace();
        }
    }

    private native void native_set_log(int logMode, int logLevel, String outLogPath);
    private native void native_close_log_file();
    private native void native_setup();

    private native int native_decoder_create(String inAudioPath, int outSampleRate, int outChannels, boolean isPcm, int volume);
    private native void native_decoder_seekTo(int seekTimeMs);
    private native int native_get_decoded_frame(short[] buffer, int bufferSize, boolean loop);

    private native int native_fade_init(int pcmSampleRate, int pcmNbChannels, int audioStartTimeMs,
                           int audioEndTimeMs, int fadeInTimeMs, int fadeOutTimeMs);
    private native int native_fade(short[] buffer, int bufferSize, int bufferStartTimeMs);

    private native int native_effects_init(String inConfigFilePath, int actionType);
    private native int native_effects_seekTo(int seekTimeMs, int actionType);
    private native int native_get_effects_frame(short[] buffer, int bufferSizeInShort, int actionType);

    private native void native_release();
    @Override
    protected void finalize() throws Throwable {
        Log.i(TAG, "finalize");
        try {
            native_release();
        } catch (IllegalStateException e) {
            e.printStackTrace();
        } finally {
            super.finalize();
        }
    }
}
