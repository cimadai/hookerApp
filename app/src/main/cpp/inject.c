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