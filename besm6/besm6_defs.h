// besm6_defs.h: BESM-6 simulator definitions
//
// Copyright (c) 2009, Serge Vakulenko
// Copyright (c) 2009, Leonid Broukhis
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Except as contained in this notice, the name of Leonid Broukhis or
// Serge Vakulenko shall not be used in advertising or otherwise to promote
// the sale, use or other dealings in this Software without prior written
// authorization from Leonid Broukhis and Serge Vakulenko.
#ifndef _BESM6_DEFS_H_
#define _BESM6_DEFS_H_ 0

#include "sim_defs.h" // simulator defns
#include "scp.h"

// Rename of global PC variable to avoid namespace conflicts on some platforms

#define PC PC_Global

// Memory.
#define NREGS   30           // number of registers-modifiers
#define MEMSIZE (512 * 1024) // memory size, words

// Drums and disks.
//
// One zone contains 1024 words of user memory and 8 system data words.
// Every word (t_value) is stored as 8-byte record, low byte first.
// System data is stored first, then user data.
//
// Images are read and written with plain fread/fwrite, so the host must be
// little-endian; on a big-endian host the records would come out byte-reversed.
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "BESM-6 disk and drum images are little-endian; this host is not."
#endif

#define ZONE_SIZE (8 + 1024)         // 1kword zone size, words
#define DRUM_SIZE (256 * ZONE_SIZE)  // drum size per controller, words
#define DISK_SIZE (1024 * ZONE_SIZE) // disk size per unit, words

// Simulator stop codes
enum {
    STOP_STOP = 1,         // STOP
    STOP_RUNOUT,           // run out end of memory limits
    STOP_BADCMD,           // invalid instruction
    STOP_INSN_CHECK,       // not an instruction
    STOP_INSN_PROT,        // fetch from blocked page
    STOP_OPERAND_PROT,     // load from blocked page
    STOP_RAM_CHECK,        // RAM parity error
    STOP_CACHE_CHECK,      // data cache parity error
    STOP_OVFL,             // arith. overflow
    STOP_DIVZERO,          // division by 0 or denorm
    STOP_DOUBLE_INTR,      // double internal interrupt
    STOP_DRUMINVDATA,      // reading unformatted drum
    STOP_DISKINVDATA,      // reading unformatted disk
    STOP_INSN_ADDR_MATCH,  // fetch address matched breakpt reg
    STOP_LOAD_ADDR_MATCH,  // load address matched watchpt reg
    STOP_STORE_ADDR_MATCH, // store address matched watchpt reg
    STOP_UNIMPLEMENTED,    // unimplemented 033 or 002 insn feature
};

// Bits of a machine word, right to left, starting at 1.
#define BBIT(n)   (1 << (n - 1))       // a single bit, 1 to 32
#define BIT40     000010000000000000LL // bit 40 - top bit of the mantissa
#define BIT41     000020000000000000LL // bit 41 - sign
#define BIT42     000040000000000000LL // bit 42 - duplicate sign in the mantissa
#define BIT48     004000000000000000LL // bit 48 - sign of the exponent
#define BIT49     010000000000000000LL // bit 49
#define BITS(n)   (~0U >> (32 - n))    // mask of bits n..1
#define BITS40    00017777777777777LL  // bits 40..1 - mantissa
#define BITS41    00037777777777777LL  // bits 41..1 - mantissa and sign
#define BITS42    00077777777777777LL  // bits 42..1 - mantissa and both signs
#define BITS48    07777777777777777LL  // bits 48..1
#define BITS48_42 07740000000000000LL  // bits 48..42 - exponent
#define ADDR(x)   ((x) & BITS(15))     // word address

// Parity tags. A word's tag bits hold what registers ПКЛ and ПКП
// contained when the word was written.
// 00 - instruction parity
// 01 or 10 - number check
// 11 - number parity
// In memory the tag bits stand in for the parity of the half-words.
#define PARITY_INSN      1
#define PARITY_NUMBER    2
#define SET_PARITY(x, c) (((x) & BITS48) | (((c) & 3LL) << 48))
#define IS_INSN(x)       (((x) >> 48) == PARITY_INSN)
#define IS_NUMBER(x)     (((x) >> 48) == PARITY_INSN || ((x) >> 48) == PARITY_NUMBER)

// An attempt to approximate instruction execution times.
// The arguments number of clock ticks spent on an instruction
// in the ALU and in the CU; the computed result assumes
// a 50% overlap in execution.
#define MEAN_TIME(x, y) (x > y ? x + y / 2 : x / 2 + y)

