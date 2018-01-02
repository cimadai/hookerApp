package net.cimadai.hookerApp;

import android.os.Handler;
import android.os.Message;
import android.util.Log;

/**
 * ActivityThread内の`mH: Handle`内のmCallbackをフックするクラス
 * http://androidxref.com/5.1.1_r6/xref/frameworks/base/core/java/android/os/Handler.java
 * コンストラクタで元々のコールバックを渡す。
 */
public class HookCallback implements Handler.Callback {
    private final String TAG = HookTool.TAG;
    public static final int RESUME_ACTIVITY         = 107;
    public static final int PAUSE_ACTIVITY          = 101;

    private Handler.Callback mParentCallback;
    public HookCallback(Handler.Callback parentCallback){
        mParentCallback = parentCallback;
    }

    @Override
    public boolean handleMessage(Message msg) {
        switch (msg.what) {
            case RESUME_ACTIVITY:
                Log.d(TAG, "Hook activity resume!!!");
                break;
            case PAUSE_ACTIVITY:
                Log.d(TAG, "Hook activity pause!!!");
                break;
            default:
                Log.d(TAG, "Hook a " + msg.what);
                break;
        }

        if (mParentCallback != null){
            return mParentCallback.handleMessage(msg);
        }else{
            return false;
        }
    }
}
