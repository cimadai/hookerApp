# AndroidでNative API/Java APIをフックする方法

この記事ではAndroid端末で対象のアプリが使っているNative APIやJava APIをフックして、任意の処理を差し込む方法を解説します。

## 目的

1. 対象プロセス(VictimApp)がNDK内で利用している `std::rand` をフックして任意の値を返すようにする。
2. 対象プロセス(VictimApp)のandroid.app.ActivityThread(AndroidのPrivate API)をフックし、任意の処理を差し込む。


## 用意するもの

1. rootedな端末(本記事で利用したのは rootedなAndroid 5.1.1端末)
2. Android Studio 3.0.1
3. ADB, Android SDK/NDK

Androidをroot化する方法や、各種開発環境のセットアップは本記事では割愛します。

## 作るもの

1. 任意の処理を含むライブラリ (本記事では `libhooker.so` )
2. 対象プロセス上で任意の処理を実行するための実行バイナリ (本記事では `inject` )
3. 任意のJava Classを含むAPK (本記事では `hooker_app.apk` )

本記事で引用しているソースコードは以下にあります。

#### APIフックを実行するアプリ
https://github.com/cimadai/hookerApp

#### APIフックの犠牲となるアプリ
https://github.com/cimadai/victimApp


## APIフックの手法(概要)

対象プロセスで任意の処理を実行するために、まず実行したい関数を持つライブラリを作成します。( `libhooker.so` )

次に、ライブラリを対象プロセスにインジェクトするための実行バイナリ( `inject` )を作成します。

このライブラリインジェクトの手順はNative APIフックとJava APIフックのどちらも共通です。

### Native APIフックの概要

1. ライブラリインジェクト (インジェクト用プロセス/Native World)
    - ptraceを利用し、対象プロセスにフック用ライブラリを読み込ませる。
    - その後ライブラリ内の自前の関数(フック用関数)を対象プロセス上で実行する。
2. Native APIフック (対象プロセス/Native World)
    - 対象プロセス上で改めて任意ライブラリを読み込み、メモリ上に展開(dlopen)する。
    - そのライブラリに含まれる関数と、置き換えたいNative APIを置換する。

### Java APIフックの概要

1. ライブラリインジェクト (インジェクト用プロセス/Native World)
    - ptraceを利用し、対象プロセスにフック用ライブラリを読み込ませる。
    - その後ライブラリ内の自前の関数(フック用関数)を対象プロセス上で実行する。
2. Java Classインジェクト (対象プロセス/Native World)
    - 対象プロセス上のJNIEnv経由で、フック用のAPKをclassLoaderに読み込ませる。
    - そのAPKに含まれる関数を、対象プロセス上で実行する。
3. Java APIフック (対象プロセス/Java World)
    - リフレクションを利用し、Private APIを任意の処理に置換する。


## APIフックの手法解説

### 手順1. 実行したい関数を持つライブラリを作成

まず始めに、対象プロセスにインジェクトしたいライブラリを作成します。

#### inject.c (抜粋) 

```
// placeholder
int (*old_rand)() = NULL;
 
// 置き換える関数
int new_rand() {
    return 99;
}
 
/**
 * std::randを置き換える
 * @param new_rand 置き換え関数のアドレス
 * @param old_rand プレースホルダー
 * @return
 */
int hook_rand(uint32_t new_rand, uint32_t *old_rand) {
    if (registerInlineHook((uint32_t) rand, new_rand, &old_rand) != ELE7EN_OK) {
        LOGD("Failed registerInlineHook\n");
        return 1;
    }
    if (inlineHook((uint32_t) rand) != ELE7EN_OK) {
        LOGD("Failed inlineHook\n");
        return 2;
    }

    return 0;
}
 
/**
 * inject関数からinject_remote_processで呼び出される
 * @param so_file_path
 * @return
 */
int hook_entry(char *so_file_path) {
    LOGD("Hook success, pid = %d\n", getpid());
    LOGD("Hello %s\n", so_file_path);
    void *hooker = dlopen(so_file_path, RTLD_NOW);
    uint32_t new_rand_pointer = (uint32_t) dlsym(hooker, "new_rand");
    uint32_t *old_rand_pointer = (uint32_t *) dlsym(hooker, "old_rand");
    LOGD("Hook result = %d\n", hook_rand(new_rand_pointer, old_rand_pointer));
    return 0;
}
```