#define USEC      1             // 1 microsecond
#define MSEC      (1000 * USEC) // 1 millisecond
#define CLK_TPS   250           // Fast Clock Ticks Per Second (every 4ms)
#define CLK_DELAY 4000          // Uncalibrated instructions per clock tick

extern UNIT cpu_unit;
extern UNIT mmu_unit;
extern UNIT tty_unit[];
extern UNIT md_unit[];
extern UNIT drum_unit[];
extern UNIT clocks[];
extern t_value memory[MEMSIZE];
extern t_value pult[11][8];
extern unsigned pult_packet_switch; // selector of hardwired programs

extern uint32 PC, RK, Aex, RAU, RUU;
extern uint32 M[NREGS];
extern t_value BRZ[8], RP[8], GRP, MGRP;
extern uint32 PRP, MPRP;
extern t_value ACC, RMR;
extern uint32 BAZ[8], TABST, RZ;
extern uint32 READY;  // read by ext 4031
extern uint32 READY2; // read by ext 4102
extern DEVICE cpu_dev, drum_dev, mmu_dev;
extern DEVICE md_dev[];
extern DEVICE clock_dev;
extern DEVICE tty_dev;
extern jmp_buf cpu_halt;

t_stat disk_attach(UNIT *u, const char *cptr);
t_stat drum_attach(UNIT *u, const char *cptr);
t_stat tty_attach(UNIT *u, const char *cptr);

// MMU unit flags: `set mmu cache' / `set mmu check'.
#define CACHE_ENB 1
#define CHECK_ENB 2

// TTY unit flags: the encoding, three bits wide, of which two are raw.
#define TTY_UNICODE_CHARSET     0
#define TTY_KOI7_JCUKEN_CHARSET (1 << UNIT_V_UF)
#define TTY_KOI7_QWERTY_CHARSET (2 << UNIT_V_UF)
#define TTY_RAW_CHARSET         (3 << UNIT_V_UF)
#define TTY_RAW8_CHARSET        (4 << UNIT_V_UF)
#define TTY_CHARSET_MASK        (7 << UNIT_V_UF)

// Bits of the ALU (АУ) mode register.
#define RAU_NORM_DISABLE  001 // normalization disabled
#define RAU_ROUND_DISABLE 002 // rounding disabled
#define RAU_LOG           004 // logical group
#define RAU_MULT          010 // multiplication group
#define RAU_ADD           020 // addition group
#define RAU_OVF_DISABLE   040 // overflow disabled

#define RAU_MODE              (RAU_LOG | RAU_MULT | RAU_ADD)
#define SET_MODE(x, m)        (((x) & ~RAU_MODE) | (m))
#define SET_LOGICAL(x)        (((x) & ~RAU_MODE) | RAU_LOG)
#define SET_MULTIPLICATIVE(x) (((x) & ~RAU_MODE) | RAU_MULT)
#define SET_ADDITIVE(x)       (((x) & ~RAU_MODE) | RAU_ADD)
#define IS_LOGICAL(x)         (((x) & RAU_MODE) == RAU_LOG)
#define IS_MULTIPLICATIVE(x)  (((x) & (RAU_ADD | RAU_MULT)) == RAU_MULT)
#define IS_ADDITIVE(x)        ((x) & RAU_ADD)

// Synthetic control unit (УУ) mode register; absent on the real machine.
#define RUU_PARITY_RIGHT  000001 // ПКП - parity check of the right half
#define RUU_PARITY_LEFT   000002 // ПКЛ - parity check of the left half
#define RUU_EXTRACODE     000004 // РежЭ - extracode mode
#define RUU_INTERRUPT     000010 // РежПр - interrupt mode
#define RUU_MOD_RK        000020 // ПрИК - modification by register М[16]
#define RUU_AVOST_DISABLE 000040 // БРО - halt mode disabled
#define RUU_RIGHT_INSTR   000400 // ПрК - right instruction

#define IS_SUPERVISOR(x)     ((x) & (RUU_EXTRACODE | RUU_INTERRUPT))
#define SET_SUPERVISOR(x, m) (((x) & ~(RUU_EXTRACODE | RUU_INTERRUPT)) | (m))

// Special registers.
#define MOD  020 // address modifier
#define PSW  021 // control unit (УУ) modes
#define SPSW 027 // saved control unit (УУ) modes
#define ERET 032 // return address from an extracode
#define IRET 033 // return address from an interrupt
#define IBP  034 // execution breakpoint address
#define DWP  035 // read/write watchpoint address

