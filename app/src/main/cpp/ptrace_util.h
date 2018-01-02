#ifndef __PTRACE_UTIL_H__
#define __PTRACE_UTIL_H__

#include <stdio.h>
#include <stdlib.h>
#include <asm/ptrace.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "log_util.h"

/**
 * 対象プロセスのメモリ上の指定アドレスからデータを読み出す
 * @param pid 対象プロセスID
 * @param src 指定アドレス
 * @param buf 読み出し先
 * @param size 読み出しサイズ
 * @return 成否
 */
int32_t ptrace_readdata(pid_t pid, const uint8_t *src, const uint8_t *buf, size_t size);

/**
 * 対象プロセスのメモリ上の指定アドレスへデータを書き込む
 * @param pid 対象プロセスID
 * @param dest 指定アドレス
 * @param data 書き込むデータ
 * @param size 書き込みサイズ
 * @return 成否
 */
int32_t ptrace_writedata(pid_t pid, const uint8_t *dest, const uint8_t *data, size_t size);

/**
 * 対象プロセス上で指定された関数アドレスを実行
 * @param pid  対象プロセスID
 * @param addr 対象関数のアドレス
 * @param params パラメータ (実行するアドレスによって異なる)
 * @param num_params パラメータ数
 * @param regs 対象プロセスのレジスタ(事前に取得しておく)
 * @return 成否
 */
int32_t ptrace_call(pid_t pid, uint32_t addr, const long *params, uint32_t num_params, struct pt_regs* regs);

/**
 * 対象プロセスのレジスタを取得する
 * @param pid 対象プロセスID
 * @param regs レジスタ構造体
 * @return 成否
 */
int32_t ptrace_getregs(pid_t pid, const struct pt_regs * regs);

/**
 * 対象プロセスにレジスタを設定する
 * @param pid 対象プロセスID
 * @param regs レジスタ構造体
 * @return 成否
 */
int32_t ptrace_setregs(pid_t pid, const struct pt_regs * regs);

/**
 * 停止した対象プロセスの実行を再開させる。
 * 対象プロセスにはシグナルを配送しない。
 * @param pid 対象プロセスID
 * @return 成否
 */
int32_t ptrace_continue(pid_t pid);

/**
 * 対象プロセスへのアタッチ
 * @param pid 対象プロセスID
 * @return 成否
 */
int32_t ptrace_attach(pid_t pid);

/**
 * 対象プロセスからのデタッチ
 * @param pid 対象プロセスID
 * @return 成否
 */
int32_t ptrace_detach(pid_t pid);

/**
 * ptrace実行結果の取得
 * @param regs レジスタ
 * @return 実行結果
 */
long ptrace_retval(struct pt_regs * regs);

/**
 * PCレジスタの取得
 * @param regs レジスタ
 * @return PCレジスタの内容
 */
long ptrace_pc(struct pt_regs *regs);

/**
 * ptrace経由で関数を実行
 * @param target_pid 対象プロセスID
 * @param func_name 関数名
 * @param func_addr 関数アドレス
 * @param parameters 関数に渡すパラメータ
 * @param param_num パラメータ数
 * @param regs 対象プロセスのレジスタ
 * @return 成否
 */
int32_t ptrace_call_wrapper(pid_t target_pid, const char * func_name, uint32_t * func_addr, long * parameters, uint32_t param_num, struct pt_regs * regs);

#endif
