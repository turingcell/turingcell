// Copyright 2019 Sen Han <00hnes@gmail.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// ** md: mathematical deterministic function implementation in C **
// same type has different implementation on different platform is not allowed to be used
// in a `md` function, `int` type for example.
//
// unpredictable behaviour can be used as dtm-arm-api
//  1. ARM DDI 0100I page A4-6: add rd is r15 (but only work in usr and sys mode)
//  2. In all variants of ARMv4 and ARMv5, bits[1:0] of a value written to R15 in ARM state
//      must be 0b00. If they are not, the results are UNPREDICTABLE.

#include<stdint.h>

typedef struct {
    uint32_t R[31];     // register
    uint32_t cpsr;      // current program status register
    uint32_t spsr[6];   // saved program status register ([0] & [1] never used)

    // amount of all inst try to executed even failed in the end
    // for the example of the undef/inst-fetch-mem-abort/data-access-mem-abort etc...
    uint64_t inst_executed_ct_total;             // cleared after cpu reset (may rewind)
    uint64_t inst_executed_ct_in_this_execute;   // cleared after cpu reset and at the entry point
                                                 //   of function call `armv4cpu_execute`

    // temp bellow
    // all members below are **temporary** and does not need to be serialized
    uint64_t inst_ct_limit_in_this_execute;
    // need_suspend_flag // set when inst_executed_ct_in_this_execute >=
                         //     inst_ct_limit_in_this_execute
    uint8_t inst_enter_cpsr_negative_flag_ro;       // inst enter cpsr N state, readonly
    uint8_t inst_enter_cpsr_zero_flag_ro;           // inst enter cpsr Z state, readonly
    uint8_t inst_enter_cpsr_carryout_flag_ro;       // inst enter cpsr C state, readonly
    uint8_t inst_enter_cpsr_v_overflow_flag_ro;     // inst enter cpsr V state, readonly
    uint8_t inst_enter_cpumodn_ro;                  // inst enter cpumodn state, readonly
    uint8_t inst_enter_real_PC_ro;                  // inst enter real PC value, readonly
    uint8_t dp_do_not_write_result_to_rd_flag;      // do not need to write result to rd

    uint8_t dp_next_negative_flag;      // could only be used inside dp inst
    uint8_t dp_next_zero_flag;
    uint8_t dp_next_carry_out_flag;
    uint8_t dp_next_v_overflow_flag;
    uint32_t dp_op2;

    uint8_t next_negative_flag;     // could be used inside any valid inst
    uint8_t next_zero_flag;
    uint8_t next_carry_out_flag;
    uint8_t next_v_overflow_flag;

    // tell mmu this is LDRT/LDRBT/STRT/STRBT executing
    // mmu will treat all the data access as if the cpu is under USR mode, even if it is
    // actually under privileged mode
    uint8_t mmu_inst_is_ldrxt_strxt_flag;
    uint8_t mmu_data_access_need_abort_flag;

    uint32_t this_inst;
} armv4cpu_md_t;

const uint8_t gl_armv4_reg_const_lookup_array_mod_to_regtidx[16] =
// cpumod & 0b01111 =
//         0          1          2          3          7         11         15
// abbr.: usr -> u | fiq -> f | irq -> i | svc -> v | abt -> a | und -> u | sys -> s
//   u  f  i  v          a           u           s
//   0  1  2  3          7           11          15
    {0, 5, 4, 1, 1, 1, 1, 2, 2, 2, 2, 3, 0, 0, 0, 0};

const uint8_t gl_armv4_reg_const_lookup_array_regtidx_to_realr_idx[6][16] = {
// armv4cpu_md_t.R layout:
//     {usr|sys}.r[0:15] <-> R[0:15]
//          svc.r[13:14] <-> R[16:17]
//          abt.r[13:14] <-> R[18:19]
//          und.r[13:14] <-> R[20:21]
//          irq.r[13:14] <-> R[22:23]
//          fiq.r[8:14]  <-> R[24:30]

//                          r8                 r13 r14
    {0, 1, 2, 3, 4, 5, 6, 7, 8,  9,  10, 11, 12, 13, 14, 15}, // usr|sys
    {0, 1, 2, 3, 4, 5, 6, 7, 8,  9,  10, 11, 12, 16, 17, 15}, // svc
    {0, 1, 2, 3, 4, 5, 6, 7, 8,  9,  10, 11, 12, 18, 19, 15}, // abt
    {0, 1, 2, 3, 4, 5, 6, 7, 8,  9,  10, 11, 12, 20, 21, 15}, // und
    {0, 1, 2, 3, 4, 5, 6, 7, 8,  9,  10, 11, 12, 22, 23, 15}, // irq
    {0, 1, 2, 3, 4, 5, 6, 7, 24, 25, 26, 27, 28, 29, 30, 15}, // fiq
};

#define CPUMODEN_USR 0x10
#define CPUMODEN_FIQ 0x11
#define CPUMODEN_IRQ 0x12
#define CPUMODEN_SVC 0x13
#define CPUMODEN_ABT 0x17
#define CPUMODEN_UND 0x1b
#define CPUMODEN_SYS 0x1f

#define EXCEPTION_CPUMODEN_FIQ CPUMODEN_FIQ
#define EXCEPTION_CPUMODEN_IRQ CPUMODEN_IRQ
#define EXCEPTION_CPUMODEN_SVC CPUMODEN_SVC
#define EXCEPTION_CPUMODEN_ABT CPUMODEN_ABT
#define EXCEPTION_CPUMODEN_UND CPUMODEN_UND

#define EXCEPTION_VECTOR_ADDR_UND           ((uint32_t)0x04)
#define EXCEPTION_VECTOR_ADDR_SWI           ((uint32_t)0x08)
#define EXCEPTION_VECTOR_ADDR_INST_ABT      ((uint32_t)0x0c)
#define EXCEPTION_VECTOR_ADDR_DATA_ABT      ((uint32_t)0x10)
#define EXCEPTION_VECTOR_ADDR_IRQ           ((uint32_t)0x18)
#define EXCEPTION_VECTOR_ADDR_FIQ           ((uint32_t)0x1c)


// helpers
// get_cur_cpumod() // usr sys svc abrt undef irq fiq
inline uint32_t get_R(armv4cpu_md_t* cpup, uint8_t cpumodn, uint8_t r_idx){
    return cpup->R[
        gl_armv4_reg_const_lookup_array_regtidx_to_realr_idx[
            gl_armv4_reg_const_lookup_array_mod_to_regtidx[cpumodn & 0x0f]
        ][r_idx]
    ];
}