// Register 021: control unit (УУ) modes.
// PSW: program status word.
#define PSW_MMAP_DISABLE 000001 // БлП - address mapping disabled
#define PSW_PROT_DISABLE 000002 // БлЗ - protection disabled
// ПоП - halt on any internal interrupt.
#define PSW_INTR_HALT 000004
// ПоК - halt on a parity-check interrupt.
#define PSW_CHECK_HALT 000010
// Зп(М29) - the operand address of a memory write matched register М29.
#define PSW_WRITE_WATCH 000020

#define PSW_INTR_DISABLE 002000 // БлПр - external interrupts disabled
#define PSW_AUT_B        004000 // АвтБ - Automat B mode

// Register 027: saved control unit (УУ) modes.
// SPSW: saved program status word.
#define SPSW_MMAP_DISABLE 000001 // БлП - address mapping disabled
#define SPSW_PROT_DISABLE 000002 // БлЗ - protection disabled
#define SPSW_EXTRACODE    000004 // РежЭ - extracode mode
#define SPSW_INTERRUPT    000010 // РежПр - interrupt mode
// ПрИК(РК) - register РК holds an instruction that is to be modified
// by register М[16].
#define SPSW_MOD_RK 000020
// ПрИК(РР) - register РР holds an instruction that was executed with
// modification.
#define SPSW_MOD_RR 000040

#define SPSW_UNKNOWN     000100 // НОК? pencilled into volume 9
#define SPSW_RIGHT_INSTR 000400 // ПрК - right instruction

// ГД./ДК2 - register РК holds the instruction following the one that
// caused the interrupt.
#define SPSW_NEXT_RK 001000

#define SPSW_INTR_DISABLE 002000 // БлПр - external interrupts disabled

// Unicode Cyrillic.
#define CYRILLIC_CAPITAL_LETTER_A         0x0410
#define CYRILLIC_CAPITAL_LETTER_BE        0x0411
#define CYRILLIC_CAPITAL_LETTER_VE        0x0412
#define CYRILLIC_CAPITAL_LETTER_GHE       0x0413
#define CYRILLIC_CAPITAL_LETTER_DE        0x0414
#define CYRILLIC_CAPITAL_LETTER_IE        0x0415
#define CYRILLIC_CAPITAL_LETTER_ZHE       0x0416
#define CYRILLIC_CAPITAL_LETTER_ZE        0x0417
#define CYRILLIC_CAPITAL_LETTER_I         0x0418
#define CYRILLIC_CAPITAL_LETTER_SHORT_I   0x0419
#define CYRILLIC_CAPITAL_LETTER_KA        0x041a
#define CYRILLIC_CAPITAL_LETTER_EL        0x041b
#define CYRILLIC_CAPITAL_LETTER_EM        0x041c
#define CYRILLIC_CAPITAL_LETTER_EN        0x041d
#define CYRILLIC_CAPITAL_LETTER_O         0x041e
#define CYRILLIC_CAPITAL_LETTER_PE        0x041f
#define CYRILLIC_CAPITAL_LETTER_ER        0x0420
#define CYRILLIC_CAPITAL_LETTER_ES        0x0421
#define CYRILLIC_CAPITAL_LETTER_TE        0x0422
#define CYRILLIC_CAPITAL_LETTER_U         0x0423
#define CYRILLIC_CAPITAL_LETTER_EF        0x0424
#define CYRILLIC_CAPITAL_LETTER_HA        0x0425
#define CYRILLIC_CAPITAL_LETTER_TSE       0x0426
#define CYRILLIC_CAPITAL_LETTER_CHE       0x0427
#define CYRILLIC_CAPITAL_LETTER_SHA       0x0428
#define CYRILLIC_CAPITAL_LETTER_SHCHA     0x0429
#define CYRILLIC_CAPITAL_LETTER_HARD_SIGN 0x042a
#define CYRILLIC_CAPITAL_LETTER_YERU      0x042b
#define CYRILLIC_CAPITAL_LETTER_SOFT_SIGN 0x042c
#define CYRILLIC_CAPITAL_LETTER_E         0x042d
#define CYRILLIC_CAPITAL_LETTER_YU        0x042e
#define CYRILLIC_CAPITAL_LETTER_YA        0x042f
#define CYRILLIC_SMALL_LETTER_A           0x0430
#define CYRILLIC_SMALL_LETTER_BE          0x0431
#define CYRILLIC_SMALL_LETTER_VE          0x0432
#define CYRILLIC_SMALL_LETTER_GHE         0x0433
#define CYRILLIC_SMALL_LETTER_DE          0x0434
#define CYRILLIC_SMALL_LETTER_IE          0x0435
#define CYRILLIC_SMALL_LETTER_ZHE         0x0436
#define CYRILLIC_SMALL_LETTER_ZE          0x0437
#define CYRILLIC_SMALL_LETTER_I           0x0438
#define CYRILLIC_SMALL_LETTER_SHORT_I     0x0439
#define CYRILLIC_SMALL_LETTER_KA          0x043a
#define CYRILLIC_SMALL_LETTER_EL          0x043b
#define CYRILLIC_SMALL_LETTER_EM          0x043c
#define CYRILLIC_SMALL_LETTER_EN          0x043d
#define CYRILLIC_SMALL_LETTER_O           0x043e
#define CYRILLIC_SMALL_LETTER_PE          0x043f
#define CYRILLIC_SMALL_LETTER_ER          0x0440
#define CYRILLIC_SMALL_LETTER_ES          0x0441
#define CYRILLIC_SMALL_LETTER_TE          0x0442
#define CYRILLIC_SMALL_LETTER_U           0x0443
#define CYRILLIC_SMALL_LETTER_EF          0x0444
#define CYRILLIC_SMALL_LETTER_HA          0x0445
#define CYRILLIC_SMALL_LETTER_TSE         0x0446
#define CYRILLIC_SMALL_LETTER_CHE         0x0447
#define CYRILLIC_SMALL_LETTER_SHA         0x0448
#define CYRILLIC_SMALL_LETTER_SHCHA       0x0449
#define CYRILLIC_SMALL_LETTER_HARD_SIGN   0x044a
#define CYRILLIC_SMALL_LETTER_YERU        0x044b
#define CYRILLIC_SMALL_LETTER_SOFT_SIGN   0x044c
#define CYRILLIC_SMALL_LETTER_E           0x044d
#define CYRILLIC_SMALL_LETTER_YU          0x044e
#define CYRILLIC_SMALL_LETTER_YA          0x044f

