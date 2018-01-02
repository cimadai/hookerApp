package net.cimadai.hookerApp;

import android.annotation.SuppressLint;
import android.os.Handler;
import android.util.Log;

import java.lang.reflect.Field;

public class HookTool {
    public static final String TAG = "INJECT";

    public static void dexInject() throws ClassNotFoundException, IllegalAccessException {
        Log.d(TAG, "This is dex code. Start hooking process in Java world.");

        try {
            // PrivateなActivityThreadをフックする。
            // see: http://androidxref.com/5.1.1_r6/xref/frameworks/base/core/java/android/app/ActivityThread.java#sCurrentActivityThread
            @SuppressLint("PrivateApi") Class<?> activityThreadClass = Class.forName("android.app.ActivityThread");

            Field currentActivityThreadField = activityThreadClass.getDeclaredField("sCurrentActivityThread");
            currentActivityThreadField.setAccessible(true);
            Object currentActivityThread = currentActivityThreadField.get(null);

            Field mHField = activityThreadClass.getDeclaredField("mH");
            mHField.setAccessible(true);
            Handler mH = (Handler) mHField.get(currentActivityThread);

            Field mCallbackField = Handler.class.getDeclaredField("mCallback");
            mCallbackField.setAccessible(true);
            Handler.Callback oriCallback = (Handler.Callback) mCallbackField.get(mH);
            Handler.Callback hookCallBack = new HookCallback(oriCallback);
            mCallbackField.set(mH, hookCallBack);
        } catch (IllegalArgumentException | NoSuchFieldException e) {
            e.printStackTrace();
            Log.d(TAG, "Failed to hook in Java world.");
        }
    }

}