inline uint32_t set_R(armv4cpu_md_t* cpup, uint8_t cpumodn, uint8_t r_idx, uint32_t v){
    cpup->R[
        gl_armv4_reg_const_lookup_array_regtidx_to_realr_idx[
            gl_armv4_reg_const_lookup_array_mod_to_regtidx[cpumodn & 0x0f]
        ][r_idx]
    ] = v;
}

#define REGIDX_PC   15
#define REGIDX_R14  14

inline uint32_t get_PC(armv4cpu_md_t* cpup, uint8_t cpumodn){
    return get_R(cpup, cpumodn, REGIDX_PC);
}

inline uint32_t set_PC(armv4cpu_md_t* cpup, uint8_t cpumodn, uint32_t v){
    return set_R(cpup, cpumodn, REGIDX_PC, v);
}

inline uint32_t get_spsr(armv4cpu_md_t* cpup, uint8_t cpumodn){
    return cpup->spsr[
            gl_armv4_reg_const_lookup_array_mod_to_regtidx[cpumodn & 0x0f]
    ];
}

inline uint32_t set_spsr(armv4cpu_md_t* cpup, uint8_t cpumodn, uint32_t v){
    cpup->spsr[
            gl_armv4_reg_const_lookup_array_mod_to_regtidx[cpumodn & 0x0f]
    ] = v;
}

inline uint32_t get_cpsr(armv4cpu_md_t* cpup){
    return cpup->cpsr;
}

inline void set_cpsr(armv4cpu_md_t* cpup, uint32_t new_psr){
    cpup->cpsr = new_psr;
}

inline uint8_t get_cur_cpumodn(armv4cpu_md_t* cpup){
    return (uint8_t)(cpup->cpsr & 0x001f);
}

// ret u32: 0 for clr | 1 for set
inline uint32_t get_psr_N(uint32_t psr){
    return !!(psr & 0x80000000);
}

// ret u32: 0 for clr | 1 for set
inline uint32_t get_psr_Z(uint32_t psr){
    return !!(psr & 0x40000000);
}

// ret u32: 0 for clr | 1 for set
inline uint32_t get_psr_C(uint32_t psr){
    return !!(psr & 0x20000000);
}

// ret u32: 0 for clr | 1 for set
inline uint32_t get_psr_V(uint32_t psr){
    return !!(psr & 0x10000000);
}

#define likely(x)       (__builtin_expect(!!(x), 1))
#define unlikely(x)     (__builtin_expect(!!(x), 0))

#define if_likely(x)    if(likely(x))
#define if_unlikely(x)  if(unlikely(x))

#define assert(x) ((likely(EX))?((void)0):(abort()))

// idx must belong to [31:0] && topidx >= bottomidx
// ret:
//  (0b11101101, 5, 2) -> 0b1011
//  (0b11111111_11111111_11111111_11111101, 1, 1) -> 0b0
//  (0b00000000_00000000_00000000_00000001, 0, 0) -> 0b1
//  (0b10111111_11111111_11111111_11111111, 30, 30) -> 0b0
inline uint32_t bits_span_drop_to_floor_u32(
    uint32_t u32, uint32_t topidx, uint32_t bottomidx){

    return (((u32 << (31 - topidx)) >> (31 - topidx)) >> bottomidx);
}

// api
/*
armv4cpu_new // already hwreset-ed
armv4cpu_hwreset
armv4cpu_load_persistent_cpu_state
armv4cpu_save_persistent_cpu_state
armv4cpu_execute(inst_amount_limit) -> inst_amount_executed
armv4cpu_destroy

// dependent api
armv4cpu_inst_fetch_mem()
armv4cpu_data_read_mem()
armv4cpu_data_write_mem()
*/

// return 0 for fail or non-0 for success
// Note: unpredictable if inst_cond == 0b'1111
// TODO: use lookup table directly instead of the switch-case
inline uint32_t
armv4cpu_inst_cond_test_is_ok(uint32_t inst, uint32_t psr) {
    // ARM DDI 0100I: Page A3-4
    uint32_t cond = bits_span_drop_to_floor_u32(inst, 31, 28);
    if_likely(cond == 14){ // 0b'1110 AL
        return 1;
    }
    switch(cond){
        case 0: // 0b'0000 EQ
            return get_psr_Z(psr);
            break;
        case 1: // 0b'0001 NE
            return !get_psr_Z(psr);
            break;
        case 2: // 0b'0010 CS/HS
            return get_psr_C(psr);
            break;
        case 3: // 0b'0011 CC/LO
            return !get_psr_C(psr);
            break;
        case 4: // 0b'0100 MI
            return get_psr_N(psr);
            break;
        case 5: // 0b'0101 PL
            return !get_psr_N(psr);
            break;
        case 6: // 0b'0110 VS
            return get_psr_V(psr);
            break;
        case 7: // 0b'0111 VC
            return !get_psr_V(psr);
            break;
        case 8: // 0b'1000 HI
            return (get_psr_C(psr) && (!get_psr_Z(psr)));
            break;
        case 9: // 0b'1001 LS
            return ((!get_psr_C(psr)) || get_psr_Z(psr));
            break;
        case 10: // 0b'1010 GE
            return (get_psr_N(psr) == get_psr_V(psr));
            break;
        case 11: // 0b'1011 LT
            return (get_psr_N(psr) != get_psr_V(psr));
            break;
        case 12: // 0b'1100 GT
            return ((!get_psr_Z(psr)) && (get_psr_N(psr) == get_psr_V(psr)));
            break;
        case 13: // 0b'1101 LE
            return (get_psr_Z(psr) || (get_psr_N(psr) != get_psr_V(psr)));
            break;
        case 14: // 0b'1110 AL
            assert(0);
            break;
        case 15: // 0b'1111
            assert(0);
            break;
        default:
            assert(0);
    }
    assert(0);
}

// always_inline
inline void
armv4cpu_update_cpsr_NZCV_part(armv4cpu_md_t* cpup  , uint8_t n, uint8_t z, uint8_t c, uint8_t v){
    uint32_t psr = get_cpsr(cpup);
    psr = (~((uint32_t)0xf0000000)) & psr;
    psr = psr | (((uint32_t)((n << 3) | (z << 2) | (c << 1) | v)) << 28);
    set_cpsr(cpup, psr);
}

// always_inline
inline void
armv4cpu_inst_nop(armv4cpu_md_t* cpup  , uint8_t cpumodn){
    uint32_t nPc = get_PC(cpup, cpumodn) + 4;
    set_PC(cpup, cpumodn, nPc);
}

// always_inline
inline void
armv4cpu_inst_enter_init_tmp(armv4cpu_md_t* cpup){
    /* others NEEND to be implemented!!! */

    cpup->dp_next_negative_flag = 0;
    cpup->dp_next_zero_flag = 0;
    cpup->dp_next_carry_out_flag = 0;
    cpup->dp_next_v_overflow_flag = 0;
}