// Memory access routines.
extern void mmu_store(int addr, t_value word);
extern t_value mmu_load(int addr);
extern t_value mmu_fetch(int addr);
extern t_value mmu_prefetch(int addr, int actual);
extern void mmu_setcache(int idx, t_value word);
extern t_value mmu_getcache(int idx);
extern void mmu_setrp(int idx, t_value word);
extern void mmu_setup(void);
extern void mmu_setprotection(int idx, t_value word);
extern void mmu_print_brz(void);

// Utility functions
extern int odd_parity(unsigned char);

// GOST-10859 character set (besm6_gost.c).
extern int gost_latin;
unsigned short gost_to_unicode(unsigned char ch);
void uni2utf8(unsigned short ch, char buf[5]);

// Drum access.
void drum(int ctlr, uint32 cmd);
int drum_errors(void);

// Disk access.
void disk_io(int ctlr, uint32 cmd);
void disk_ctl(int ctlr, uint32 cmd);
int disk_state(int ctlr);
int disk_errors(void);

// Terminals (teletypes, Videotons, Consuls).
void tty_send(uint32 mask);
int tty_query(void);
void vt_print(void);
void tt_print(void);
void vt_receive(void);
void tt_receive(void);
void consul_print(int num, uint32 cmd);
uint32 consul_read(int num);
void mux_send(uint32 syllable);
uint32 mux_read(void);
void mux_clear(void);
int vt_is_idle(void);

// Debug output.
void besm6_fprint_cmd(FILE *of, uint32 cmd);
void besm6_fprint_insn(FILE *of, uint32 insn);
extern int besm6_latin;
void besm6_log(const char *fmt, ...);
void besm6_log_cont(const char *fmt, ...);
void besm6_debug(const char *fmt, ...);

// Instruction and register tracing (besm6_trace.c), gated by cpu_dev.dctrl.
void besm6_trace_reset(void);
void besm6_trace_instruction(void);
void besm6_trace_registers(void);
void besm6_trace_memory(int addr, t_value val, const char *opname);
void besm6_trace_exception(const char *message);
void besm6_trace_call_return(void);

// Symbol table extracted from an a.out image (besm6_trace.c),
// mapping function addresses to names for the call/return trace.
void besm6_sym_clear(void);
void besm6_sym_add(uint32 addr, const char *name);
void besm6_sym_sort(void);
const char *besm6_sym_find(uint32 addr, int *at_start);
t_stat fprint_sym(FILE *of, uint32 addr, t_value *val, UNIT *uptr, int32 sw);
t_stat sim_load(FILE *fi, const char *cptr, const char *fnam, int dump_flag);

