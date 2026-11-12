/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <isa.h>
#include <cpu/cpu.h>
#include <memory/paddr.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <time.h>
#include <errno.h>
#include "sdb.h"
#include "debug.h"
#include "utils.h"

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  Log(" exit. ");
  return -1;
}

static int cmd_help(char *args);
/* single execuation */
static int cmd_sigle_step_exec(char*);
/* display register status */
static int cmd_display_reg_status(char*);
/* scan memory */
static int cmd_scan_mem(char*);
/* expression evaluation */
static int cmd_exp_eval(char*);
/* set up monitoring points */
static int cmd_set_monitor_point(char*);
/* unset monitoring points */
static int cmd_unset_monitor_point(char*);

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },

  /* TODO: Add more commands */

  { "si",   "Execute N instructions step by step, then pause (default N = 1), e.g. `si 10`", cmd_sigle_step_exec },
  { "info", "Print register status or watchpoint information: `info r` | `info w`", cmd_display_reg_status },
  { "x",    "Examine memory: print N 4-byte values in hex from address EXPR, e.g. `x 10 0x80000000`", cmd_scan_mem },
  { "p",    "Evaluate expression EXPR and print its value (not implemented yet)", cmd_exp_eval },
  { "w",    "Set a watchpoint on EXPR; execution pauses when its value changes (not implemented yet)", cmd_set_monitor_point },
  { "d",    "Delete the watchpoint with number N, e.g. `d 2` (not implemented yet)", cmd_unset_monitor_point },
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

/* si [N]: 单步执行 N 条指令后暂停执行, N 缺省为 1。
 * 参数通过 strtok 从 sdb_mainloop() 尚未解析完的输入串中继续取出。 */
static int cmd_sigle_step_exec(char* args) {
  char *arg = strtok(NULL, " ");
  uint64_t n = 1;   /* 不带参数时默认只执行一条指令 */

  if (arg != NULL) {
    /* 把参数字符串转换成十进制整数, 并检查是否完整、合法 */
    char *endptr = NULL;
    errno = 0;
    long val = strtol(arg, &endptr, 10);
    if (errno != 0 || endptr == arg || *endptr != '\0' || val <= 0) {
      printf("Usage: si [N], where N is a positive integer (default: 1)\n");
      return 0;
    }
    n = (uint64_t)val;
  }

  /* 执行 n 条指令; cpu_exec() 返回后程序处于暂停状态 */
  cpu_exec(n);
  return 0;
}

/* info r|w: r 打印寄存器状态, w 打印监视点信息 */
static int cmd_display_reg_status(char* args) {
  char *arg = strtok(NULL, " ");   /* 取出子命令 r 或 w */

  if (arg == NULL) {
    printf("Usage: info r|w\n");
  }
  else if (strcmp(arg, "r") == 0) {
    /* 具体的寄存器打印由 ISA 相关的 isa_reg_display() 完成 */
    isa_reg_display();
  }
  else if (strcmp(arg, "w") == 0) {
    /* TODO: 监视点功能在 PA1.6 实现后, 这里遍历并打印所有监视点 */
    printf("No watchpoint is set.\n");
  }
  else {
    printf("Unknown info subcommand '%s'. Usage: info r|w\n", arg);
  }

  return 0;
}

/* x N EXPR: 以 EXPR 的值为起始地址, 连续打印 N 组 4 字节的值。
 * 按 PA1.4 的要求, 这里先实现简化版表达式: EXPR 只能是一个十六进制数,
 * 例如 `x 10 0x80000000`。 */
static int cmd_scan_mem(char* args) {
  char *n_str = strtok(NULL, " ");      /* 要打印的组数 N */
  char *addr_str = strtok(NULL, " ");   /* 起始地址 EXPR */

  if (n_str == NULL || addr_str == NULL) {
    printf("Usage: x N EXPR (e.g. x 10 0x80000000)\n");
    return 0;
  }

  /* 解析 N, 必须是正整数 */
  char *endptr = NULL;
  long n = strtol(n_str, &endptr, 10);
  if (endptr == n_str || *endptr != '\0' || n <= 0) {
    printf("Invalid count '%s'\n", n_str);
    return 0;
  }

  /* 解析起始地址; strtol 以 16 为基数时同样接受 "0x" 前缀 */
  word_t addr = (word_t)strtol(addr_str, &endptr, 16);
  if (endptr == addr_str || *endptr != '\0') {
    printf("Invalid address '%s' (a hexadecimal number is expected)\n", addr_str);
    return 0;
  }

  /* 通过 paddr_read() 读取客户机内存, 每次读 4 字节 (32 位) */
  for (long i = 0; i < n; i ++) {
    word_t cur = addr + (word_t)(4 * i);
    printf(FMT_WORD ": " FMT_WORD "\n", cur, paddr_read(cur, 4));
  }
  return 0;
}

/* p EXPR: 计算并打印表达式 EXPR 的值。
 * 依赖 PA1.5 的表达式求值器, 目前 expr.c 中尚未实现。 */
static int cmd_exp_eval(char* args) {
  /* TODO: 完成 expr() 后改为:
   *   bool success = true;
   *   word_t val = expr(args, &success);
   *   if (success) printf(FMT_WORD "\n", val); */
  (void)args;
  printf("Expression evaluation is not implemented yet.\n");
  return 0;
}

/* w EXPR: 设置监视点, 当 EXPR 的值发生变化时暂停程序。
 * 依赖 PA1.6 的监视点实现, 目前 watchpoint.c 中尚未实现。 */
static int cmd_set_monitor_point(char* args) {
  /* TODO: 完成 watchpoint.c 后, 用 expr() 求值并经 new_wp() 加入监视点池 */
  (void)args;
  printf("Watchpoints are not implemented yet.\n");
  return 0;
}

/* d N: 删除编号为 N 的监视点。
 * 依赖 PA1.6 的监视点实现, 目前 watchpoint.c 中尚未实现。 */
static int cmd_unset_monitor_point(char* args) {
  /* TODO: 完成 watchpoint.c 后调用 free_wp(NO) */
  (void)args;
  printf("Watchpoints are not implemented yet.\n");
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
