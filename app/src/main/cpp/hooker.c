#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include "inline_hook.h"
#include "log_util.h"
#include "invoke_dex_method.h"

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
 * 置き換えたstd::randを元に戻す
 * @return
 */
int unhook_rand() {
    if (inlineUnHook((uint32_t) rand) != ELE7EN_OK) {
        return 3;
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
    LOGD("Injecting %s\n", so_file_path);
    void *hooker = dlopen(so_file_path, RTLD_NOW);
    uint32_t new_rand_pointer = (uint32_t) dlsym(hooker, "new_rand");
    uint32_t *old_rand_pointer = (uint32_t *) dlsym(hooker, "old_rand");
    LOGD("Hook result = %d\n", hook_rand(new_rand_pointer, old_rand_pointer));
    return 0;
}

/**
 * inject関数からinject_remote_processで呼び出される
 * @param param (未使用)
 * @return
 */
int unhook_entry() {
    LOGD("Unhook result = %d\n", unhook_rand());
    return 0;
}

/**
 * APKのメソッドをインジェクトして実行する
 * @param apkPath インジェクトしたいAPK(DEX)
 * @param cachePath 対象プロセスが書き込み権限を持つディレクトリ
 * @param className APK内の実行したいクラス名
 * @param methodName クラス内の実行したいメソッド名
 * @return
 */
int inject_entry(char * apkPath, char * cachePath, char * className, char * methodName) {
    LOGD("Start inject entry: %s, %s, %s, %s\n", apkPath, cachePath, className, methodName);
    int ret = invoke_dex_method(apkPath, cachePath, className, methodName, 0, NULL);
    LOGD("APK inject result = %d\n", ret);
    return 0;
}