インジェクトするライブラリは、実行するためのエントリポイントを用意しておきます。(ここでは `hook_entry`)
このライブラリは、実行時にptraceによって一時的に対象プロセスにロードされ、実行が終わったらアンロードされます。

そのため、関数アドレスの置き換えなどのように恒久的に変化させておきたい場合は、上記例のように関数内で改めてdlopenを用いて
ライブラリをロードします。
（このようにしないと、`hook_entry` を抜けた後に置き換えた関数アドレスが不正なものになってしまう。）

この例で使っている inlineHook は https://github.com/ele7enxxh/Android-Inline-Hook を利用させてもらいました。

ARMプロセッサのレジスタを書き換え、対象関数を置き換えるということをしています。

### 手順2. ライブラリを対象プロセスにインジェクトする実行ファイルの作成

#### inject.c
```
#include <string.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include "process_util.h"
#include "ptrace_util.h"
 
#define FUNCTION_NAME_ADDR_OFFSET       0x100
#define FUNCTION_OFFSET                 0x100
 
const char *libc_path =  "/system/lib/libc.so";
const char *linker_path = "/system/bin/linker";
 
/**
 * 指定されたプロセスに対してsoファイルをinjectし、そのプロセス上で関数を実行する。
 *
 * @param target_pid: 対象プロセスのPID
 * @param library_path: injectしたいsoファイルのパス
 * @param function_name: 実行したいsoファイル内の関数
 * @param program_args: 上記関数に渡す引数
 */
int inject_remote_process(pid_t target_pid, const char *library_path,
                          const char *function_name, uint32_t param_count,
                          const char **program_args) {
 
    LOGD("start injecting process< %d > \n", target_pid);
 
    // 1. プロセスにアタッチする
    if (ptrace_attach(target_pid) < 0) {
        LOGD("attach error");
        return -1;
    }
 
    // 2. 対象プロセスのレジスタを取得し、保持しておく。
    struct pt_regs regs, original_regs;
    if (ptrace_getregs(target_pid, &regs) < 0) {
        LOGD("getregs error");
        return -1;
    }
    memcpy(&original_regs, &regs, sizeof(regs));
 
    // 3. 対象プロセスのmmap関数のアドレスを取得
    unsigned int target_mmap_addr = (unsigned int) get_remote_func_address(target_pid, libc_path, (void *) mmap);
    LOGD("target mmap address: %x\n", target_mmap_addr);
 
    // 4. 対象プロセスのmmap関数を使ってメモリを割り当てる
    // mmapの引数 see: https://linuxjm.osdn.jp/html/LDP_man-pages/man2/mmap.2.html
    // void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
    long parameters[6];
    parameters[0] = 0;  // addr
    parameters[1] = 0x400; // length
    parameters[2] = PROT_READ | PROT_WRITE | PROT_EXEC; // prot
    parameters[3] = MAP_ANONYMOUS | MAP_PRIVATE; // flags
    parameters[4] = 0; // fd
    parameters[5] = 0; // offset
 
    if (ptrace_call_wrapper(target_pid, "mmap", target_mmap_addr, parameters, 6, &regs) < 0) {
        LOGD("call target mmap error");
        return -1;
    }
 
    // mmap関数の実行結果取得 (mmap() は成功するとマップされた領域へのポインターを返す。)
    uint8_t *target_mmap_base = ptrace_retval(&regs);
    LOGD("target_mmap_base: %x\n", target_mmap_base);
 
    // 5. 対象プロセスの、dlopen関数のインジェクトを行う
 
    // 関数プロトタイプ：void *dlopen(const char *filename, int flag);
 
    // 対象プロセスのdlopen関数のアドレスを取得する
    void *target_dlopen_addr = get_remote_func_address(target_pid, linker_path, (void *) dlopen);
    LOGD("target dlopen address: %x\n", target_dlopen_addr);
 
    // mmapで割り当てられたアドレスにInjectしたいライブラリパスを書き込む
    ptrace_writedata(target_pid, target_mmap_base, library_path, strlen(library_path) + 1);
 
    // dlopen用パラメータ設定
    parameters[0] = target_mmap_base; // filename: library_path
    parameters[1] = RTLD_NOW | RTLD_GLOBAL; // flag: 要解決、シンボルをグローバル公開
 
    // 対象プロセス上でdlopenを実行
    if (ptrace_call_wrapper(target_pid, "dlopen", target_dlopen_addr, parameters, 2, &regs) < 0) {
        LOGD("call target dlopen error");
        return -1;
    }
 
    // dlopenの実行結果取得 (dlopen() は成功するとライブラリのハンドルを返す。)
    void *target_so_handle = ptrace_retval(&regs);
 
    // 6. dlsym関数により、ライブラリ内の対象関数のアドレスをインジェクトする。
 
    // 関数プロトタイプ：void *dlsym(void *handle, const char *symbol);
 
    // 対象プロセスのdlsym関数のアドレスを取得する
    void *target_dlsym_addr = get_remote_func_address(target_pid, linker_path, (void *) dlsym);
    LOGD("target dlsym address: %x\n", target_dlsym_addr);
 
    // 実行したい関数名をメモリに書き込む
    ptrace_writedata(target_pid, target_mmap_base + FUNCTION_NAME_ADDR_OFFSET, function_name,
                     strlen(function_name) + 1);

    parameters[0] = target_so_handle; // handle: 対象ライブラリのハンドル
    parameters[1] = target_mmap_base + FUNCTION_NAME_ADDR_OFFSET; // symbol: 関数名
 
    // 対象プログラム上でdlsymを実行
    if (ptrace_call_wrapper(target_pid, "dlsym", target_dlsym_addr, parameters, 2, &regs) < 0) {
        LOGD("call target dlsym error");
        return -1;
    }
 
    // dlsymの実行結果取得 (dlsym() は成功すると指定されたシンボルのアドレスを返す。)
    void *hook_func_addr = ptrace_retval(&regs);
    LOGD("target %s address: %x\n", function_name, target_dlsym_addr);
 
    // 7. 対象関数を呼び出す
    // 関数が必要とするパラメータをメモリに書き込む
    for (uint32_t i = 0; i < param_count; ++i) {
        ptrace_writedata(target_pid,
                         target_mmap_base + FUNCTION_OFFSET * (2 + i),
                         (const uint8_t *) program_args[i],
                         strlen(program_args[i]) + 1);
        parameters[i] = target_mmap_base + FUNCTION_OFFSET * (2 + i);
    }
 
    // 対象プログラム上で対象関数を実行
    if (ptrace_call_wrapper(target_pid, function_name, hook_func_addr, parameters, param_count, &regs) < 0) {
        LOGD("call target %s error", function_name);
        return -1;
    }
 
    // 8. dlclose関数を呼び出す
    // 関数プロトタイプ: int dlclose(void *handle);
    void *target_dlclose_addr = get_remote_func_address(target_pid, linker_path, (void *) dlclose);
 
    parameters[0] = target_so_handle; // 対象ライブラリのハンドル
 
    // 対象プログラム上でdlcloseを実行
    if (ptrace_call_wrapper(target_pid, "dlclose", target_dlclose_addr, parameters, 1, &regs) < -1) {
        LOGD("call target dlclose error");
        return -1;
    }
 
    // 9. 元々のレジスタを復元する
    ptrace_setregs(target_pid, &original_regs);
 
    // 10. 対象プロセスからデタッチ
    ptrace_detach(target_pid);
 
    return 0;
}
 
int main(int argc, char **argv) {
 
    char * target_process_name = argv[1];
    char * so_file_path = argv[2];
    char * method_name = argv[3];
    uint32_t param_count = (uint32_t) atoi(argv[4]);
 
    pid_t target_pid;
    target_pid = find_pid_of(target_process_name);
    if (-1 == target_pid) {
        LOGD("Can't find the process\n");
        return -1;
    }
 
    char * program_args[param_count];
    for (uint32_t i = 0; i < (argc - 5) && i < param_count; ++i) {
        program_args[i] = argv[5 + i];
    }
 
    inject_remote_process(target_pid,
                          so_file_path,
                          method_name,
                          param_count,
                          (const char **) program_args
    );
 
    return 0;
}
```

