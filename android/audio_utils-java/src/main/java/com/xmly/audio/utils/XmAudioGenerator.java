package com.xmly.audio.utils;

import android.os.Build;
import android.util.Log;

/**
 * Created by sunyc on 19-11-19.
 */

public class XmAudioGenerator {
    private static final String TAG = "XmAudioGenerator";
    private static final int DefaultSampleRate = 44100;
    private static final int DefaultChannelNumber = 1;
    public static final int ENCODER_FFMPEG = 0;
    public static final int ENCODER_MEDIA_CODEC = 1;
    //是否加载过so
    private static boolean mIsLibLoaded = false;
    //本地XmAudioGenerator对象
    private long mNativeXmAudioGenerator = 0;

    private static final LibLoader sLocalLibLoader = new LibLoader() {
        @Override
        public void loadLibrary(String libName) throws UnsatisfiedLinkError, SecurityException {
            String ABI = Build.CPU_ABI;
            Log.i(TAG, "ABI " + ABI + " libName " +libName);
            System.loadLibrary(libName + "-" + ABI);
        }
    };

    private static void loadLibrariesOnce(LibLoader libLoader) {
        synchronized (XmAudioGenerator.class) {
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
        setLogModeAndLevel(XmAudioUtils.LOG_MODE_ANDROID, XmAudioUtils.LOG_LEVEL_DEBUG, null);
        native_setup();
    }

    public XmAudioGenerator()
    {
        loadLibrariesOnce(sLocalLibLoader);
        init();
    }

    public XmAudioGenerator(LibLoader libLoader)
    {
        loadLibrariesOnce(libLoader);
        init();
    }

    public void setLogModeAndLevel(int logMode, int logLevel, String outLogPath) {
        if (logMode == XmAudioUtils.LOG_MODE_FILE && outLogPath == null) {
            Log.e(TAG, "Input Params is inValid, exit");
            return;
        }

        native_set_log(logMode, logLevel, outLogPath);
    }

    public int start(String inPcmPath, int pcmSampleRate, int pcmChannels,
                     String inConfigFilePath, String outM4aPath, int encoderType) {
        if (inPcmPath == null || inConfigFilePath == null || outM4aPath == null) {
            return -1;
        }

        return native_start(inPcmPath, pcmSampleRate, pcmChannels,
                inConfigFilePath, outM4aPath, encoderType);
    }

    public int getProgress() {
        return native_get_progress();
    }

    public void stop() {
        native_stop();
    }

    public void release() {
        native_release();
    }

    private native void native_release();
    private native void native_stop();
    private native int native_get_progress();
    private native int native_start(String inPcmPath, int sampleRate, int channels,
                                                  String inConfigFilePath, String outM4aPath, int encoderType);
    private native void native_set_log(int logMode, int logLevel, String outLogPath);
    private native void native_setup();

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