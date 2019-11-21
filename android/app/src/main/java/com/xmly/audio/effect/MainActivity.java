package com.xmly.audio.effect;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Message;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.RadioGroup;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Iterator;

import com.xmly.audio.effect.audio.AudioCapturer;
import com.xmly.audio.effect.audio.AudioPlayer;
import com.xmly.audio.utils.XmAudioGenerator;
import com.xmly.audio.utils.XmAudioUtils;

public class MainActivity extends AppCompatActivity implements AudioCapturer.OnAudioFrameCapturedListener, View.OnClickListener, RadioGroup.OnCheckedChangeListener {
    private final static String TAG = MainActivity.class.getName();
    private static final String speech = "/sdcard/audio_effect_test/speech.pcm";
    private static final String effect = "/sdcard/audio_effect_test/effect.pcm";
    private static final String rawPcm = "/sdcard/audio_effect_test/pcm_mono_44kHz_0035.pcm";
    private static final String finalAudio = "/sdcard/audio_effect_test/final.m4a";
    private static final String decodeRawAudio = "/sdcard/audio_effect_test/side_chain_music_test.wav";
    private static final String decodePcm = "/sdcard/audio_effect_test/decode.pcm";
    private static final String jsonPath = "/sdcard/audio_effect_test/json.txt";
    private static final String mixVoicePcm = "/sdcard/audio_effect_test/side_chain_test.pcm";
    private static final String mixedPcm = "/sdcard/audio_effect_test/mixed.pcm";
    private static final String mixedJson = "/sdcard/audio_effect_test/effect_config.txt";
    private static final int SAMPLE_RATE_44100 = 44100;
    private static final int MONO_CHANNELS = 1;
    private static final int STEREO_CHANNELS = 2;
    private OutputStream mOsSpeech;
    private AudioPlayer mPlayer;
    private AudioCapturer mCapturer;
    private Button mBtnRecord;
    private Button mBtnMix;
    private Button mBtnPlayMix;
    private Button mBtnPlayOrg;
    private Button mBtnNsSwitch;
    private Button mBtnLimitSwitch;
    private Thread mPlayOrgThread;
    private Thread mPlayEffectThread;
    private XmAudioUtils mAudioUtils;
    private XmAudioGenerator mAudioGenerator;
    private HashMap<String, String> mEffectsInfoMap = new HashMap<String, String>();
    private Handler mHandler = null;
    private static final int MSG_PROGRESS = 1;
    private static final int MSG_COMPLETED = 2;
    private volatile boolean abort = false;
    private volatile boolean abortMix = false;
    private volatile boolean abortProgress = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        getSystemPermission();
        mAudioUtils = new XmAudioUtils();
        mAudioGenerator = new XmAudioGenerator();

        mBtnNsSwitch = findViewById(R.id.btn_ns);
        mBtnNsSwitch.setOnClickListener(this);

        mBtnLimitSwitch = findViewById(R.id.btn_limiter);
        mBtnLimitSwitch.setOnClickListener(this);

        mBtnRecord = findViewById(R.id.btn_record);
        mBtnRecord.setOnClickListener(this);

        mBtnPlayOrg = findViewById(R.id.btn_play_org);
        mBtnPlayOrg.setOnClickListener(this);

        mBtnMix = findViewById(R.id.btn_mix);
        mBtnMix.setOnClickListener(this);

        mBtnPlayMix = findViewById(R.id.btn_play_mix);
        mBtnPlayMix.setOnClickListener(this);

        ((RadioGroup) findViewById(R.id.voice_group)).setOnCheckedChangeListener(this);
        ((RadioGroup) findViewById(R.id.eq_group)).setOnCheckedChangeListener(this);