本プログラムの引数は以下の通りです。

- argv[1] : インジェクトの犠牲対象となるプロセスの名前
- argv[2] : インジェクトしたいsoファイルのパス
- argv[3] : インジェクトしたいsoファイル内の関数名
- argv[4] : 関数が受取る引数の個数
- argv[5]以降 : 関数が受取る引数(char *)

任意ライブラリのインジェクトおよびインジェクトしたライブラリ内の関数の実行は以下の流れになります。

1. PTRACE_ATTACHで、対象プロセスにアタッチする
2. PTRACE_GETREGSで、対象プロセスのARMプロセッサレジスタを取得する
3. 対象プロセスのmmap関数のアドレスを取得する
4. 対象プロセス上でmmapを実行しメモリを確保する
5. 対象プロセスのdlopen関数のアドレスを取得し、対象プロセスにインジェクトしたいライブラリをロードする
6. 対象プロセスのdlsym関数のアドレスを取得し、対象プロセス上の実行したい関数のシンボルを検索してアドレスを取得する
7. 対象プロセス上でインジェクトしたライブラリ内の関数を実行する
8. 対象プロセスのdlclose関数のアドレスを取得し、対象プロセスからインジェクトしたライブラリをアンロードする
9. PTRACE_SETREGSで、2で取得したレジスタを書き戻す
10. PTRACE_DETACHで、対象プロセスからデタッチする