// logical shift left - lsl
// n must belong to [1, 31]
// always_inline
inline uint32_t
armv4cpu_lsl(uint32_t u32, uint8_t n){
    return u32 << n;
}

// logical shift right - lsr
// n must belong to [1, 31]
// always_inline
inline uint32_t
armv4cpu_lsr(uint32_t u32, uint8_t n){
    return u32 >> n;
}

// arithmetic shift right - asr
// n must belong to [1, 31]
// always_inline
inline uint32_t
armv4cpu_asr(uint32_t u32, uint8_t n){
    if(bits_span_drop_to_floor_u32(u32, 31, 31) == 0){
        return u32 >> n;
    }else{
        return ~((~u32) >> n);
    }
    assert(0);
}

// rotate right - ror
// n must belong to [1, 31]
// always_inline
inline uint32_t
armv4cpu_ror(uint32_t u32, uint8_t n){
    return (bits_span_drop_to_floor_u32(u32, n - 1, 0) << (32 - n)) | (u32 >> n);
}

// always_inline
inline void
armv4cpu_inst_dp_calc_op2_op2reg_immshift(armv4cpu_md_t* cpup){
    uint32_t shift_type = bits_span_drop_to_floor_u32(cpup->this_inst, 6, 5);       // [0, 3]
    uint8_t shift_amt = bits_span_drop_to_floor_u32(cpup->this_inst, 11, 7);        // [0, 31]
    uint8_t regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0);
    uint32_t reg32 = get_R(cpup, cpup->inst_enter_cpumodn_ro, regidx);              // Rm
    if_unlikely(regidx == REGIDX_PC){
        reg32 = reg32 + 8;
    }
    switch(shift_type){
        case 0: // logical shift left - lsl
            if_unlikely(shift_amt == 0){
                cpup->dp_next_carry_out_flag = cpup->inst_enter_cpsr_carryout_flag_ro;
                cpup->dp_op2 = reg32;
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 32 - shift_amt, 32 - shift_amt);
                cpup->dp_op2 = armv4cpu_lsl(reg32, shift_amt);
            }
            break;
        case 1: // logical shift right - lsr
            if_unlikely(shift_amt == 0){ // lsr #32
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 32 - 1, 32 - 1);
                cpup->dp_op2 = 0;
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, shift_amt - 1, shift_amt - 1);
                cpup->dp_op2 = armv4cpu_lsr(reg32, shift_amt);
            }
            break;
        case 2: // arithmetic shift right - asr
            if_unlikely(shift_amt == 0){ // asr #32
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 32 - 1, 32 - 1);
                cpup->dp_op2 = (~((uint32_t)cpup->dp_next_carry_out_flag)) + 1;
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, shift_amt - 1, shift_amt - 1);
                cpup->dp_op2 = armv4cpu_asr(reg32, shift_amt);
            }
            break;
        case 3: // rotate right - ror
            if_unlikely(shift_amt == 0){ // rrx
                cpup->dp_op2 = (((uint32_t)cpup->inst_enter_cpsr_carryout_flag_ro) << 31) | (reg32 >> 1);
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 0, 0);
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, shift_amt - 1, shift_amt - 1);
                cpup->dp_op2 = armv4cpu_ror(reg32, shift_amt);
            }
            break;
        default:
            assert(0);
    }
}

// always_inline
inline void
armv4cpu_inst_dp_calc_op2_op2reg_regshift(armv4cpu_md_t* cpup){
    uint32_t shift_type = bits_span_drop_to_floor_u32(cpup->this_inst, 6, 5);
    uint8_t shift_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 11, 8);
    // here we just left R15 be itself because if Rs == R15 the behaviour could be unpredictable
    //  according to the arm ref manual
    uint8_t shift_amt = bits_span_drop_to_floor_u32(
        get_R(cpup, cpup->inst_enter_cpumodn_ro, shift_regidx), 7, 0);
    uint8_t regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0);
    uint32_t reg32 = get_R(cpup, cpup->inst_enter_cpumodn_ro, regidx); // Rm
    if_unlikely(regidx == REGIDX_PC){
        reg32 = reg32 + 8;
    }
    if(shift_amt == 0){
        cpup->dp_next_carry_out_flag = cpup->inst_enter_cpsr_carryout_flag_ro;
        cpup->dp_op2 = reg32;
        return;
    }
    switch(shift_type){
        case 0: // logical shift left - lsl
            if(shift_amt >= 32){
                if(shift_amt == 32){
                    cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 0, 0);
                    cpup->dp_op2 = 0;
                }else{ // shift_amt > 32
                    cpup->dp_next_carry_out_flag = 0;
                    cpup->dp_op2 = 0;
                }
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 32 - shift_amt, 32 - shift_amt);
                cpup->dp_op2 = armv4cpu_lsl(reg32, shift_amt);
            }
            break;
        case 1: // logical shift right - lsr
            if(shift_amt >= 32){
                if(shift_amt == 32){
                    cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 31, 31);
                    cpup->dp_op2 = 0;
                }else{ // shift_amt > 32
                    cpup->dp_next_carry_out_flag = 0;
                    cpup->dp_op2 = 0;
                }
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, shift_amt - 1, shift_amt - 1);
                cpup->dp_op2 = armv4cpu_lsr(reg32, shift_amt);
            }
            break;
        case 2: // arithmetic shift right - asr
            if(shift_amt >= 32){
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 31, 31);
                cpup->dp_op2 = (~((uint32_t)cpup->dp_next_carry_out_flag)) + 1;
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, shift_amt - 1, shift_amt - 1);
                cpup->dp_op2 = armv4cpu_asr(reg32, shift_amt);
            }
            break;
        case 3: // rotate right - ror
            uint8_t new_shift_amt = bits_span_drop_to_floor_u32((uint32_t)shift_amt, 4, 0);
            if(new_shift_amt == 0){
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 31, 31);
                cpup->dp_op2 = reg32;
            }else{ // new_shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32,
                    new_shift_amt - 1, new_shift_amt - 1);
                cpup->dp_op2 = armv4cpu_ror(reg32, new_shift_amt);
            }
            /* // old and very redundant but correct implementation
            if(shift_amt >= 32){
                if(shift_amt == 32){
                    cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 31, 31);
                    cpup->dp_op2 = reg32;
                }else{ // shift_amt > 32
                    uint8_t new_shift_amt = bits_span_drop_to_floor_u32((uint32_t)shift_amt, 4, 0);
                    if(new_shift_amt == 0){
                        cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, 31, 31);
                        cpup->dp_op2 = reg32;
                    }else{ // new_shift_amt belong to [1, 31]
                        cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32,
                            new_shift_amt - 1, new_shift_amt - 1);
                        cpup->dp_op2 = armv4cpu_ror(reg32, new_shift_amt);
                    }
                }
            }else{ // shift_amt belong to [1, 31]
                cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(reg32, shift_amt - 1, shift_amt - 1);
                cpup->dp_op2 = armv4cpu_ror(reg32, shift_amt);
            }*/
            break;
        default:
            assert(0);
    }

}

