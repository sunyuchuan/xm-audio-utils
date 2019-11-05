package com.xmly.audio.utils;

import android.os.Build;
import android.util.Log;

/**
 * Created by sunyc on 19-10-10.
 */

public class XMAudioUtils {
    private static final String TAG = "XMAudioUtils";
    private static final int DefaultSampleRate = 44100;
    private static final int DefaultChannelNumber = 1;
    public static final int ENCODER_FFMPEG = 0;
    public static final int ENCODER_MEDIA_CODEC = 1;
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
    private long mNativeXMAudioUtils = 0;

    private static final LibLoader sLocalLibLoader = new LibLoader() {
        @Override
        public void loadLibrary(String libName) throws UnsatisfiedLinkError, SecurityException {
            String ABI = Build.CPU_ABI;
            Log.i(TAG, "ABI " + ABI + " libName " +libName);
            System.loadLibrary(libName + "-" + ABI);
        }
    };

    private static void loadLibrariesOnce(LibLoader libLoader) {
        synchronized (XMAudioUtils.class) {
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

    public XMAudioUtils()
    {
        loadLibrariesOnce(sLocalLibLoader);
        init();
    }

    public XMAudioUtils(LibLoader libLoader)
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

    public int addVoiceEffects(String inPcmPath, int pcmSampleRate, int pcmChannels,
                               String inConfigFilePath, String outPcmPath) {
        if (inPcmPath == null || inConfigFilePath == null || outPcmPath == null) {
            return -1;
        }

        return native_add_effects(inPcmPath, pcmSampleRate, pcmChannels,
                inConfigFilePath, outPcmPath);
    }

    public int decode(String inAudioPath, String outPcmPath, int outSampleRate, int outChannels) {
        if (inAudioPath == null || outPcmPath == null) {
            return -1;
        }
        return native_decode(inAudioPath, outPcmPath, outSampleRate, outChannels);
    }

    public int getProgressVoiceEffects() {
        return native_get_progress_effects();
    }

    public void stopVoiceEffects() {
        native_stop_effects();
    }

    public int mix(String inPcmPath, int pcmSampleRate, int pcmChannels,
                   String inConfigFilePath, String outM4aPath, int encoderType) {
        if (inPcmPath == null || inConfigFilePath == null || outM4aPath == null) {
            return -1;
        }

        return native_mixer_mix(inPcmPath, pcmSampleRate, pcmChannels,
                inConfigFilePath, outM4aPath, encoderType);
    }

    public int getProgressMix() {
        return native_get_progress_mix();
    }

    public void stopMix() {
        native_stop_mix();
    }

    public void release() {
        native_release();
    }

    private native int native_add_effects(String inPcmPath, int sampleRate, int channels,
                                                String inConfigFilePath, String outPcmPath);
    private native int native_get_progress_effects();
    private native void native_stop_effects();

    private native int native_mixer_mix(String inPcmPath, int sampleRate, int channels,
                                          String inConfigFilePath, String outM4aPath, int encoderType);
    private native int native_get_progress_mix();
    private native void native_stop_mix();

    private native void native_set_log(int logMode, int logLevel, String outLogPath);
    private native void native_setup();
    private native int native_decode(String inAudioPath, String outPcmPath, int outSampleRate, int outChannels);

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
