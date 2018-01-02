#include "ptrace_util.h"

/**
 * see man of ptrace: http://surf.ml.seikei.ac.jp/~nakano/JMwww/html/LDP_man-pages/man2/ptrace.2.html
 */

#define CPSR_T_MASK     ( 1u << 5 )

/**
 * ptrace実行結果の取得
 * @param regs レジスタ
 * @return 実行結果
 */
long ptrace_retval(struct pt_regs * regs) {
	return regs->ARM_r0;
}

/**
 * PCレジスタの取得
 * @param regs レジスタ
 * @return PCレジスタの内容
 */
long ptrace_pc(struct pt_regs *regs) {
	return regs->ARM_pc;
}

/**
 * 対象プロセスのメモリ上の指定アドレスからデータを読み出す
 * @param pid 対象プロセスID
 * @param src 指定アドレス
 * @param buf 読み出し先
 * @param size 読み出しサイズ
 * @return 成否
 */
int32_t ptrace_readdata(pid_t pid, const uint8_t *src, const uint8_t *buf, size_t size)
{
    uint32_t i, j, remain;
    uint8_t *laddr;

    union u {
        long val;
        char chars[sizeof(long)];
    } d;

    j = size / 4;
    remain = size % 4;
    laddr = buf;
	
    for (i = 0; i < j; i ++) {
        d.val = ptrace(PTRACE_PEEKTEXT, pid, src, 0);
        memcpy(laddr, d.chars, 4);
        src += 4;
        laddr += 4;
    }

    if (remain > 0) {
        d.val = ptrace(PTRACE_PEEKTEXT, pid, src, 0);
        memcpy(laddr, d.chars, remain);
    }

    return 0;
}

/**
 * 対象プロセスのメモリ上の指定アドレスへデータを書き込む
 * @param pid 対象プロセスID
 * @param dest 指定アドレス
 * @param data 書き込むデータ
 * @param size 書き込みサイズ
 * @return 成否
 */
int32_t ptrace_writedata(pid_t pid, const uint8_t *dest, const uint8_t *data, size_t size)
{
    uint32_t i, j, remain;
    const uint8_t *laddr;

    union u {
        long val;
        char chars[sizeof(long)];
    } d;

    j = size / 4;
    remain = size % 4;

    laddr = data;

    for (i = 0; i < j; i ++) {
        memcpy(d.chars, laddr, 4);
        ptrace(PTRACE_POKETEXT, pid, dest, d.val);
        dest  += 4;
        laddr += 4;
    }

    if (remain > 0) {
        d.val = ptrace(PTRACE_PEEKTEXT, pid, dest, 0);
        for (i = 0; i < remain; i ++) {
            d.chars[i] = *laddr ++;
        }

        ptrace(PTRACE_POKETEXT, pid, dest, d.val);
    }

    return 0;
}

/**
 * 対象プロセス上で指定された関数アドレスを実行
 * @param pid  対象プロセスID
 * @param addr 対象関数のアドレス
 * @param params パラメータ (実行するアドレスによって異なる)
 * @param num_params パラメータ数
 * @param regs 対象プロセスのレジスタ(事前に取得しておく)
 * @return 成否
 */
int32_t ptrace_call(pid_t pid, uint32_t addr, const long *params, uint32_t num_params, struct pt_regs* regs)
{
    uint32_t i;
    // 初めの4つのパラメータ(r1〜r4)はそのままレジスタに渡される
    for (i = 0; i < num_params && i < 4; i ++) {
        regs->uregs[i] = params[i];
    }

    // 渡されたパラメータが4以上なら、4をスタックポインタ(r13)に書き込む
    if (i < num_params) {
        regs->ARM_sp -= (num_params - i) * sizeof(long) ;
        ptrace_writedata(pid, (void *)regs->ARM_sp, (uint8_t *)&params[i], (num_params - i) * sizeof(long));
    }

    // プログラムカウンタ(r15)を対象関数のアドレスにする
    regs->ARM_pc = (long) addr;

    // https://hiro99ma.blogspot.jp/2012/04/armcpsr.html
    if (regs->ARM_pc & 1) {
        /* thumb */
        regs->ARM_pc &= (~1u);
        regs->ARM_cpsr |= CPSR_T_MASK;
    } else {
        /* arm */
        regs->ARM_cpsr &= ~CPSR_T_MASK;
    }

    // リンクレジスタ(関数の戻りアドレス)を0にすることで、対象プロセスはアドレスエラー後に中断され、制御がデバッグプロセスに戻る
    regs->ARM_lr = 0;

    // 対象プロセスに対してレジスタを設定し、実行を継続させる
    if (ptrace_setregs(pid, regs) == -1 || ptrace_continue(pid) == -1) {
        return -1;
    }

    // 対象プロセスの状態が変わるまで待つ
    int stat = 0;
    waitpid(pid, &stat, WUNTRACED);
    while (stat != 0xb7f) {
        if (ptrace_continue(pid) == -1) {
            return -1;
        }
        waitpid(pid, &stat, WUNTRACED);
    }

    return 0;
}

/**
 * 対象プロセスのレジスタを取得する
 * @param pid 対象プロセスID
 * @param regs レジスタ構造体
 * @return 成否
 */
int32_t ptrace_getregs(pid_t pid, const struct pt_regs * regs) {
    if (ptrace(PTRACE_GETREGS, pid, NULL, regs) < 0) {
        return -1;
    }

    return 0;
}

/**
 * 対象プロセスにレジスタを設定する
 * @param pid 対象プロセスID
 * @param regs レジスタ構造体
 * @return 成否
 */
int32_t ptrace_setregs(pid_t pid, const struct pt_regs * regs) {
    if (ptrace(PTRACE_SETREGS, pid, NULL, regs) < 0) {
        perror("ptrace_setregs: Can not set register values");
        return -1;
    }

    return 0;
}

/**
 * 停止した対象プロセスの実行を再開させる。
 * 対象プロセスにはシグナルを配送しない。
 * @param pid 対象プロセスID
 * @return 成否
 */
int32_t ptrace_continue(pid_t pid) {
    if (ptrace(PTRACE_CONT, pid, NULL, 0) < 0) {
        perror("ptrace_cont");
        return -1;
    }

    return 0;
}

/**
 * 対象プロセスへのアタッチ
 * @param pid 対象プロセスID
 * @return 成否
 */
int32_t ptrace_attach(pid_t pid) {
    if (ptrace(PTRACE_ATTACH, pid, NULL, 0) < 0) {
        return -1;
    }

    int status = 0;
    waitpid(pid, &status , WUNTRACED);

    return 0;
}

/**
 * 対象プロセスからのデタッチ
 * @param pid 対象プロセスID
 * @return 成否
 */
int32_t ptrace_detach(pid_t pid) {
    if (ptrace(PTRACE_DETACH, pid, NULL, 0) < 0) {
        perror("ptrace_detach");
        return -1;
    }

    return 0;
}

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
int32_t ptrace_call_wrapper(pid_t target_pid, const char * func_name, uint32_t * func_addr, long * parameters, uint32_t param_num, struct pt_regs * regs) {
    LOGD("Calling [%s] in target process <%d> \n", func_name,target_pid);
    if (ptrace_call(target_pid, (uint32_t)func_addr, parameters, param_num, regs) < 0) {
        return -1;
	}
	
    if (ptrace_getregs(target_pid, regs) < 0) {
        return -1;
	}
    LOGD("Target process returned from %s, return value=%x, pc=%ld \n",
         func_name, ptrace_retval(regs), ptrace_pc(regs));
    return 0;
}
