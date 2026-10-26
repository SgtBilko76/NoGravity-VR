package com.bilko.nogravityvr;

import android.os.Bundle;
import android.util.Log;

import org.libsdl.app.SDLActivity;

/**
 * No Gravity VR for Meta Quest: SDL2 (statically linked into libmain.so) hosts the RLX engine
 * with the GLES 3 renderer; the OpenXR side lives in port/quest/src/ng_quest.cpp.
 * The game data (NOGRAVITY.RMX, GPL) ships inside the APK and is copied to the app's
 * internal files dir on first start, so no storage permission is needed.
 * Launch arguments for tests: adb shell am start -n com.bilko.nogravityvr/.NoGravityActivity --es args "..."
 */
public class NoGravityActivity extends SDLActivity {
    private static final String TAG = "NoGravityVR";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String[] getLibraries() {
        // SDL2 is linked statically into libmain.so (its JNI_OnLoad registers the natives).
        return new String[] { "openxr_loader", "main" };
    }

    @Override
    protected String[] getArguments() {
        String args = getIntent() != null ? getIntent().getStringExtra("args") : null;
        if (args == null || args.isEmpty()) return new String[0];
        Log.i(TAG, "launch args: " + args);
        return args.trim().split("\\s+");
    }

    @Override
    protected void onDestroy() {
        // SDL joins its native thread without a timeout; make sure the process really ends.
        Thread watchdog = new Thread(() -> {
            try { Thread.sleep(3000); } catch (InterruptedException ignored) { }
            android.os.Process.killProcess(android.os.Process.myPid());
        });
        watchdog.setDaemon(true);
        watchdog.start();
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