// always_inline
inline void
armv4cpu_inst_dp_calc_op2_op2imm(armv4cpu_md_t* cpup){
    uint8_t rotate_imm = bits_span_drop_to_floor_u32(cpup->this_inst, 11, 8);   // [0, 15]
    uint32_t imm32 = bits_span_drop_to_floor_u32(cpup->this_inst, 7, 0);

    if(rotate_imm == 0){
        cpup->dp_next_carry_out_flag = cpup->inst_enter_cpsr_carryout_flag_ro;
        cpup->dp_op2 = imm32;
    }else{ // rotate_imm belong to [1, 15]
        cpup->dp_op2 = armv4cpu_ror(imm32, rotate_imm + rotate_imm); // n belong to [2, 30]
        cpup->dp_next_carry_out_flag = bits_span_drop_to_floor_u32(cpup->dp_op2, 31, 31);
    }
}

// 0 for logical operations and 1 for arithmetic operations
const uint8_t gl_armv4_dp_opcode_const_lookup_array_logical_or_arithmetic[16] = {
    0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0
};

// always_inline
inline uint8_t
armv4cpu_inst_sub_unsigned_is_overflow(uint32_t op1, uint32_t op2){
    uint32_t res;
    return !!__builtin_sub_overflow(op1, op2, &res);
}

// always_inline
inline uint8_t
armv4cpu_inst_add_unsigned_is_overflow(uint32_t op1, uint32_t op2){
    uint32_t res;
    return !!__builtin_add_overflow(op1, op2, &res);
}

// always_inline
inline uint8_t
armv4cpu_inst_adc_unsigned_is_overflow(uint32_t op1, uint32_t op2, uint32_t c){
    uint32_t res0, res1;
    if(__builtin_add_overflow(op1, op2, &res0)){
        return 1;
    }else{
        return !!__builtin_add_overflow(res0, c, &res1);
    }
}

// always_inline
inline uint8_t
armv4cpu_inst_sbc_unsigned_is_overflow(uint32_t op1, uint32_t op2, uint32_t c){
    uint32_t res0, res1;
    c = !c;
    if(__builtin_sub_overflow(op1, op2, &res0)){
        return 1;
    }else{
        return !!__builtin_sub_overflow(res0, c, &res1);
    }
}

// always_inline
inline uint8_t
armv4cpu_inst_sub_signed_is_overflow(uint32_t op1, uint32_t op2){
    int32_t res, iop1, iop2;
    iop1 = *((int32_t*)(&op1)); // `int32 x = uint32 y` is implementation defined
    iop2 = *((int32_t*)(&op2)); // whereas `x = *((int32*)&y)` is deterministic behaviour
    return !!__builtin_sub_overflow(iop1, iop2, &res);
}

// always_inline
inline uint8_t
armv4cpu_inst_add_signed_is_overflow(uint32_t op1, uint32_t op2){
    int32_t res, iop1, iop2;
    iop1 = *((int32_t*)(&op1));
    iop2 = *((int32_t*)(&op2));
    return !!__builtin_add_overflow(iop1, iop2, &res);
}

// always_inline
inline uint8_t
armv4cpu_inst_adc_signed_is_overflow(uint32_t op1, uint32_t op2, uint32_t c){
    int32_t res0, res1, iop1, iop2, ic;
    iop1 = *((int32_t*)(&op1));
    iop2 = *((int32_t*)(&op2));
    ic   = *((int32_t*)(&c));
    if(__builtin_add_overflow(iop1, iop2, &res0)){
        return 1;
    }else{
        return !!__builtin_add_overflow(res0, ic, &res1);
    }
}

// always_inline
inline uint8_t
armv4cpu_inst_sbc_signed_is_overflow(uint32_t op1, uint32_t op2, uint32_t c){
    int32_t res0, res1, iop1, iop2, ic;
    c = !c;
    iop1 = *((int32_t*)(&op1));
    iop2 = *((int32_t*)(&op2));
    ic   = *((int32_t*)(&c));
    if(__builtin_sub_overflow(iop1, iop2, &res0)){
        return 1;
    }else{
        return !!__builtin_sub_overflow(res0, ic, &res1);
    }
}