### 手順3. インジェクト実行ファイルの実行

手順2で作成した `inject` を端末上で直接実行するか、アプリを通じて実行すると対象プロセスにインジェクトすることができます。

直接実行の例)
```
PC>$ adb shell
Android>$ su
Android># cd /data/data/net.cimadai.hooker_app/files
Android># ./inject net.cimadai.victim_app /data/data/net.cimadai.hooker_app/lib/libhooker.so hook_entry
```

アプリから実行する例) (抜粋)
```
public class MainActivity extends AppCompatActivity {
    final String TAG = HookTool.TAG;
    final String TargetApp = "net.cimadai.victim_app";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        final File execFile = new File(MainActivity.this.getFilesDir()+"/inject");
        final File soFile = new File(MainActivity.this.getFilesDir().getParentFile().getPath() + "/lib/libhooker.so");

        // injectはassetsに入れているので、filesディレクトリに書き出す。
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
```

アプリから実行する場合は、`inject` をAPK内のassetsに含めておき、アプリ内でfilesディレクトリに展開して利用します。

また、この時にsupersuなどの確認ダイアログがポップアップしますので許可をしてください。


## 参考にしたURL

- 任意のプロセスにSOファイルをインジェクトする
    - https://github.com/yangbean9/injectDemo
- Native worldで任意の関数をフックする 
    - https://github.com/ele7enxxh/Android-Inline-Hook
- 任意のアプリにクラスをインジェクトする
    - http://taoyuanxiaoqi.com/2015/03/16/dexinject/

