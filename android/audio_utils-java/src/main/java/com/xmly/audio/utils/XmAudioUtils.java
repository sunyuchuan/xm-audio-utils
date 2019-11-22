package com.xmly.audio.utils;

import android.os.Build;
import android.util.Log;

/**
 * Created by sunyc on 19-10-10.
 */

public class XmAudioUtils {
    private static final String TAG = "XmAudioUtils";
    private static final int DefaultSampleRate = 44100;
    private static final int DefaultChannelNumber = 1;
    public static final int ENCODER_FFMPEG = 0;
    public static final int ENCODER_MEDIA_CODEC = 1;
    public static final int DECODER_BGM = 0;
    public static final int DECODER_MUSIC = 1;
    public static final int ADD_EFFECTS = 0;
    public static final int MIXER_MIX = 1;
    //输出日志模式
    public static final int LOG_MODE_NONE = 0;
    public static final int LOG_MODE_FILE = 1;
    public static final int LOG_MODE_ANDROID = 2;
    public static final int LOG_MODE_SCREEN = 3;
    //输出日志级别
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
        native_setup();
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

    public void setLogModeAndLevel(int logMode, int logLevel, String outLogPath) {
        if (logMode == LOG_MODE_FILE && outLogPath == null) {
            Log.e(TAG, "Input Params is inValid, exit");
            return;
        }

        native_set_log(logMode, logLevel, outLogPath);
    }

    public int add_effects_init(String inPcmPath, int pcmSampleRate, int pcmChannels, String inConfigFilePath) {
        if (inPcmPath == null || inConfigFilePath == null) {
            return -1;
        }

        return native_effects_init(inPcmPath, pcmSampleRate, pcmChannels, inConfigFilePath, ADD_EFFECTS);
    }

    public int add_effects_seekTo(int seekTimeMs) {
        return native_effects_seekTo(seekTimeMs, ADD_EFFECTS);
    }

    public int get_effects_frame(short[] buffer, int bufferSizeInShort) {
        if (buffer == null) {
            return -1;
        }

        return native_get_effects_frame(buffer, bufferSizeInShort, ADD_EFFECTS);
    }

    public int mixer_init(String inPcmPath, int pcmSampleRate, int pcmChannels, String inConfigFilePath) {
        if (inPcmPath == null || inConfigFilePath == null) {
            return -1;
        }

        return native_effects_init(inPcmPath, pcmSampleRate, pcmChannels, inConfigFilePath, MIXER_MIX);
    }

    public int mixer_seekTo(int seekTimeMs) {
        return native_effects_seekTo(seekTimeMs, MIXER_MIX);
    }

    public int get_mixed_frame(short[] buffer, int bufferSizeInShort) {
        if (buffer == null) {
            return -1;
        }

        return native_get_effects_frame(buffer, bufferSizeInShort, MIXER_MIX);
    }

    public int fadeInit(int pcmSampleRate, int pcmNbChannels, int audioStartTimeMs,
                    int audioEndTimeMs, int volume, int fadeInTimeMs, int fadeOutTimeMs) {
        return native_fade_init(pcmSampleRate, pcmNbChannels, audioStartTimeMs,
                audioEndTimeMs, volume,fadeInTimeMs, fadeOutTimeMs);
    }

    public int fade(short[] buffer, int bufferSize, int bufferStartTimeMs) {
        if (buffer == null) {
            return -1;
        }
        return native_fade(buffer, bufferSize, bufferStartTimeMs);
    }

    public int decoder_create(String inAudioPath, int outSampleRate, int outChannels, int decoderType) {
        if (inAudioPath == null) {
            return -1;
        }
        return native_decoder_create(inAudioPath, outSampleRate, outChannels, decoderType);
    }

    public void decoder_seekTo(int seekTimeMs, int decoderType) {
        native_decoder_seekTo(seekTimeMs, decoderType);
    }

    public int get_decoded_frame(short[] buffer, int bufferSize, boolean loop, int decoderType) {
        if (buffer == null) {
            return -1;
        }
        return native_get_decoded_frame(buffer, bufferSize, loop, decoderType);
    }

    public void release() {
        native_release();
    }

    private native void native_set_log(int logMode, int logLevel, String outLogPath);
    private native void native_setup();

    private native int native_decoder_create(String inAudioPath, int outSampleRate, int outChannels, int decoderType);
    private native void native_decoder_seekTo(int seekTimeMs, int decoderType);
    private native int native_get_decoded_frame(short[] buffer, int bufferSize, boolean loop, int decoderType);

    private native int native_fade_init(int pcmSampleRate, int pcmNbChannels, int audioStartTimeMs,
                           int audioEndTimeMs, int volume, int fadeInTimeMs, int fadeOutTimeMs);
    private native int native_fade(short[] buffer, int bufferSize, int bufferStartTimeMs);

    private native int native_effects_init(String inPcmPath, int pcmSampleRate, int pcmChannels,
                                           String inConfigFilePath, int actionType);
    private native int native_effects_seekTo(int seekTimeMs, int actionType);
    private native int native_get_effects_frame(short[] buffer, int bufferSizeInShort, int actionType);

    private native void native_release();
    @Override
    protected void finalize() throws Throwable {
        Log.i(TAG, "finalize");
        try {
            native_release();
        } finally {
            super.finalize();
        }
    }
}