// always_inline
inline void
armv4cpu_inst_dp_exec(armv4cpu_md_t* cpup){
    uint32_t opcode = bits_span_drop_to_floor_u32(cpup->this_inst, 24, 21);
    uint8_t op1_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 19, 16);
    uint8_t rd_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12);
    uint32_t op1 = get_R(cpup, cpup->inst_enter_cpumodn_ro, op1_regidx); // Rn
    uint32_t op2 = cpup->dp_op2;
    uint32_t result = 0;
    cpup->dp_do_not_write_result_to_rd_flag = 0;
    switch(opcode){
        case 0:  // AND : logical
            result = op1 & op2;
            break;
        case 1:  // EOR : logical
            result = op1 ^ op2;
            break;
        case 2:  // SUB : arithmetic
            result = op1 - op2;
            cpup->dp_next_carry_out_flag = !armv4cpu_inst_sub_unsigned_is_overflow(op1, op2);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_sub_signed_is_overflow(op1, op2);
            break;
        case 3:  // RSB : arithmetic
            result = op2 - op1;
            cpup->dp_next_carry_out_flag = !armv4cpu_inst_sub_unsigned_is_overflow(op2, op1);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_sub_signed_is_overflow(op2, op1);
            break;
        case 4:  // ADD : arithmetic
            result = op1 + op2;
            cpup->dp_next_carry_out_flag = armv4cpu_inst_add_unsigned_is_overflow(op1, op2);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_add_signed_is_overflow(op1, op2);
            break;
        case 5:  // ADC : arithmetic
            result = op1 + op2 + cpup->inst_enter_cpsr_carryout_flag_ro;
            cpup->dp_next_carry_out_flag = armv4cpu_inst_adc_unsigned_is_overflow(
                op1, op2, cpup->inst_enter_cpsr_carryout_flag_ro);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_adc_signed_is_overflow(
                op1, op2, cpup->inst_enter_cpsr_carryout_flag_ro);
            break;
        case 6:  // SBC : arithmetic
            result = op1 - op2 - (!cpup->inst_enter_cpsr_carryout_flag_ro);
            cpup->dp_next_carry_out_flag = !armv4cpu_inst_sbc_unsigned_is_overflow(
                op1, op2, cpup->inst_enter_cpsr_carryout_flag_ro);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_sbc_signed_is_overflow(
                op1, op2, cpup->inst_enter_cpsr_carryout_flag_ro);
            break;
        case 7:  // RSC : arithmetic
            result = op2 - op1 - (!cpup->inst_enter_cpsr_carryout_flag_ro);
            cpup->dp_next_carry_out_flag = !armv4cpu_inst_sbc_unsigned_is_overflow(
                op2, op1, cpup->inst_enter_cpsr_carryout_flag_ro);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_sbc_signed_is_overflow(
                op2, op1, cpup->inst_enter_cpsr_carryout_flag_ro);
            break;
        case 8:  // TST : logical
            result = op1 & op2;
            cpup->dp_do_not_write_result_to_rd_flag = 1;
            break;
        case 9:  // TEQ : logical
            result = op1 ^ op2;
            cpup->dp_do_not_write_result_to_rd_flag = 1;
            break;
        case 10: // CMP : arithmetic
            result = op1 - op2;
            cpup->dp_next_carry_out_flag = !armv4cpu_inst_sub_unsigned_is_overflow(op1, op2);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_sub_signed_is_overflow(op1, op2);
            cpup->dp_do_not_write_result_to_rd_flag = 1;
            break;
        case 11: // CMN : arithmetic
            result = op1 + op2;
            cpup->dp_next_carry_out_flag = armv4cpu_inst_add_unsigned_is_overflow(op1, op2);
            cpup->dp_next_v_overflow_flag = armv4cpu_inst_add_signed_is_overflow(op1, op2);
            cpup->dp_do_not_write_result_to_rd_flag = 1;
            break;
        case 12: // ORR : logical
            result = op1 | op2;
            break;
        case 13: // MOV : logical
            result = op2;
            break;
        case 14: // BIC : logical
            result = op1 & (~op2);
            break;
        case 15: // MVN : logical
            result =  ~op2;
            break;
        default:
            assert(0);
    }

    if(result == 0){
        cpup->dp_next_zero_flag = 1;
    }
    cpup->dp_next_negative_flag = bits_span_drop_to_floor_u32(result, 31, 31);

    if(gl_armv4_dp_opcode_const_lookup_array_logical_or_arithmetic[opcode]){ // arithmetic

    }else{ // logical
        cpup->dp_next_v_overflow_flag = cpup->inst_enter_cpsr_v_overflow_flag_ro;
        // cpup->dp_next_carry_out_flag already set in the armv4cpu_inst_dp_calc_op2_* function call
    }

    if(cpup->dp_do_not_write_result_to_rd_flag){

    }else{
        set_R(cpup, cpup->inst_enter_cpumodn_ro, rd_regidx, result);
    }
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 20, 20)){ // S bit: set condition code
        if_unlikely(rd_regidx == REGIDX_PC){
            if_unlikely(cpup->inst_enter_cpumodn_ro == CPUMODEN_USR ||
                cpup->inst_enter_cpumodn_ro == CPUMODEN_SYS){

                // unpredictable behaviour, do what you like ;-p
                // we just leave it here now
                return;
            }else{
                set_cpsr(cpup, get_spsr(cpup, cpup->inst_enter_cpumodn_ro));
                return;
            }
            return;
        }else{
            armv4cpu_update_cpsr_NZCV_part(cpup,
                cpup->dp_next_negative_flag, cpup->dp_next_zero_flag,
                cpup->dp_next_carry_out_flag, cpup->dp_next_v_overflow_flag);
        }
    }else{
        if_unlikely(rd_regidx == REGIDX_PC){
            return;
        }
    }
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
}

// always_inline
inline void
armv4cpu_inst_raise_exception(
    armv4cpu_md_t* cpup, uint8_t exception_cpumodn,
    uint32_t return_link_addr, uint32_t exception_vector_addr){

    if_unlikely(exception_cpumodn == CPUMODEN_USR || exception_cpumodn == CPUMODEN_SYS){
        assert(0);
    }

    set_R(cpup, exception_cpumodn, REGIDX_R14, return_link_addr);
    uint32_t cpsr = get_cpsr(cpup);
    set_spsr(cpup, exception_cpumodn, cpsr);
    cpsr = (cpsr >> 8) << 8;
    cpsr = cpsr + ((uint32_t)exception_cpumodn) + ((uint32_t)0x80);
    if(exception_cpumodn == CPUMODEN_FIQ){
        cpsr = cpsr + ((uint32_t)0x40);
    }
    set_cpsr(cpup, cpsr);
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, exception_vector_addr);
}

// always_inline
inline void
armv4cpu_inst_undefined_exec(armv4cpu_md_t* cpup){
    armv4cpu_inst_raise_exception(cpup,
        EXCEPTION_CPUMODEN_UND,
        cpup->inst_enter_real_PC_ro + 4,
        EXCEPTION_VECTOR_ADDR_UND);
}

// always_inline
inline void
armv4cpu_inst_swi_exec(armv4cpu_md_t* cpup){
    armv4cpu_inst_raise_exception(cpup,
        EXCEPTION_CPUMODEN_SVC,
        cpup->inst_enter_real_PC_ro + 4,
        EXCEPTION_VECTOR_ADDR_SWI);
}

// always_inline
inline void
armv4cpu_inst_bx_exec(armv4cpu_md_t* cpup){
    uint8_t rn_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0);
    uint32_t rn = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    if_unlikely(bits_span_drop_to_floor_u32(rn, 0, 0)){ // thumb
        // do not support thumb mode yet
        armv4cpu_inst_undefined_exec(cpup);
        return;
    }
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, rn);
}

// always_inline
inline void
armv4cpu_inst_b_bl_exec(armv4cpu_md_t* cpup){
    uint32_t offset = bits_span_drop_to_floor_u32(cpup->this_inst, 23, 0);
    uint8_t neg_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 23, 23);
    offset = offset << 2;
    if(neg_flag){
        offset = offset | (uint32_t)0xfc000000;
    }
    uint32_t pc = get_R(cpup, cpup->inst_enter_cpumodn_ro, REGIDX_PC) + 8;
    set_R(cpup, cpup->inst_enter_cpumodn_ro, REGIDX_R14, pc);
    set_R(cpup, cpup->inst_enter_cpumodn_ro, REGIDX_PC, pc + offset);
}

