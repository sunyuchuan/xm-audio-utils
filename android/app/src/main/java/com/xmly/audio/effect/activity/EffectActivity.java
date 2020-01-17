package com.xmly.audio.effect.activity;

import android.content.Context;
import android.content.Intent;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Message;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.RadioGroup;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Iterator;

import com.xmly.audio.effect.JsonUtils;
import com.xmly.audio.effect.R;
import com.xmly.audio.effect.Utils;
import com.xmly.audio.effect.audio.AudioPlayer;
import com.xmly.audio.utils.XmAudioUtils;

public class EffectActivity extends AppCompatActivity implements View.OnClickListener, RadioGroup.OnCheckedChangeListener {
    private final static String TAG = EffectActivity.class.getName();

    private static final String config = "/sdcard/audio_effect_test/config.txt";
    private static final String effect = "/sdcard/audio_effect_test/effect.pcm";

    private Button mBtnNsSwitch;
    private Button mBtnLimitSwitch;

    private Button mBtnAudition;
    private XmAudioUtils mAudioUtils;
    private AudioPlayer mPlayer;

    private HashMap<String, String> mEffectsInfoMap = new HashMap<String, String>();
    private Handler mHandler = null;
    private static final int MSG_PROGRESS = 1;
    private static final int MSG_COMPLETED = 2;
    private volatile boolean abort = false;

    public static Intent newIntent(Context context) {
        Intent intent = new Intent(context, EffectActivity.class);
        return intent;
    }

    public static void intentTo(Context context) {
        context.startActivity(newIntent(context));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_effect);

        mAudioUtils = new XmAudioUtils();
        mPlayer = new AudioPlayer();

        mBtnNsSwitch = findViewById(R.id.btn_ns);
        mBtnNsSwitch.setOnClickListener(this);

        mBtnLimitSwitch = findViewById(R.id.btn_limiter);
        mBtnLimitSwitch.setOnClickListener(this);

        mBtnAudition = findViewById(R.id.btn_audition);
        mBtnAudition.setOnClickListener(this);

        ((RadioGroup) findViewById(R.id.voice_group)).setOnCheckedChangeListener(this);
        ((RadioGroup) findViewById(R.id.eq_group)).setOnCheckedChangeListener(this);
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
                            mBtnAudition.setText("停止试听");
                            stopAudition();
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
        stopAudition();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        mAudioUtils.release();
        super.onDestroy();
    }

    private void startAudition() {
        JsonUtils.generateJsonFile(config, mEffectsInfoMap);
        JsonUtils.readJsonFile(config);
        mBtnAudition.setText("停止试听");
        Runnable runnable = new Runnable() {
            @Override
            public void run() {
                if (!mPlayer.isPlayerStarted()) {
                    mPlayer.startPlayer(AudioManager.STREAM_MUSIC, 44100, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
                }

                long startTime = System.currentTimeMillis();
                JsonUtils.createOutputFile(effect);
                int bufferSize = 1024;
                short[] buffer = new short[bufferSize];
                File outEffectPcm = new File(effect);
                if (outEffectPcm.exists()) outEffectPcm.delete();
                FileOutputStream osEffectPcm = null;
                try {
                    osEffectPcm = new FileOutputStream(outEffectPcm);
                } catch (FileNotFoundException e) {
                    e.printStackTrace();
                }

                int ret = mAudioUtils.add_effects_init(config);
                if (ret < 0) {
                    Log.e(TAG, "add_effects_init failed");
                    return;
                }

                ret = mAudioUtils.add_effects_seekTo(10000);
                if (ret < 0) {
                    Log.e(TAG, "add_effects_seekTo failed");
                    return;
                }

                long cur_size = 0;
                abort = false;
                while (!abort) {
                    ret = mAudioUtils.get_effects_frame(buffer, bufferSize);
                    if (ret <= 0) {
                        mPlayer.stopPlayer();
                        break;
                    }
                    try {
                        if (mPlayer.isPlayerStarted()) {
                            mPlayer.play(buffer, 0, ret);
                        }
                        byte[] data = Utils.getByteArrayInLittleOrder(buffer);
                        osEffectPcm.write(data, 0, 2*ret);
                        osEffectPcm.flush();
                    } catch (IOException e) {
                        e.printStackTrace();
                    }
                    cur_size += ret;
                    if (1000 * (cur_size / (float)44100 / 1) > 33000) {
                        break;
                    }
                }

                ret = mAudioUtils.add_effects_seekTo(127226);
                if (ret < 0) {
                    Log.e(TAG, "add_effects_seekTo failed");
                    return;
                }

                while (!abort) {
                    ret = mAudioUtils.get_effects_frame(buffer, bufferSize);
                    if (ret <= 0) {
                        mPlayer.stopPlayer();
                        break;
                    }
                    try {
                        if (mPlayer.isPlayerStarted()) {
                            mPlayer.play(buffer, 0, ret);
                        }
                        byte[] data = Utils.getByteArrayInLittleOrder(buffer);
                        osEffectPcm.write(data, 0, 2*ret);
                        osEffectPcm.flush();
                    } catch (IOException e) {
                        e.printStackTrace();
                    }
                }
                long endTime = System.currentTimeMillis();
                Log.i(TAG, "cost time "+(float)(endTime - startTime)/(float)1000);
                mPlayer.stopPlayer();
                mHandler.sendMessage(mHandler.obtainMessage(MSG_COMPLETED));
            }
        };
        new Thread(runnable).start();
    }

    private void stopAudition() {
        mBtnAudition.setText("试听");
        //mPlayer.stopPlayer();
        abort = true;
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();
        if (id == R.id.btn_audition) {
            if (mBtnAudition.getText().toString().contentEquals("试听")) {
                startAudition();
            } else if (mBtnAudition.getText().toString().contentEquals("停止试听")) {
                stopAudition();
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

