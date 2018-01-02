#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <jni.h>
#include <dlfcn.h>
#include "log_util.h"

JNIEnv* (*getJNIEnv)();

/**
 * DEXファイルをインジェクトし、メソッドを実行する。
 * @param dexPath インジェクトするdexファイル
 * @param dexOptDir キャッシュパス。対象アプリ・対象プロセスの書き込み権限に注意。
 * @param className インジェクト後に実行したいクラス名
 * @param methodName 実行したいメソッド名
 * @param argc 引数の数
 * @param argv 引数
 * @return
 */
int invoke_dex_method(const char* dexPath, const char* dexOptDir, const char* className, const char* methodName, int argc, char *argv[]) {
    LOGD("dexPath = %s, dexOptDir = %s, className = %s, methodName = %s\n", dexPath, dexOptDir, className, methodName);
    // JNIEnvの取得
    void* handle = dlopen("/system/lib/libandroid_runtime.so", RTLD_NOW);
    getJNIEnv = dlsym(handle, "_ZN7android14AndroidRuntime9getJNIEnvEv");
    JNIEnv* env = getJNIEnv();
    LOGD("JNIEnv = %x\n", env);

    // ClassLoaderのgetSystemClassLoaderを呼び出し、現在のプロセスのClassLoaderを取得
    jclass classloaderClass = (*env)->FindClass(env,"java/lang/ClassLoader");
    jmethodID getsysloaderMethod = (*env)->GetStaticMethodID(env,classloaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = (*env)->CallStaticObjectMethod(env, classloaderClass, getsysloaderMethod);
    LOGD("loader = %x\n", loader);

    // 現在のClassLoaderで処理するために、DexClassLoaderでdexファイルを読み込む
    jstring dexpath = (*env)->NewStringUTF(env, dexPath);
    jstring dex_odex_path = (*env)->NewStringUTF(env,dexOptDir);
    jclass dexLoaderClass = (*env)->FindClass(env,"dalvik/system/DexClassLoader");
    jmethodID initDexLoaderMethod = (*env)->GetMethodID(env, dexLoaderClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    jobject dexLoader = (*env)->NewObject(env, dexLoaderClass, initDexLoaderMethod,dexpath,dex_odex_path,NULL,loader);
    LOGD("dexLoader = %x\n", dexLoader);

    // DexClassLoaderを使って実行するコードを読み込む
    jmethodID findclassMethod = (*env)->GetMethodID(env,dexLoaderClass,"findClass","(Ljava/lang/String;)Ljava/lang/Class;");
    jstring javaClassName = (*env)->NewStringUTF(env,className);
    jclass javaClientClass = (*env)->CallObjectMethod(env,dexLoader,findclassMethod,javaClassName);
    if (!javaClientClass) {
        LOGD("Failed to load target class %s\n", className);
        printf("Failed to load target class %s\n", className);
        return -1;
    }

    // インジェクトするメソッドを取得する
    jmethodID start_inject_method = (*env)->GetStaticMethodID(env, javaClientClass, methodName, "()V");
    if (!start_inject_method) {
        LOGD("Failed to load target method %s\n", methodName);
        printf("Failed to load target method %s\n", methodName);
        return -1;
    }

    // メソッドを実行 (このメソッドはpublic static voidなメソッドである必要がある。)
    (*env)->CallStaticVoidMethod(env,javaClientClass,start_inject_method);
    return 0;
}