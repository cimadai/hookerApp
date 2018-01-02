#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include "log_util.h"

/**
 * プロセス名からPIDを取得する
 *
 * @param process_name プロセス名
 */
int find_pid_of(const char *process_name) ;

/**
 * プロセス内におけるライブラリのメモリマップ開始位置を取得する
 *
 * @param pid 対象プロセスID
 * @param module_name ライブラリ名
 */
void* get_library_address(pid_t pid, const char *module_name);

/**
 * 対象プロセスにおけるライブラリ内の関数のアドレスを取得
 *
 * @param target_pid 対象プロセスID
 * @param lib_name 対象ライブラリ名
 * @param local_func_address 自プロセスの対象ライブラリ内関数のアドレス
 */
void* get_remote_func_address(pid_t target_pid, const char* lib_name,void* local_func_address);