// always_inline
inline void
armv4cpu_inst_mul_mla_exec(armv4cpu_md_t* cpup){
    uint8_t acmul_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 21, 21);
    uint8_t rd_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 19, 16);
    uint32_t rn, rs, rm, rd;
    rm = get_R(cpup, cpup->inst_enter_cpumodn_ro,
        bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0));
    rs = get_R(cpup, cpup->inst_enter_cpumodn_ro,
        bits_span_drop_to_floor_u32(cpup->this_inst, 11, 8));
    if(acmul_flag){
        rn = get_R(cpup, cpup->inst_enter_cpumodn_ro,
            bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12));
        rd = rm * rs + rn;
    }else{
        rd = rm * rs;
    }
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 20, 20)){ // S
        cpup->next_negative_flag = bits_span_drop_to_floor_u32(rd, 31, 31);
        cpup->next_zero_flag = 0;
        if_unlikely(rd == 0){
            cpup->next_zero_flag = 1;
        }
        cpup->next_carry_out_flag = cpup->inst_enter_cpsr_carryout_flag_ro;
        cpup->next_v_overflow_flag = cpup->inst_enter_cpsr_v_overflow_flag_ro;
        armv4cpu_update_cpsr_NZCV_part(cpup,
            cpup->next_negative_flag, cpup->next_zero_flag,
            cpup->next_carry_out_flag, cpup->next_v_overflow_flag);
    }
    set_R(cpup, cpup->inst_enter_cpumodn_ro, rd_regidx, rd);
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
}

// SMULL UMULL SMLAL UMLAL
// always_inline
inline void
armv4cpu_inst_mull_mlal_exec(armv4cpu_md_t* cpup){
    uint8_t acmul_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 21, 21);
    uint8_t signed_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 22, 22);
    uint8_t rdhi_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 19, 16);
    uint8_t rdlo_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12);
    uint32_t rdhi, rdlo, rs, rm;
    rm = get_R(cpup, cpup->inst_enter_cpumodn_ro,
        bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0));
    rs = get_R(cpup, cpup->inst_enter_cpumodn_ro,
        bits_span_drop_to_floor_u32(cpup->this_inst, 11, 8));
    uint64_t u64;
    if(signed_flag == 0){ // unsigned
        if(acmul_flag){
            u64 = get_R(cpup, cpup->inst_enter_cpumodn_ro, rdhi_regidx);
            u64 = u64 << 32;
            u64 = get_R(cpup, cpup->inst_enter_cpumodn_ro, rdlo_regidx) + u64;
            u64 = ((uint64_t)rm) * ((uint64_t)rs) + u64;
        }else{
            u64 = ((uint64_t)rm) * ((uint64_t)rs);
        }
    }else{ // signed
        int64_t i64, i64rm, i64rs;
        if(acmul_flag){
            u64 = get_R(cpup, cpup->inst_enter_cpumodn_ro, rdhi_regidx);
            u64 = u64 << 32;
            u64 = get_R(cpup, cpup->inst_enter_cpumodn_ro, rdlo_regidx) + u64;
            i64 = *((int64_t*)&u64);
            u64 = (uint64_t)rm;
            i64rm = *((int64_t*)&u64);
            u64 = (uint64_t)rs;
            i64rs = *((int64_t*)&u64);
            i64 = i64rm * i64rs + i64;
            u64 = *((uint64_t*)&i64);
        }else{
            u64 = (uint64_t)rm;
            i64rm = *((int64_t*)&u64);
            u64 = (uint64_t)rs;
            i64rs = *((int64_t*)&u64);
            i64 = i64rm * i64rs + i64;
            u64 = *((uint64_t*)&i64);
        }
    }
    rdhi = (uint32_t)(u64 >> 32);
    rdlo = (uint32_t)(u64);
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 20, 20)){ // S
        cpup->next_negative_flag = bits_span_drop_to_floor_u32(rdhi, 31, 31);
        cpup->next_zero_flag = 0;
        if_unlikely(u64 == 0){
            cpup->next_zero_flag = 1;
        }
        cpup->next_carry_out_flag = cpup->inst_enter_cpsr_carryout_flag_ro;
        cpup->next_v_overflow_flag = cpup->inst_enter_cpsr_v_overflow_flag_ro;
        armv4cpu_update_cpsr_NZCV_part(cpup,
            cpup->next_negative_flag, cpup->next_zero_flag,
            cpup->next_carry_out_flag, cpup->next_v_overflow_flag);
    }
    set_R(cpup, cpup->inst_enter_cpumodn_ro, rdhi_regidx, rdhi);
    set_R(cpup, cpup->inst_enter_cpumodn_ro, rdlo_regidx, rdlo);
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
}

// always_inline
inline void
armv4cpu_inst_msr_mrs_exec(armv4cpu_md_t* cpup){
    uint8_t rd_regidx;
    uint32_t r, psr;
    uint8_t spsr_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 22, 22);
    if_unlikely(spsr_flag){
        psr = get_spsr(cpup, cpup->inst_enter_cpumodn_ro);
    }else{
        psr = get_cpsr(cpup);
    }
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 21, 21)){ // MSR - s <= r
        if(bits_span_drop_to_floor_u32(cpup->this_inst, 25, 25)){ // imm
            armv4cpu_inst_dp_calc_op2_op2imm(cpup);
            r = cpup->dp_op2;
        }else{
            r = get_R(cpup, cpup->inst_enter_cpumodn_ro,
                bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0));
        }
        if(bits_span_drop_to_floor_u32(cpup->this_inst, 16, 16)){ // whole psr
            psr = r;
        }else{ // psr flag bits only
            psr = bits_span_drop_to_floor_u32(psr, 27, 0);
            psr = psr | (bits_span_drop_to_floor_u32(r, 31, 28) << 28);
        }
        if(cpup->inst_enter_cpumodn_ro == CPUMODEN_USR){
            if(spsr_flag){
            }else{
                uint32_t cpsr = get_cpsr(cpup);
                cpsr = bits_span_drop_to_floor_u32(cpsr, 27, 0);
                cpsr = cpsr | (bits_span_drop_to_floor_u32(psr, 31, 28) << 28);
                set_cpsr(cpup, cpsr);
            }
        }else{
            if_unlikely(spsr_flag){
                set_spsr(cpup, cpup->inst_enter_cpumodn_ro, psr);
            }else{
                set_cpsr(cpup, psr);
            }
        }
    }else{ // MRS - r <= s
        rd_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12);
        set_R(cpup, cpup->inst_enter_cpumodn_ro, rd_regidx, psr);
    }

    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
}


// always_inline
inline uint32_t
armv4cpu_mmu_fetch_inst_4bytes(armv4cpu_md_t* cpup, uint32_t addr){

}

// always_inline
inline uint32_t
armv4cpu_mmu_data_access_read_4bytes(armv4cpu_md_t* cpup, uint32_t addr){

}

