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
#include <isa.h>
#include "local-include/reg.h"

const char *regs[] = {
  "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
  "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
  "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
  "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

/* 打印所有通用寄存器和 pc 的当前值, 供调试器的 `info r` 命令调用。
 * 各 ISA 的实现方式可以不同, 这里按 RISC-V 的寄存器表逐个输出。 */
void isa_reg_display() {
  for (int i = 0; i < (int)ARRLEN(cpu.gpr); i ++) {
    printf("%-4s = " FMT_WORD "\n", regs[i], cpu.gpr[i]);
  }
  printf("%-4s = " FMT_WORD "\n", "pc", cpu.pc);
}

/* 根据寄存器名返回寄存器的值, 供 PA1.5 的表达式求值器使用。
 * TODO: 在表达式求值中支持寄存器时实现。 */
word_t isa_reg_str2val(const char *s, bool *success) {
  (void)s;
  *success = false;
  return 0;
}
