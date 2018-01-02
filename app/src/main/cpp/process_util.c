#include <string.h>
#include "process_util.h"

/**
 * プロセス名からPIDを取得する
 *
 * @param process_name プロセス名
 */
int find_pid_of(const char * process_name) {
    int id;
    pid_t pid = -1;
    DIR *dir;
    FILE *fp;
    char filename[32];
    char cmdline[256];

    struct dirent *entry;

    if (process_name == NULL)
        return -1;

    dir = opendir("/proc");
    if (dir == NULL)
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        id = atoi(entry->d_name);
        if (id != 0) {
            sprintf(filename, "/proc/%d/cmdline", id);
            fp = fopen(filename, "r");
            if (fp) {
                fgets(cmdline, sizeof(cmdline), fp);
                fclose(fp);

                if (strcmp(process_name, cmdline) == 0) {
                    /* process found */
                    pid = id;
                    break;
                }
            }
        }
    }

    closedir(dir);
    return pid;
}

/**
 * プロセス内におけるライブラリのメモリマップ開始位置を取得する
 *
 * @param pid 対象プロセスID
 * @param module_name ライブラリ名
 */
void *get_library_address(pid_t pid, const char *module_name) {
    FILE *fp;
    long address = 0;
    char *pch;
    char filename[32];
    char line[1024];

    if (pid < 0) {
        /* self process */
        snprintf(filename, sizeof(filename), "/proc/self/maps");
    } else {
        snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    }

    fp = fopen(filename, "r");

    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, module_name)) {
                pch = strtok(line, "-");
                address = strtoul(pch, NULL, 16);

                if (address == 0x8000)
                    address = 0;

                break;
            }
        }

        fclose(fp);
    }

    return (void *) address;
}

/**
 * 対象プロセスにおけるライブラリ内の関数のアドレスを取得
 *
 * @param target_pid 対象プロセスID
 * @param lib_name 対象ライブラリ名
 * @param local_func_address 自プロセスの対象ライブラリ内関数のアドレス
 */
void *get_remote_func_address(pid_t target_pid, const char *lib_name, void *local_func_address) {
    void *local_module_base, *remote_module_base;

    // 自プロセスにおける対象ライブラリの開始アドレス
    local_module_base = get_library_address(-1, lib_name);
    // 対象プロセスにおける対象ライブラリの開始アドレス
    remote_module_base = get_library_address(target_pid, lib_name);

    /*目标进程函数地址= 目标进程lib库地址 + （本进程函数地址 -本进程lib库地址）*/
    // 対象プロセスにおける対象関数のアドレス =
    //   対象プロセスにおける対象ライブラリの開始アドレス + (自プロセスにおける対象関数のアドレス - 自プロセスにおける対象ライブラリの開始アドレス)
    void *target_address = (void *) ((uint32_t) remote_module_base +
                                     (uint32_t) local_func_address -
                                     (uint32_t) local_module_base);

    return target_address;
}
