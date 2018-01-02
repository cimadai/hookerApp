package net.cimadai.hookerApp;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends AppCompatActivity {
    final String TAG = HookTool.TAG;
    final String TargetApp = "net.cimadai.victim_app";

    /**
     * @param message Toastするメッセージ
     */
    void toastMessage(String message) {
        Toast.makeText(MainActivity.this,
                message,
                Toast.LENGTH_SHORT).show();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        final File execFile = new File(MainActivity.this.getFilesDir()+"/inject");
        final File soFile = new File(MainActivity.this.getFilesDir().getParentFile().getPath() + "/lib/libhooker.so");
        final File apkFile = new File(MainActivity.this.getFilesDir()+"/app-debug.apk");
        final File cacheDir = new File(MainActivity.this.getCacheDir().getParentFile().getParentFile().getAbsolutePath() + "/" + TargetApp + "/cache");

        try {
            String folder = "armeabi-v7a";
            AssetManager assetManager = getAssets();
            InputStream in = assetManager.open(folder + "/" + "inject");

            OutputStream out = this.openFileOutput("inject", MODE_PRIVATE);
            byte[] buff = new byte[1024];
            long size = 0;
            int nRead;
            while ((nRead = in.read(buff)) != -1) {
                out.write(buff, 0, nRead);
                size += nRead;
            }
            out.flush();
            out.close();

            Boolean ret = execFile.setExecutable(true);
            Log.d(TAG, String.format("Exec path: %s %b", execFile.getAbsolutePath(), ret));
        } catch (IOException e) {
            Toast.makeText(MainActivity.this, "Failed to extract injector!", Toast.LENGTH_SHORT).show();
        }

        // Targetアプリのstd::randをフックする
        Button hookApi = (Button) findViewById(R.id.hook_api);
        hookApi.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.d(TAG, "HookApi onclick");
                final String method = "hook_entry";
                String[] cmd = new String[] {
                        "su",
                        "-c",
                        execFile.getAbsolutePath(), // inject
                        TargetApp, // target process
                        soFile.getAbsolutePath(), // inject so path
                        method, // method name
                        "1", // param count
                        soFile.getAbsolutePath() // param
                };

                runCommand(method, cmd);
            }
        });

        // フックしたTargetアプリのstd::randを元に戻す
        Button unhookApi = (Button) findViewById(R.id.unhook_api);
        unhookApi.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.d(TAG, "UnHookApi onclick");
                final String method = "unhook_entry";
                String[] cmd = new String[] {
                        "su",
                        "-c",
                        execFile.getAbsolutePath(), // inject
                        TargetApp, // target process
                        soFile.getAbsolutePath(), // inject so path
                        method, // method name
                        "0" // param count
                };

                runCommand(method, cmd);
            }
        });

        // TargetアプリのコアコードのActivityThreadをフックする
        Button injectDex = (Button) findViewById(R.id.inject_dex);
        injectDex.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.d(TAG, "InjectDex onclick");
                if (apkFile.exists() && apkFile.canRead()) {
                    final String method = "inject_entry";
                    String[] cmd = new String[] {
                            "su",
                            "-c",
                            execFile.getAbsolutePath(), // inject
                            TargetApp, // target process
                            soFile.getAbsolutePath(), // inject so path
                            method, // method name
                            "4", // param count
                            apkFile.getAbsolutePath(), // param 1 (inject dex file path)
                            cacheDir.getAbsolutePath(), // param 2 (cache dir)
                            "net/cimadai/hookerApp/HookTool", // param 3 (inject class)
                            "dexInject" // param 4 (inject method)
                    };
                    Log.d(TAG, apkFile.getAbsolutePath());
                    Log.d(TAG, cacheDir.getAbsolutePath());

                    runCommand(method, cmd);
                } else {
                    if (!apkFile.exists()) {
                        toastMessage("There is no app-debug.apk or not.");
                    }
                    if (!apkFile.canRead()) {
                        toastMessage("The app-debug.apk is not readable.");
                    }
                }
            }
        });
    }

    void runCommand(String label, String[] cmd) {
        Process p;
        try {
            p = Runtime.getRuntime().exec(cmd);
            p.waitFor();
            if (p.exitValue() == 0) {
                toastMessage("Succeeded to " + label);
            } else {
                toastMessage("Failed to " + label);
            }
        } catch (InterruptedException | IOException e) {
            e.printStackTrace();
            toastMessage("Failed to " + label);
        }
    }
}