// always_inline
inline uint8_t
armv4cpu_mmu_data_access_read_1byte(armv4cpu_md_t* cpup, uint32_t addr){

}

// always_inline
inline void
armv4cpu_mmu_data_access_write_4bytes(armv4cpu_md_t* cpup, uint32_t addr, uint32_t u32){

}

// always_inline
inline void
armv4cpu_mmu_data_access_write_1byte(armv4cpu_md_t* cpup, uint32_t addr, uint8_t u8){

}

// SWP SWPB
// always_inline
inline void
armv4cpu_inst_swp_exec(armv4cpu_md_t* cpup){
    uint8_t rn_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 19, 16);
    uint32_t rn = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    uint8_t rd_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12);
    uint32_t rd = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    uint8_t rm_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 3, 0);
    uint32_t rm = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);

    if_unlikely(bits_span_drop_to_floor_u32(cpup->this_inst, 22, 22)){ // byte mode - SWPB
        uint8_t m8 = armv4cpu_mmu_data_access_read_1byte(cpup, rn);
        if_unlikely(cpup->mmu_data_access_need_abort_flag){
            goto DATA_ACCESS_ABORT;
        }
        armv4cpu_mmu_data_access_write_1byte(cpup, rn, (uint8_t)rm);
        if_unlikely(cpup->mmu_data_access_need_abort_flag){
            goto DATA_ACCESS_ABORT;
        }
        rd = (uint32_t)m8;
        return;
    }else{ // word mode - SWP
        uint32_t m = armv4cpu_mmu_data_access_read_4bytes(cpup, rn);
        if_unlikely(cpup->mmu_data_access_need_abort_flag){
            goto DATA_ACCESS_ABORT;
        }
        armv4cpu_mmu_data_access_write_4bytes(cpup, rn, rm);
        if_unlikely(cpup->mmu_data_access_need_abort_flag){
            goto DATA_ACCESS_ABORT;
        }
        rd = m;
    }
    set_R(cpup, cpup->inst_enter_cpumodn_ro, rd_regidx, rd);
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
    return;

    DATA_ACCESS_ABORT:;
    armv4cpu_inst_raise_exception(cpup,
        EXCEPTION_CPUMODEN_ABT, cpup->inst_enter_real_PC_ro + 4, EXCEPTION_VECTOR_ADDR_DATA_ABT);
    return;
}

// LDR LDRT LDRB LDRBT STR STRT STRB STRBT
// always_inline
inline void
armv4cpu_inst_ldr_str_exec(armv4cpu_md_t* cpup){
    uint8_t rn_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 19, 16);
    uint32_t rn = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    uint8_t rd_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12);
    uint32_t rd = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    uint8_t write_back_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 21, 21);
    uint8_t byte_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 22, 22);
    uint8_t add_offset_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 23, 23);
    uint8_t pre_calc_offset_flag =
        bits_span_drop_to_floor_u32(cpup->this_inst, 24, 24);
    uint32_t offset, new_rn, addr;
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 25, 25)){ // Rm + imm_shift
        armv4cpu_inst_dp_calc_op2_op2reg_immshift(cpup);
        offset = cpup->dp_op2;
    }else{
        offset = bits_span_drop_to_floor_u32(cpup->this_inst, 11, 0);
    }
    if(add_offset_flag){
        new_rn = rn + offset;
    }else{
        new_rn = rn - offset;
    }
    if(pre_calc_offset_flag){
        addr = new_rn;
        if(write_back_flag){
            cpup->mmu_inst_is_ldrxt_strxt_flag = 1;
        }
    }else{
        addr = rd;
    }
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 20, 20)){ // LDR
        if_unlikely(byte_flag){
            rd = (uint32_t)(armv4cpu_mmu_data_access_read_1byte(cpup, addr));
        }else{
            rd = armv4cpu_mmu_data_access_read_4bytes(cpup, addr);
        }
    }else{ // STR
        if_unlikely(byte_flag){
            armv4cpu_mmu_data_access_write_1byte(cpup, addr, (uint8_t)rd);
        }else{
            armv4cpu_mmu_data_access_write_4bytes(cpup, addr, rd);
        }
    }
    if_unlikely(cpup->mmu_data_access_need_abort_flag){
        goto DATA_ACCESS_ABORT;
    }
    if(pre_calc_offset_flag){
        if(write_back_flag){
        }else{
        }
    }else{
        if(write_back_flag){
            rn = new_rn;
            set_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx, new_rn);
        }else{
        }
    }
    set_R(cpup, cpup->inst_enter_cpumodn_ro, rd_regidx, rd);
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
    return;

    DATA_ACCESS_ABORT:;
    armv4cpu_inst_raise_exception(cpup,
        EXCEPTION_CPUMODEN_ABT, cpup->inst_enter_real_PC_ro + 4, EXCEPTION_VECTOR_ADDR_DATA_ABT);
    return;
}

// STRH LDRH LDRSB LDRSH
// always_inline
inline void
armv4cpu_inst_extra_ldr_str_exec(armv4cpu_md_t* cpup){
    uint8_t rn_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 19, 16);
    uint32_t rn = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    uint8_t rd_regidx = bits_span_drop_to_floor_u32(cpup->this_inst, 15, 12);
    uint32_t rd = get_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx);
    uint8_t write_back_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 21, 21);
    uint8_t byte_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 22, 22);
    uint8_t add_offset_flag = bits_span_drop_to_floor_u32(cpup->this_inst, 23, 23);
    uint8_t pre_calc_offset_flag =
        bits_span_drop_to_floor_u32(cpup->this_inst, 24, 24);
    uint32_t offset, new_rn, addr;
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 25, 25)){ // Rm + imm_shift
        armv4cpu_inst_dp_calc_op2_op2reg_immshift(cpup);
        offset = cpup->dp_op2;
    }else{
        offset = bits_span_drop_to_floor_u32(cpup->this_inst, 11, 0);
    }
    if(add_offset_flag){
        new_rn = rn + offset;
    }else{
        new_rn = rn - offset;
    }
    if(pre_calc_offset_flag){
        addr = new_rn;
        if(write_back_flag){
            cpup->mmu_inst_is_ldrxt_strxt_flag = 1;
        }
    }else{
        addr = rd;
    }
    if(bits_span_drop_to_floor_u32(cpup->this_inst, 20, 20)){ // LDR
        if_unlikely(byte_flag){
            rd = (uint32_t)(armv4cpu_mmu_data_access_read_1byte(cpup, addr));
        }else{
            rd = armv4cpu_mmu_data_access_read_4bytes(cpup, addr);
        }
    }else{ // STR
        if_unlikely(byte_flag){
            armv4cpu_mmu_data_access_write_1byte(cpup, addr, (uint8_t)rd);
        }else{
            armv4cpu_mmu_data_access_write_4bytes(cpup, addr, rd);
        }
    }
    if_unlikely(cpup->mmu_data_access_need_abort_flag){
        goto DATA_ACCESS_ABORT;
    }
    if(pre_calc_offset_flag){
        if(write_back_flag){
        }else{
        }
    }else{
        if(write_back_flag){
            rn = new_rn;
            set_R(cpup, cpup->inst_enter_cpumodn_ro, rn_regidx, new_rn);
        }else{
        }
    }
    set_R(cpup, cpup->inst_enter_cpumodn_ro, rd_regidx, rd);
    set_PC(cpup, cpup->inst_enter_cpumodn_ro, cpup->inst_enter_real_PC_ro + 4);
    return;

    DATA_ACCESS_ABORT:;
    armv4cpu_inst_raise_exception(cpup,
        EXCEPTION_CPUMODEN_ABT, cpup->inst_enter_real_PC_ro + 4, EXCEPTION_VECTOR_ADDR_DATA_ABT);
    return;
}