        mPlayer = new AudioPlayer();
        mCapturer = new AudioCapturer();
        mCapturer.setOnAudioFrameCapturedListener(MainActivity.this);
    }

    @Override
    public void onResume() {
        super.onResume();
        if (mHandler == null) {
            mHandler = new Handler() {
                @Override
                public void handleMessage(Message msg) {
                    switch (msg.what) {
                        case MSG_PROGRESS:
                            break;
                        case MSG_COMPLETED:
                            mBtnMix.setText("给录音加特效");
                            abortProgress = true;
                            abortMix = true;
                            break;
                        default:
                            break;
                    }
                }
            };
        }
    }

    @Override
    protected void onStop() {
        mAudioGenerator.stop();
        abortProgress = true;
        abortMix = true;
        stopPlayOrg();
        stopPlayMix();
        stopRecord();
        stopMix();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        mAudioUtils.release();
        mAudioGenerator.release();
        super.onDestroy();
    }

    private void getReadExternalStoragePermission() {
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            if (ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.READ_EXTERNAL_STORAGE)) {
                // TODO: show explanation
            } else {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
            }
        }
    }

    private void getWriteExternalStoragePermission() {
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            if (ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
                // TODO: show explanation
            } else {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
            }
        }
    }

    private void getRecordAudioPermission() {
        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            if (ActivityCompat.shouldShowRequestPermissionRationale(this, Manifest.permission.RECORD_AUDIO)) {
                // TODO: show explanation
            } else {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.RECORD_AUDIO}, 1);
            }
        }
    }

    private void getSystemPermission() {
        getReadExternalStoragePermission();
        getWriteExternalStoragePermission();
        getRecordAudioPermission();
    }

    private void OpenPcmFiles() {
        // 打开录制文件
        File outSpeech = new File(speech);
        if (outSpeech.exists()) outSpeech.delete();
        try {
            mOsSpeech = new FileOutputStream(outSpeech);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
        }
    }

    private class PlayOrgRunnable implements Runnable {
        @Override
        public void run() {
            try {
                // 打开播放文件
                File orgFile = new File(rawPcm);
                InputStream isOrg = new FileInputStream(orgFile);

                // 启动播放
                if (!mPlayer.isPlayerStarted()) {
                    mPlayer.startPlayer();
                }

                // 循环读数据，播放音频
                // 创建字节数组
                int bufferSize = 4096;
                byte[] data = new byte[bufferSize];
                abort = false;
                while (!abort) {
                    // 读取内容，放到字节数组里面
                    int readsize = isOrg.read(data);
                    if (readsize <= 0) {
                        Log.w(TAG, "  end of file stop player");
                        mPlayer.stopPlayer();
                        break;
                    } else {
                        mPlayer.play(data, 0, readsize);
                    }
                }
                mPlayer.stopPlayer();
                isOrg.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    private class PlayEffectRunnable implements Runnable {
        @Override
        public void run() {
            try {
                // 打开播放文件
                File inEffect = new File(effect);
                InputStream isEffect = new FileInputStream(inEffect);

                // 启动播放
                if (!mPlayer.isPlayerStarted()) {
                    mPlayer.startPlayer();
                }

                // 循环读数据，播放音频
                // 创建字节数组
                int bufferSize = 4096;
                byte[] data = new byte[bufferSize];
                abort = false;
                while (!abort) {
                    // 读取内容，放到字节数组里面
                    int readsize = isEffect.read(data);
                    if (readsize <= 0) {
                        Log.w(TAG, "  end of file stop player");
                        mPlayer.stopPlayer();
                        break;
                    } else {
                        mPlayer.play(data, 0, readsize);
                    }
                }
                mPlayer.stopPlayer();
                isEffect.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    private void startRecord() {
        mBtnRecord.setText("停止");
        OpenPcmFiles();
        // 启动录音
        if (!mCapturer.isCaptureStarted()) {
            mCapturer.startCapture();
        }
    }

    private void stopRecord() {
        mBtnRecord.setText("录音");
        // 停止录音
        if (mCapturer.isCaptureStarted()) {
            mCapturer.stopCapture();
        }
        try {
            if (null != mOsSpeech) mOsSpeech.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void realtimeMix() {
        long startTime = System.currentTimeMillis();
        JsonUtils.createOutputFile(mixedPcm);
        int bufferSize = 1024;
        short[] buffer = new short[bufferSize];
        File outMixedPcm = new File(mixedPcm);
        if (outMixedPcm.exists()) outMixedPcm.delete();
        FileOutputStream osMixedPcm = null;
        try {
            osMixedPcm = new FileOutputStream(outMixedPcm);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
        }

        int ret = mAudioUtils.mixer_init(mixVoicePcm, 44100, 1, mixedJson);
        if (ret < 0) {
            Log.e(TAG, "mixer_init failed");
            return;
        }

        ret = mAudioUtils.mixer_seekTo(10000);
        if (ret < 0) {
            Log.e(TAG, "mixer_seekTo failed");
            return;
        }

        long cur_size = 0;
        while (true) {
            ret = mAudioUtils.get_mixed_frame(buffer, bufferSize);
            if (ret <= 0) break;
            try {
                byte[] data = Utils.getByteArrayInLittleOrder(buffer);
                osMixedPcm.write(data, 0, 2*ret);
                osMixedPcm.flush();
            } catch (IOException e) {
                e.printStackTrace();
            }
            cur_size += ret;
            if (1000 * (cur_size / (float)44100 / 2) > 33000) {
                break;
            }
        }

        ret = mAudioUtils.mixer_seekTo(127226);
        if (ret < 0) {
            Log.e(TAG, "mixer_seekTo failed");
            return;
        }

        while (true) {
            ret = mAudioUtils.get_mixed_frame(buffer, bufferSize);
            if (ret <= 0) break;
            try {
                byte[] data = Utils.getByteArrayInLittleOrder(buffer);
                osMixedPcm.write(data, 0, 2*ret);
                osMixedPcm.flush();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        long endTime = System.currentTimeMillis();
        Log.i(TAG, "real-time mix cost time "+(float)(endTime - startTime)/(float)1000);
    }

    private void decodeAndFade() {
        long startTime = System.currentTimeMillis();
        mAudioUtils.decoder_create(decodeRawAudio, SAMPLE_RATE_44100, STEREO_CHANNELS, XmAudioUtils.DECODER_BGM);
        mAudioUtils.decoder_seekTo(10000, XmAudioUtils.DECODER_BGM);
        JsonUtils.createOutputFile(decodePcm);
        int bufferSize = 1024;
        short[] buffer = new short[bufferSize];
        File outDecode = new File(decodePcm);
        if (outDecode.exists()) outDecode.delete();
        FileOutputStream osDecode = null;
        try {
            osDecode = new FileOutputStream(outDecode);
        } catch (FileNotFoundException e) {
            e.printStackTrace();
        }
        mAudioUtils.fadeInit(SAMPLE_RATE_44100, STEREO_CHANNELS, 0, 60000, 80, 5000, 5000);
        long curSize = 0;
        while (true) {
            int ret = mAudioUtils.get_decoded_frame(buffer, bufferSize, false, XmAudioUtils.DECODER_BGM);
            if (ret <= 0) break;

            int bufferStartTime = (int) ((double)(1000 * curSize) / STEREO_CHANNELS / SAMPLE_RATE_44100);
            curSize += ret;
            mAudioUtils.fade(buffer, ret, bufferStartTime);
            try {
                byte[] data = Utils.getByteArrayInLittleOrder(buffer);
                osDecode.write(data, 0, 2*ret);
                osDecode.flush();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        try {
            osDecode.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
        long endTime = System.currentTimeMillis();
        Log.i(TAG, "decode cost time "+(float)(endTime - startTime)/(float)1000);
    }

    private void addEffectsAndMix() {
        JsonUtils.createOutputFile(finalAudio);
        long startTime = System.currentTimeMillis();
        if (mAudioGenerator.start(rawPcm, SAMPLE_RATE_44100, MONO_CHANNELS,
                jsonPath, finalAudio, XmAudioUtils.ENCODER_MEDIA_CODEC) < 0) {
            Log.e(TAG, "addEffectsAndMix error");
        }
        long endTime = System.currentTimeMillis();
        Log.i(TAG, "addEffectsAndMix cost time "+(float)(endTime - startTime)/(float)1000);
    }

    private void startMix() {
        printEffectsMap();
        JsonUtils.generateJsonFile(jsonPath, mEffectsInfoMap);
        clearEffectsMap();
        JsonUtils.readJsonFile(jsonPath);
        mBtnMix.setText("停止");
        Runnable runnable = new Runnable() {
            @Override
            public void run() {
                // add voice effects and mix music
                addEffectsAndMix();
                // decode final.m4a to final.pcm
                decodeAndFade();
                // real-time mix voice/bgm/music
                realtimeMix();
                mHandler.sendMessage(mHandler.obtainMessage(MSG_COMPLETED));
            }
        };
        new Thread(runnable).start();
        abortProgress = false;
        Runnable progressRunnable = new Runnable() {
            @Override
            public void run() {
                int progress = 0;
                while (progress < 99 && !abortProgress) {
                    try {
                        Thread.sleep(100L);
                        progress = mAudioGenerator.getProgress();
                        Log.i(TAG, "addEffectsAndMix progress : " + progress);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
        };
        new Thread(progressRunnable).start();
    }

    private void stopMix() {
        mBtnMix.setText("给录音加特效");
        mAudioGenerator.stop();
        abortProgress = true;
        abortMix = true;
    }

    private void startPlayOrg() {
        mBtnPlayOrg.setText("停止");
        mPlayOrgThread = new Thread(new PlayOrgRunnable());
        mPlayOrgThread.setPriority(Thread.MAX_PRIORITY);
        mPlayOrgThread.start();
    }

    private void stopPlayOrg() {
        mBtnPlayOrg.setText("试听原声");
        abort = true;
        if (null != mPlayOrgThread && mPlayOrgThread.isAlive()) {
            mPlayOrgThread.interrupt();
        }
    }

    private void startPlayMix() {
        mBtnPlayMix.setText("停止");
        mPlayEffectThread = new Thread(new PlayEffectRunnable());
        mPlayEffectThread.setPriority(Thread.MAX_PRIORITY);
        mPlayEffectThread.start();
    }

    private void stopPlayMix() {
        mBtnPlayMix.setText("试听带特效的声音");
        abort = true;
        if (null != mPlayEffectThread && mPlayEffectThread.isAlive()) {
            mPlayEffectThread.interrupt();
        }
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();
        if (id == R.id.btn_record) {
            if (mBtnRecord.getText().toString().contentEquals("录音")) {
                stopPlayMix();
                stopMix();
                stopPlayOrg();
                startRecord();
            } else if (mBtnRecord.getText().toString().contentEquals("停止")) {
                stopRecord();
            }
        } else if (id == R.id.btn_mix) {
            if (mBtnMix.getText().toString().contentEquals("给录音加特效")) {
                stopRecord();
                stopPlayOrg();
                stopPlayMix();
                startMix();
            } else if (mBtnMix.getText().toString().contentEquals("停止")) {
                stopMix();
            }
        } else if (id == R.id.btn_play_org) {
            if (mBtnPlayOrg.getText().toString().contentEquals("试听原声")) {
                stopRecord();
                stopMix();
                stopPlayMix();
                startPlayOrg();
            } else if (mBtnPlayOrg.getText().toString().contentEquals("停止")) {
                stopPlayOrg();
            }
        } else if (id == R.id.btn_play_mix) {
            if (mBtnPlayMix.getText().toString().contentEquals("试听带特效的声音")) {
                stopRecord();
                stopMix();
                stopPlayOrg();
                startPlayMix();
            } else if (mBtnPlayMix.getText().toString().contentEquals("停止")) {
                stopPlayMix();
            }
        } else if (id == R.id.btn_ns) {
            if (mBtnNsSwitch.getText().toString().contentEquals("打开降噪")) {
                addEffectsToMap("NoiseSuppression", "On");
                mBtnNsSwitch.setText("关闭降噪");
            } else if (mBtnNsSwitch.getText().toString().contentEquals("关闭降噪")) {
                addEffectsToMap("NoiseSuppression", "Off");
                mBtnNsSwitch.setText("打开降噪");
            }
        } else if (id == R.id.btn_limiter) {
            if (mBtnLimitSwitch.getText().toString().contentEquals("打开限幅")) {
                addEffectsToMap("VolumeLimiter", "On");
                mBtnLimitSwitch.setText("关闭限幅");
            } else if (mBtnLimitSwitch.getText().toString().contentEquals("关闭限幅")) {
                addEffectsToMap("VolumeLimiter", "Off");
                mBtnLimitSwitch.setText("打开限幅");
            }
        }
    }

    @Override
    public void onCheckedChanged(RadioGroup group, int checkedId) {
        if (checkedId == R.id.rb_voice_original) {
            addEffectsToMap("Reverb", "Original");
        } else if (checkedId == R.id.rb_echo_church) {
            addEffectsToMap("Reverb", "Church");
        } else if (checkedId == R.id.rb_echo_live) {
            addEffectsToMap("Reverb", "Live");
        } else if (checkedId == R.id.rb_eq_none) {
            addEffectsToMap("Beautify", "None");
        } else if (checkedId == R.id.rb_eq_clean_voice) {
            addEffectsToMap("Beautify", "CleanVoice");
        } else if (checkedId == R.id.rb_eq_bass) {
            addEffectsToMap("Beautify", "Bass");
        } else if (checkedId == R.id.rb_eq_low_voice) {
            addEffectsToMap("Beautify", "LowVoice");
        } else if (checkedId == R.id.rb_eq_penetrating) {
            addEffectsToMap("Beautify", "Penetrating");
        } else if (checkedId == R.id.rb_eq_magnetic) {
            addEffectsToMap("Beautify", "Magnetic");
        } else if (checkedId == R.id.rb_eq_soft_pitch) {
            addEffectsToMap("Beautify", "SoftPitch");
        }
    }

    @Override
    public void onAudioFrameCaptured(byte[] audioData) {
        try {
            mOsSpeech.write(audioData, 0, audioData.length);
            mOsSpeech.flush();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    @Override
    public void onAudioFrameCaptured(short[] audioData) {
        try {
            byte[] data = Utils.getByteArrayInLittleOrder(audioData);
            mOsSpeech.write(data, 0, data.length);
            mOsSpeech.flush();

            /*mEffect.xmly_send_samples(audioData, audioData.length);
            int ret = mEffect.xmly_receive_samples(speechSamples, maxNbSamples);
            while (ret > 0) {
                byte[] effectData = Utils.getByteArrayInLittleOrder(speechSamples);
                mOsEffect.write(effectData, 0, ret << 1);
                mOsEffect.flush();
                ret = mEffect.xmly_receive_samples(speechSamples, maxNbSamples);
            }*/
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    private void addEffectsToMap(String name, String info) {
        if (name == null || info == null) {
            return;
        }

        if(mEffectsInfoMap != null) {
            mEffectsInfoMap.put(name, info);
        }
    }

    private void printEffectsMap() {
        if(mEffectsInfoMap != null) {
            Iterator<HashMap.Entry<String, String>> iterator = mEffectsInfoMap.entrySet().iterator();
            while (iterator.hasNext()) {
                HashMap.Entry<String, String> entry = iterator.next();
                Log.i(TAG, entry.getKey() + " : " + entry.getValue());
            }
        }
    }

    private void clearEffectsMap() {
        if(mEffectsInfoMap != null) {
            mEffectsInfoMap.clear();
        }
    }
}