// Arithmetic.
double besm6_to_ieee(t_value word);
void besm6_add(t_value val, int negate_acc, int negate_val);
void besm6_divide(t_value val);
void besm6_multiply(t_value val);
void besm6_change_sign(int sign);
void besm6_add_exponent(int val);
int besm6_highest_bit(t_value val);
void besm6_shift(int toright);
int besm6_count_ones(t_value word);
t_value besm6_pack(t_value val, t_value mask);
t_value besm6_unpack(t_value val, t_value mask);

// Bits of the main interrupt register ГРП (GRP)
// External:
#define GRP_PRN1_SYNC  04000000000000000LL // 48
#define GRP_PRN2_SYNC  02000000000000000LL // 47
#define GRP_DRUM1_FREE 01000000000000000LL // 46
#define GRP_DRUM2_FREE 00400000000000000LL // 45
#define GRP_VU1_SYNC   00200000000000000LL // 44
#define GRP_VU2_SYNC   00100000000000000LL // 43
#define GRP_FS1_SYNC   00040000000000000LL // 42
#define GRP_FS2_SYNC   00020000000000000LL // 41
#define GRP_TIMER      00010000000000000LL // 40
#define GRP_PRN1_ZERO  00004000000000000LL // 39
#define GRP_PRN2_ZERO  00002000000000000LL // 38
#define GRP_SLAVE      00001000000000000LL // 37
#define GRP_CHAN3_DONE 00000400000000000LL // 36
#define GRP_CHAN4_DONE 00000200000000000LL // 35
#define GRP_CHAN5_DONE 00000100000000000LL // 34
#define GRP_CHAN6_DONE 00000040000000000LL // 33
#define GRP_PANEL_REQ  00000020000000000LL // 32
#define GRP_TTY_START  00000010000000000LL // 31
#define GRP_IMITATION  00000004000000000LL // 30
#define GRP_CHAN3_FREE 00000002000000000LL // 29
#define GRP_CHAN4_FREE 00000001000000000LL // 28
#define GRP_CHAN5_FREE 00000000400000000LL // 27
#define GRP_CHAN6_FREE 00000000200000000LL // 26
#define GRP_CHAN7_FREE 00000000100000000LL // 25
#define GRP_SERIAL     00000000001000000LL // 19, nonstandard
#define GRP_WATCHDOG   00000000000002000LL // 11
#define GRP_SLOW_CLK   00000000000001000LL // 10, nonstandard
// Internal:
#define GRP_DIVZERO    00000000034000000LL // 23-21
#define GRP_OVERFLOW   00000000014000000LL // 22-21
#define GRP_CHECK      00000000004000000LL // 21
#define GRP_OPRND_PROT 00000000002000000LL // 20
#define GRP_WATCHPT_W  00000000000200000LL // 17
#define GRP_WATCHPT_R  00000000000100000LL // 16
#define GRP_INSN_CHECK 00000000000040000LL // 15
#define GRP_INSN_PROT  00000000000020000LL // 14
#define GRP_ILL_INSN   00000000000010000LL // 13
#define GRP_BREAKPOINT 00000000000004000LL // 12
#define GRP_PAGE_MASK  00000000000000760LL // 9-5
#define GRP_RAM_CHECK  00000000000000010LL // 4
#define GRP_BLOCK_MASK 00000000000000007LL // 3-1

#define GRP_SET_BLOCK(x, m) (((x) & ~GRP_BLOCK_MASK) | ((m) & GRP_BLOCK_MASK))
#define GRP_SET_PAGE(x, m)  (((x) & ~GRP_PAGE_MASK) | (((m) << 4) & GRP_PAGE_MASK))

// Bits of the peripheral interrupt register ПРП (PRP)
#define PRP_VU1_END      010000000 // 22
#define PRP_VU2_END      004000000 // 21
#define PRP_PCARD1_CHECK 002000000 // 20
#define PRP_PCARD2_CHECK 001000000 // 19
#define PRP_PCARD1_PUNCH 000400000 // 18
#define PRP_PCARD2_PUNCH 000200000 // 17
#define PRP_PTAPE1_PUNCH 000100000 // 16
#define PRP_PTAPE2_PUNCH 000040000 // 15

// 14-13 unused

#define PRP_CONS1_INPUT 000004000 // 12
#define PRP_CONS2_INPUT 000002000 // 11
#define PRP_CONS1_DONE  000001000 // 10
#define PRP_CONS2_DONE  000000400 // 9

#define PRP_PLOTTER   000000200 // 8
#define PRP_MUX_INPUT 000000100 // 7
#define PRP_MUX_DONE  000000040 // 6

// Number of the RAM block or page that raised the interrupt
extern uint32 iintr_data;
extern uint32 trace_counter;
#endif