/*
armv4cpu_execute(inst_amount_limit) -> inst_amount_executed
    // data-access-mem-abort that instructon would be totally atomic in any case,
    // for example data abort occur in inst[R0-R15 <-load-or-store-> mem-span]
    //    such inst would be totally atomic operation

    loop{
        inst = inst_fetch()
        handle_exception_if_inst_fetch_mem_abort()
        // inst decode and execute start
        if_unlikely(inst cond_field is 0b1111){ // ARM DDI 0100I: Page A3-4 Line 2
            goto UNPREDICTABLE_INST_HANDLE;
        }
        if(!armv4cpu_inst_cond_test_is_ok()){
            armv4cpu_inst_nop(cpup, inst);
            goto POST_INST_HANDLE;
        }
        switch(inst[27:25]){ // ARM DDI 0100I: Page A3-2 Figure A3-1
            case 0b'000:
                if(inst[4] == 0'b0){
                    if_unlikely(inst[24:23] == 0'b10 && inst[20] == 0'b0){
                        goto MISCELLANEOUS_INSTRUCTIONS_HANDLE_ROW2 // Row 2 in Figure A3-1 of A3-2
                    }else{
                        goto DATA_PROCESSING_IMM_SHIFT_HANDLE_ROW1; // Row 1
                    }
                }else{
                    if_likely(inst[7] == 0){
                        if_unlikely(inst[24:23] == 0'b10 && inst[20] == 0'b0){
                            goto MISCELLANEOUS_INSTRUCTIONS_HANDLE_ROW4 // Row 4
                        }else{
                            goto DATA_PROCESSING_REG_SHIFT_HANDLE_ROW3 // Row 3
                        }
                    }else{
                        goto MUTIPLIES_AND_EXTRA_LOAD_STORE_HANDLE_ROW5 // Row 5
                    }
                }
                break;
            case 0b'001:
                if_unlikely(inst[24:23] == 0'b10 && inst[20] == 0'b0){
                    if_likely(inst[21] == 1){
                        goto MOV_IMM_TO_STATUS_REG_HANDLE_ROW8 // Row 8
                    }else{
                        goto UNDEF_HANDLE_ROW7 // Row 7
                    }
                }else{
                    goto DATA_PROCESSING_IMM_HANDLE_ROW6 // Row 6
                }
                break;
            case 0b'010:
                goto LOAD_STORE_IMM_OFFSET_HANDLE_ROW9 // Row 9
                break;
            case 0b'011:
                if_likely(inst[4] == 0){
                    goto LOAD_STORE_REG_OFFSET_HANDLE_ROW10 // Row 10
                }else{
                    if(inst[24:20] == 0b11111 && inst[7:5] == 0b111){
                        goto UNDEF_ARCHITECHTURALLY_HANDLE_ROW12 // Row 12
                    }else{
                        goto UNDEF_HANDLE_ROW11 // Row 11
                    }
                }
                break;
            case 0b'100:
                goto LOAD_STORE_MULTI_HANDLE_ROW13 // Row 13
                break;
            case 0b'101:
                goto BRANCH_AND_BRANCH_WITH_LINK_HANDLE_ROW14 // Row 14
                break;
            case 0b'110:
                goto COPROCESSOR_LOAD_STORE_AND_DOUBLE_REG_TRANSFER_HANDLE_ROW15 // Row 15
                break;
            case 0b'111:
                if_likely(inst[24] == 0b1){
                    goto SWI_HANDLE_ROW18 // Row 18
                }else{
                    if(inst[4] == 0b0){
                        goto COPROCESSOR_DATA_PROCESSING_HANDLE_ROW16 // Row 16
                    }else{
                        goto COPROCESSOR_REG_TRANSFER_HANDLE_ROW17 // Row 17
                    }
                }
                break;
            default: abort()
        }
        DATA_PROCESSING_IMM_SHIFT_HANDLE_ROW1:;
        DATA_PROCESSING_REG_SHIFT_HANDLE_ROW3:;
        DATA_PROCESSING_IMM_HANDLE_ROW6:;
        BRANCH_AND_BRANCH_WITH_LINK_HANDLE_ROW14:;
        MUTIPLIES_AND_EXTRA_LOAD_STORE_HANDLE_ROW5:;
        LOAD_STORE_IMM_OFFSET_HANDLE_ROW9:;
        LOAD_STORE_REG_OFFSET_HANDLE_ROW10:;
        LOAD_STORE_MULTI_HANDLE_ROW13:;
        SWI_HANDLE_ROW18:;
        MISCELLANEOUS_INSTRUCTIONS_HANDLE_ROW2:;
        MISCELLANEOUS_INSTRUCTIONS_HANDLE_ROW4:;
        MOV_IMM_TO_STATUS_REG_HANDLE_ROW8:;
        COPROCESSOR_LOAD_STORE_AND_DOUBLE_REG_TRANSFER_HANDLE_ROW15:;
        COPROCESSOR_DATA_PROCESSING_HANDLE_ROW16:;
        COPROCESSOR_REG_TRANSFER_HANDLE_ROW17:;
        UNPREDICTABLE_INST_HANDLE:;
        UNDEF_ARCHITECHTURALLY_HANDLE_ROW12:;
        UNDEF_HANDLE_ROW11:;
        UNDEF_HANDLE_ROW7:;

        POST_INST_HANDLE:
        cpup->inst_executed_amount_total++;
        cpup->inst_executed_amount_in_one_execute++;
        if_unlikely(cpu->inst_executed_ct_in_this_execute >= cpup->inst_ct_limit_in_this_execute){
            break; // return from this func call
        }
    }

*/
