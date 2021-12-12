#pragma once
/*#
    # z80.h

    A cycle-stepped Z80 emulator in a C header.

    Do this:
    ~~~~C
    #define CHIPS_IMPL
    ~~~~
    before you include this file in *one* C or C++ file to create the 
    implementation.

    Optionally provide
    ~~~C
    #define CHIPS_ASSERT(x) your_own_asset_macro(x)
    ~~~

    ## Emulated Pins
    ***********************************
    *           +-----------+         *
    * M1    <---|           |---> A0  *
    * MREQ  <---|           |---> A1  *
    * IORQ  <---|           |---> A2  *
    * RD    <---|           |---> ..  *
    * WR    <---|    Z80    |---> A15 *
    * HALT  <---|           |         *
    * WAIT  --->|           |<--> D0  *
    * INT   --->|           |<--> D1  *
    * RFSH  <---|           |<--> ... *
    *           |           |<--> D7  *
    *           +-----------+         *
    ***********************************    

    ## Functions

    ~~~C
    uint64_t z80_init(z80_t* cpu);
    ~~~
        Initializes a new z80_t instance, returns initial pin mask to start
        execution at address 0.

    ~~~C
    uint64_t z80_reset(z80_t* cpu)
    ~~~
        Resets a z80_t instance, returns pin mask to start execution at 
        address 0.

    ~~~C
    uint64_t z80_tick(z80_t* cpu, uint64_t pins)
    ~~~
        Step the z80_t instance for one clock cycle. 

    ~~~C
    uint64_t z80_prefetch(z80_t* cpu, uint16_t new_pc)
    ~~~
        Call this function to force execution to start at a specific
        PC. Use the returned pin mask as argument into the next z80_tick() call.

    ~~~C
    bool z80_opdone(z80_t* cpu)
    ~~~
        Helper function to detect whether the z80_t instance has completed
        an instruction.

    ## HOWTO

    Initialize a new z80_t instance and start ticking it:
    ~~~C
        z80_t cpu;
        uint64_t pins = z80_init(&cpu);
        while (!done) {
            pins = z80_tick(&cpu, pins);
        }
    ~~~
    Since there is no memory attached yet, the CPU will simply run whatever opcode
    bytes are present on the data bus (in this case the data bus is zero, so the CPU
    just runs throught the same NOP over and over).

    Next, add some memory and inspect and modify the pin mask to handle memory accesses:
    ~~~C
        uint8_t mem[(1<<16)] = {0};
        z80_t cpu;
        uint64_t pins = z80_init(&cpu);
        while (!done) {
            pins = z80_tick(&cpu, pins);
            if (pins & Z80_MREQ) {
                const uint16_t addr = Z80_GET_ADDR(pins);
                if (pins & Z80_RD) {
                    uint8_t data = mem[addr];
                    Z80_SET_DATA(pins, data);
                }
                else if (pins & Z80_WR) {
                    uint8_t data = Z80_GET_DATA(pins);
                    mem[addr] = data;
                }
            }
        }
    ~~~
    The CPU will now run through the whole address space executing NOPs (because the memory is 
    filled with 0s instead of a valid program). If there would be a valid Z80 program at memory
    address 0, this would be executed instead.

    IO requests are handled the same as memory requests, but instead of the MREQ pin, the 
    IORQ pin must be checked:
    ~~~C
        uint8_t mem[(1<<16)] = {0};
        z80_t cpu;
        uint64_t pins = z80_init(&cpu);
        while (!done) {
            pins = z80_tick(&cpu, pins);
            if (pins & Z80_MREQ) {
                const uint16_t addr = Z80_GET_ADDR(pins);
                if (pins & Z80_RD) {
                    uint8_t data = mem[addr];
                    Z80_SET_DATA(pins, data);
                }
                else if (pins & Z80_WR) {
                    uint8_t data = Z80_GET_DATA(pins);
                    mem[addr] = data;
                }
            }
            else if (pins & Z80_IORQ) {
                const uint16_t port = Z80_GET_ADDR(pins);
                if (pins & Z80_RD) {
                    // handle IO input request at port
                    ...
                }
                else if (pins & Z80_RD) {
                    // handle IO output request at port
                    ...
                }
            }
        }
    ~~~

    Handle interrupt acknowledge cycles by checking for Z80_IORQ|Z80_M1:
    ~~~C
        uint8_t mem[(1<<16)] = {0};
        z80_t cpu;
        uint64_t pins = z80_init(&cpu);
        while (!done) {
            pins = z80_tick(&cpu, pins);
            if (pins & Z80_MREQ) {
                const uint16_t addr = Z80_GET_ADDR(pins);
                if (pins & Z80_RD) {
                    uint8_t data = mem[addr];
                    Z80_SET_DATA(pins, data);
                }
                else if (pins & Z80_WR) {
                    uint8_t data = Z80_GET_DATA(pins);
                    mem[addr] = data;
                }
            }
            else if (pins & Z80_IORQ) {
                const uint16_t addr = Z80_GET_ADDR(pins);
                if (pins & Z80_M1) {
                    // an interrupt acknowledge cycle, depending on the emulated system,
                    // put either an instruction byte, or an interrupt vector on the data bus
                    Z80_SET_DATA(pins, opcode_or_intvec);
                }
                else if (pins & Z80_RD) {
                    // handle IO input request at port `addr`
                    ...
                }
                else if (pins & Z80_RD) {
                    // handle IO output request at port `addr`
                    ...
                }
            }
        }
    ~~~

    To request an interrupt, or inject a wait state just set the respective pin
    (Z80_INT, Z80_NMI, Z80_WAIT), don't forget to clear the pin again later (the
    details on when those pins are set and cleared depend heavily on the
    emulated system).

    !!! note
        NOTE: The Z80_RES pin is currently not emulated. Instead call the `z80_reset()` function.

    To emulate a whole computer system, add the per-tick code for the rest of the system to the
    basic ticking code above.

    If the emulated system uses the Z80 daisychain interrupt protocol (for instance when using
    the Z80 family chips like the PIO or CTC), tick those chips in interrupt priority order and
    set the Z80_IEIO pin before the highest priority chip in the daisychain is ticked:

    ~~~C
        ...
        while (!done) {
            pins = z80_tick(&cpu, pins);
            ...
            // tick Z80 family chips in 'daisychain order':
            pins |= Z80_IEIO;
            ...
            pins = z80ctc_tick(&ctc, pins);
            ...
            pins = z80pio_tick(&pio, pins);
            ...
            // the Z80_INT pin will now be set if any of the chips wants to issue an interrupt request
        }
    ~~~
#*/
/*
    zlib/libpng license

    Copyright (c) 2021 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution. 
*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// address pins
#define Z80_A0  (1ULL<<0)
#define Z80_A1  (1ULL<<1)
#define Z80_A2  (1ULL<<2)
#define Z80_A3  (1ULL<<3)
#define Z80_A4  (1ULL<<4)
#define Z80_A5  (1ULL<<5)
#define Z80_A6  (1ULL<<6)
#define Z80_A7  (1ULL<<7)
#define Z80_A8  (1ULL<<8)
#define Z80_A9  (1ULL<<9)
#define Z80_A10 (1ULL<<10)
#define Z80_A11 (1ULL<<11)
#define Z80_A12 (1ULL<<12)
#define Z80_A13 (1ULL<<13)
#define Z80_A14 (1ULL<<14)
#define Z80_A15 (1ULL<<15)

// data pins
#define Z80_D0  (1ULL<<16)
#define Z80_D1  (1ULL<<17)
#define Z80_D2  (1ULL<<18)
#define Z80_D3  (1ULL<<19)
#define Z80_D4  (1ULL<<20)
#define Z80_D5  (1ULL<<21)
#define Z80_D6  (1ULL<<22)
#define Z80_D7  (1ULL<<23)

// control pins
#define Z80_M1    (1ULL<<24)        // machine cycle 1
#define Z80_MREQ  (1ULL<<25)        // memory request
#define Z80_IORQ  (1ULL<<26)        // input/output request
#define Z80_RD    (1ULL<<27)        // read
#define Z80_WR    (1ULL<<28)        // write
#define Z80_HALT  (1ULL<<29)        // halt state
#define Z80_INT   (1ULL<<30)        // interrupt request
#define Z80_RES   (1ULL<<31)        // reset requested
#define Z80_NMI   (1ULL<<32)        // non-maskable interrupt
#define Z80_WAIT  (1ULL<<33)        // wait requested
#define Z80_RFSH  (1ULL<<34)        // refresh

// virtual pins (for interrupt daisy chain protocol)
#define Z80_IEIO    (1ULL<<37)      // unified daisy chain 'Interrupt Enable In+Out'
#define Z80_RETI    (1ULL<<38)      // cpu has decoded a RETI instruction

#define Z80_CTRL_PIN_MASK (Z80_M1|Z80_MREQ|Z80_IORQ|Z80_RD|Z80_WR|Z80_RFSH)
#define Z80_PIN_MASK ((1ULL<<40)-1)

// pin access helper macros
#define Z80_MAKE_PINS(ctrl, addr, data) ((ctrl)|((data&0xFF)<<16)|((addr)&0xFFFFULL))
#define Z80_GET_ADDR(p) ((uint16_t)(p))
#define Z80_SET_ADDR(p,a) {p=((p)&~0xFFFF)|((a)&0xFFFF);}
#define Z80_GET_DATA(p) ((uint8_t)((p)>>16))
#define Z80_SET_DATA(p,d) {p=((p)&~0xFF0000ULL)|(((d)<<16)&0xFF0000ULL);}

// status flags
#define Z80_CF (1<<0)           // carry
#define Z80_NF (1<<1)           // add/subtract
#define Z80_VF (1<<2)           // parity/overflow
#define Z80_PF Z80_VF
#define Z80_XF (1<<3)           // undocumented bit 3
#define Z80_HF (1<<4)           // half carry
#define Z80_YF (1<<5)           // undocumented bit 5
#define Z80_ZF (1<<6)           // zero
#define Z80_SF (1<<7)           // sign

typedef struct {
    uint16_t step;          // first or current decoder switch-case branch step
    uint16_t alt_step;      // alternative first decoder step for prefixed-indirect instructions
} z80_opstate_t;

// CPU state
typedef struct {
    uint16_t step;      // the currently active decoder step
    uint16_t addr;      // effective address for (HL),(IX+d),(IY+d)
    uint8_t dlatch;     // temporary store for data bus value
    uint8_t opcode;     // current opcode
    uint64_t pins;      // last pin state, used for NMI detection
    uint64_t int_bits;  // track INT and NMI state
    union {
        struct {
            uint8_t hlx_idx;        // index into hlx[] for mapping hl to ix or iy (0: hl, 1: ix, 2: iy)
            uint8_t prefix;         // one of _Z80_PREFIX_*
        };
        uint16_t prefix_state;
    };
    union { struct { uint8_t pcl; uint8_t pch; }; uint16_t pc; };

    // NOTE: These unions are fine in C, but not C++.
    union { struct { uint8_t f; uint8_t a; }; uint16_t af; };
    union { struct { uint8_t c; uint8_t b; }; uint16_t bc; };
    union { struct { uint8_t e; uint8_t d; }; uint16_t de; };
    union {
        struct {
            union { struct { uint8_t l; uint8_t h; }; uint16_t hl; };
            union { struct { uint8_t ixl; uint8_t ixh; }; uint16_t ix; };
            union { struct { uint8_t iyl; uint8_t iyh; }; uint16_t iy; };
        };
        struct { union { struct { uint8_t l; uint8_t h; }; uint16_t hl; }; } hlx[3];
    };
    union { struct { uint8_t wzl; uint8_t wzh; }; uint16_t wz; };
    union { struct { uint8_t spl; uint8_t sph; }; uint16_t sp; };
    union { struct { uint8_t r; uint8_t i; }; uint16_t ir; };
    uint16_t af2, bc2, de2, hl2; // shadow register bank
    uint8_t im;
    bool iff1, iff2;
} z80_t;

// initialize a new Z80 instance and return initial pin mask
uint64_t z80_init(z80_t* cpu);
// immediately put Z80 into reset state
uint64_t z80_reset(z80_t* cpu);
// execute one tick, return new pin mask
uint64_t z80_tick(z80_t* cpu, uint64_t pins);
// force execution to continue at address 'new_pc'
uint64_t z80_prefetch(z80_t* cpu, uint16_t new_pc);
// return true when full instruction has finished
bool z80_opdone(z80_t* cpu);

#ifdef __cplusplus
} // extern C
#endif

//-- IMPLEMENTATION ------------------------------------------------------------
#ifdef CHIPS_IMPL
#include <string.h> // memset
#ifndef CHIPS_ASSERT
#include <assert.h>
#define CHIPS_ASSERT(c) assert(c)
#endif

#if defined(__GNUC__)
#define _Z80_UNREACHABLE __builtin_unreachable()
#elif defined(_MSC_VER)
#define _Z80_UNREACHABLE __assume(0)
#else
#define _Z80_UNREACHABLE
#endif

// values for hlx_idx for mapping HL, IX or IY, used as index into hlx[]
#define _Z80_MAP_HL (0)
#define _Z80_MAP_IX (1)
#define _Z80_MAP_IY (2)

// currently active prefix
#define _Z80_PREFIX_NONE (0)
#define _Z80_PREFIX_CB   (1<<0)
#define _Z80_PREFIX_DD   (1<<1)
#define _Z80_PREFIX_ED   (1<<2)
#define _Z80_PREFIX_FD   (1<<3)

uint64_t z80_init(z80_t* cpu) {
    CHIPS_ASSERT(cpu);
    // initial state as described in 'The Undocumented Z80 Documented'
    memset(cpu, 0, sizeof(z80_t));
    cpu->af = cpu->bc = cpu->de = cpu->hl = 0xFFFF;
    cpu->wz = cpu->sp = cpu->ix = cpu->iy = 0xFFFF;
    cpu->af2 = cpu->bc2 = cpu->de2 = cpu->hl2 = 0xFFFF;
    return z80_prefetch(cpu, 0x0000);
}

uint64_t z80_reset(z80_t* cpu) {
    // reset state as described in 'The Undocumented Z80 Documented'
    memset(cpu, 0, sizeof(z80_t));
    cpu->af = cpu->bc = cpu->de = cpu->hl = 0xFFFF;
    cpu->wz = cpu->sp = cpu->ix = cpu->iy = 0xFFFF;
    cpu->af2 = cpu->bc2 = cpu->de2 = cpu->hl2 = 0xFFFF;
    return z80_prefetch(cpu, 0x0000);
}

bool z80_opdone(z80_t* cpu) {
    // because of the overlapped cycle, the result of the previous
    // instruction is only available in M1/T2
    return ((cpu->pins & (Z80_M1|Z80_RD)) == (Z80_M1|Z80_RD)) && (cpu->prefix == 0);
}

static inline uint64_t _z80_halt(z80_t* cpu, uint64_t pins) {
    cpu->pc--;
    return pins | Z80_HALT;
}

// sign+zero+parity lookup table
static const uint8_t _z80_szp_flags[256] = {
  0x44,0x00,0x00,0x04,0x00,0x04,0x04,0x00,0x08,0x0c,0x0c,0x08,0x0c,0x08,0x08,0x0c,
  0x00,0x04,0x04,0x00,0x04,0x00,0x00,0x04,0x0c,0x08,0x08,0x0c,0x08,0x0c,0x0c,0x08,
  0x20,0x24,0x24,0x20,0x24,0x20,0x20,0x24,0x2c,0x28,0x28,0x2c,0x28,0x2c,0x2c,0x28,
  0x24,0x20,0x20,0x24,0x20,0x24,0x24,0x20,0x28,0x2c,0x2c,0x28,0x2c,0x28,0x28,0x2c,
  0x00,0x04,0x04,0x00,0x04,0x00,0x00,0x04,0x0c,0x08,0x08,0x0c,0x08,0x0c,0x0c,0x08,
  0x04,0x00,0x00,0x04,0x00,0x04,0x04,0x00,0x08,0x0c,0x0c,0x08,0x0c,0x08,0x08,0x0c,
  0x24,0x20,0x20,0x24,0x20,0x24,0x24,0x20,0x28,0x2c,0x2c,0x28,0x2c,0x28,0x28,0x2c,
  0x20,0x24,0x24,0x20,0x24,0x20,0x20,0x24,0x2c,0x28,0x28,0x2c,0x28,0x2c,0x2c,0x28,
  0x80,0x84,0x84,0x80,0x84,0x80,0x80,0x84,0x8c,0x88,0x88,0x8c,0x88,0x8c,0x8c,0x88,
  0x84,0x80,0x80,0x84,0x80,0x84,0x84,0x80,0x88,0x8c,0x8c,0x88,0x8c,0x88,0x88,0x8c,
  0xa4,0xa0,0xa0,0xa4,0xa0,0xa4,0xa4,0xa0,0xa8,0xac,0xac,0xa8,0xac,0xa8,0xa8,0xac,
  0xa0,0xa4,0xa4,0xa0,0xa4,0xa0,0xa0,0xa4,0xac,0xa8,0xa8,0xac,0xa8,0xac,0xac,0xa8,
  0x84,0x80,0x80,0x84,0x80,0x84,0x84,0x80,0x88,0x8c,0x8c,0x88,0x8c,0x88,0x88,0x8c,
  0x80,0x84,0x84,0x80,0x84,0x80,0x80,0x84,0x8c,0x88,0x88,0x8c,0x88,0x8c,0x8c,0x88,
  0xa0,0xa4,0xa4,0xa0,0xa4,0xa0,0xa0,0xa4,0xac,0xa8,0xa8,0xac,0xa8,0xac,0xac,0xa8,
  0xa4,0xa0,0xa0,0xa4,0xa0,0xa4,0xa4,0xa0,0xa8,0xac,0xac,0xa8,0xac,0xa8,0xa8,0xac,
};

static inline uint8_t _z80_sz_flags(uint8_t val) {
    return (val != 0) ? (val & Z80_SF) : Z80_ZF;
}

static inline uint8_t _z80_szyxch_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return _z80_sz_flags(res) |
        (res & (Z80_YF|Z80_XF)) |
        ((res >> 8) & Z80_CF) |
        ((acc ^ val ^ res) & Z80_HF);
}

static inline uint8_t _z80_add_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return _z80_szyxch_flags(acc, val, res) | ((((val ^ acc ^ 0x80) & (val ^ res)) >> 5) & Z80_VF);
}

static inline uint8_t _z80_sub_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return Z80_NF | _z80_szyxch_flags(acc, val, res) | ((((val ^ acc) & (res ^ acc)) >> 5) & Z80_VF);
}

static inline uint8_t _z80_cp_flags(uint8_t acc, uint8_t val, uint32_t res) {
    return Z80_NF | 
        _z80_sz_flags(res) |
        (val & (Z80_YF|Z80_XF)) |
        ((res >> 8) & Z80_CF) |
        ((acc ^ val ^ res) & Z80_HF) |
        ((((val ^ acc) & (res ^ acc)) >> 5) & Z80_VF);    
}

static inline uint8_t _z80_sziff2_flags(z80_t* cpu, uint8_t val) {
    return (cpu->f & Z80_CF) | _z80_sz_flags(val) | (val & (Z80_YF|Z80_XF)) | (cpu->iff2 ? Z80_PF : 0);
}

static inline void _z80_add8(z80_t* cpu, uint8_t val) {
    uint32_t res = cpu->a + val;
    cpu->f = _z80_add_flags(cpu->a, val, res);
    cpu->a = (uint8_t)res;
}

static inline void _z80_adc8(z80_t* cpu, uint8_t val) {
    uint32_t res = cpu->a + val + (cpu->f & Z80_CF);
    cpu->f = _z80_add_flags(cpu->a, val, res);
    cpu->a = (uint8_t)res;
}

static inline void _z80_sub8(z80_t* cpu, uint8_t val) {
    uint32_t res = (uint32_t) ((int)cpu->a - (int)val);
    cpu->f = _z80_sub_flags(cpu->a, val, res);
    cpu->a = (uint8_t)res;
}

static inline void _z80_sbc8(z80_t* cpu, uint8_t val) {
    uint32_t res = (uint32_t) ((int)cpu->a - (int)val - (cpu->f & Z80_CF));
    cpu->f = _z80_sub_flags(cpu->a, val, res);
    cpu->a = (uint8_t)res;
}

static inline void _z80_and8(z80_t* cpu, uint8_t val) {
    cpu->a &= val;
    cpu->f = _z80_szp_flags[cpu->a] | Z80_HF;
}

static inline void _z80_xor8(z80_t* cpu, uint8_t val) {
    cpu->a ^= val;
    cpu->f = _z80_szp_flags[cpu->a];
}

static inline void _z80_or8(z80_t* cpu, uint8_t val) {
    cpu->a |= val;
    cpu->f = _z80_szp_flags[cpu->a];
}

static inline void _z80_cp8(z80_t* cpu, uint8_t val) {
    uint32_t res = (uint32_t) ((int)cpu->a - (int)val);
    cpu->f = _z80_cp_flags(cpu->a, val, res);
}

static inline void _z80_neg8(z80_t* cpu) {
    uint32_t res = (uint32_t) (0 - (int)cpu->a);
    cpu->f = _z80_sub_flags(0, cpu->a, res);
    cpu->a = (uint8_t)res;
}

static inline uint8_t _z80_inc8(z80_t* cpu, uint8_t val) {
    uint8_t res = val + 1;
    uint8_t f = _z80_sz_flags(res) | (res & (Z80_XF|Z80_YF)) | ((res ^ val) & Z80_HF);
    if (res == 0x80) {
        f |= Z80_VF;
    }
    cpu->f = f | (cpu->f & Z80_CF);
    return res;
}

static inline uint8_t _z80_dec8(z80_t* cpu, uint8_t val) {
    uint8_t res = val - 1;
    uint8_t f = Z80_NF | _z80_sz_flags(res) | (res & (Z80_XF|Z80_YF)) | ((res ^ val) & Z80_HF);
    if (res == 0x7F) {
        f |= Z80_VF;
    }
    cpu->f = f | (cpu->f & Z80_CF);
    return res;
}

static inline void _z80_ex_de_hl(z80_t* cpu) {
    uint16_t tmp = cpu->hl;
    cpu->hl = cpu->de;
    cpu->de = tmp;
}

static inline void _z80_ex_af_af2(z80_t* cpu) {
    uint16_t tmp = cpu->af2;
    cpu->af2 = cpu->af;
    cpu->af = tmp;
}

static inline void _z80_exx(z80_t* cpu) {
    uint16_t tmp;
    tmp = cpu->bc; cpu->bc = cpu->bc2; cpu->bc2 = tmp;
    tmp = cpu->de; cpu->de = cpu->de2; cpu->de2 = tmp;
    tmp = cpu->hl; cpu->hl = cpu->hl2; cpu->hl2 = tmp;
}

static inline void _z80_rlca(z80_t* cpu) {
    uint8_t res = (cpu->a << 1) | (cpu->a >> 7);
    cpu->f = ((cpu->a >> 7) & Z80_CF) | (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | (res & (Z80_YF|Z80_XF));
    cpu->a = res;
}

static inline void _z80_rrca(z80_t* cpu) {
    uint8_t res = (cpu->a >> 1) | (cpu->a << 7);
    cpu->f = (cpu->a & Z80_CF) | (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | (res & (Z80_YF|Z80_XF));
    cpu->a = res;
}

static inline void _z80_rla(z80_t* cpu) {
    uint8_t res = (cpu->a << 1) | (cpu->f & Z80_CF);
    cpu->f = ((cpu->a >> 7) & Z80_CF) | (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | (res & (Z80_YF|Z80_XF));
    cpu->a = res;
}

static inline void _z80_rra(z80_t* cpu) {
    uint8_t res = (cpu->a >> 1) | ((cpu->f & Z80_CF) << 7);
    cpu->f = (cpu->a & Z80_CF) | (cpu->f & (Z80_SF|Z80_ZF|Z80_PF)) | (res & (Z80_YF|Z80_XF));
    cpu->a = res;
}

static inline void _z80_daa(z80_t* cpu) {
    uint8_t res = cpu->a;
    if (cpu->f & Z80_NF) {
        if (((cpu->a & 0xF)>0x9) || (cpu->f & Z80_HF)) {
            res -= 0x06;
        }
        if ((cpu->a > 0x99) || (cpu->f & Z80_CF)) {
            res -= 0x60;
        }
    }
    else {
        if (((cpu->a & 0xF)>0x9) || (cpu->f & Z80_HF)) {
            res += 0x06;
        }
        if ((cpu->a > 0x99) || (cpu->f & Z80_CF)) {
            res += 0x60;
        }
    }
    cpu->f &= Z80_CF|Z80_NF;
    cpu->f |= (cpu->a > 0x99) ? Z80_CF : 0;
    cpu->f |= (cpu->a ^ res) & Z80_HF;
    cpu->f |= _z80_szp_flags[res];
    cpu->a = res;
}

static inline void _z80_cpl(z80_t* cpu) {
    cpu->a ^= 0xFF;
    cpu->f= (cpu->f & (Z80_SF|Z80_ZF|Z80_PF|Z80_CF)) |Z80_HF|Z80_NF| (cpu->a & (Z80_YF|Z80_XF));
}

static inline void _z80_scf(z80_t* cpu) {
    cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_PF|Z80_CF)) | Z80_CF | (cpu->a & (Z80_YF|Z80_XF));
}

static inline void _z80_ccf(z80_t* cpu) {
    cpu->f = ((cpu->f & (Z80_SF|Z80_ZF|Z80_PF|Z80_CF)) | ((cpu->f & Z80_CF)<<4) | (cpu->a & (Z80_YF|Z80_XF))) ^ Z80_CF;
}

static inline void _z80_add16(z80_t* cpu, uint16_t val) {
    const uint16_t acc = cpu->hlx[cpu->hlx_idx].hl;
    cpu->wz = acc + 1;
    const uint32_t res = acc + val;
    cpu->hlx[cpu->hlx_idx].hl = res;
    cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_VF)) |
             (((acc ^ res ^ val)>>8)&Z80_HF) | 
             ((res >> 16) & Z80_CF) |
             ((res >> 8) & (Z80_YF|Z80_XF));
}

static inline void _z80_adc16(z80_t* cpu, uint16_t val) {
    // NOTE: adc is ED-prefixed, so they are never rewired to IX/IY
    const uint16_t acc = cpu->hl;
    cpu->wz = acc + 1;
    const uint32_t res = acc + val + (cpu->f & Z80_CF);
    cpu->hl = res;
    cpu->f = (((val ^ acc ^ 0x8000) & (val ^ res) & 0x8000) >> 13) |
             (((acc ^ res ^ val) >>8 ) & Z80_HF) |
             ((res >> 16) & Z80_CF) |
             ((res >> 8) & (Z80_SF|Z80_YF|Z80_XF)) |
             ((res & 0xFFFF) ? 0 : Z80_ZF);
}

static inline void _z80_sbc16(z80_t* cpu, uint16_t val) {
    // NOTE: sbc is ED-prefixed, so they are never rewired to IX/IY
    const uint16_t acc = cpu->hl;
    cpu->wz = acc + 1;
    const uint32_t res = acc - val - (cpu->f & Z80_CF);
    cpu->hl = res;
    cpu->f = (Z80_NF | (((val ^ acc) & (acc ^ res) & 0x8000) >> 13)) | 
             (((acc ^ res ^ val) >> 8) & Z80_HF) |
             ((res >> 16) & Z80_CF) |
             ((res >> 8) & (Z80_SF|Z80_YF|Z80_XF)) |
             ((res & 0xFFFF) ? 0 : Z80_ZF);
}

static inline bool _z80_ldi_ldd(z80_t* cpu, uint8_t val) {
    const uint8_t res = cpu->a + val;
    cpu->bc -= 1;
    cpu->f = (cpu->f & (Z80_SF|Z80_ZF|Z80_CF)) |
             ((res & 2) ? Z80_YF : 0) |
             ((res & 8) ? Z80_XF : 0) |
             (cpu->bc ? Z80_VF : 0);
    return cpu->bc != 0;
}

static inline bool _z80_cpi_cpd(z80_t* cpu, uint8_t val) {
    uint32_t res = (uint32_t) ((int)cpu->a - (int)val);
    cpu->bc -= 1;
    uint8_t f = (cpu->f & Z80_CF)|Z80_NF|_z80_sz_flags(res);
    if ((res & 0xF) > ((uint32_t)cpu->a & 0xF)) {
        f |= Z80_HF;
        res--;
    }
    if (res & 2) { f |= Z80_YF; }
    if (res & 8) { f |= Z80_XF; }
    if (cpu->bc) { f |= Z80_VF; }
    cpu->f = f;
    return (cpu->bc != 0) && !(f & Z80_ZF);
}

static inline bool _z80_ini_ind(z80_t* cpu, uint8_t val, uint8_t c) {
    const uint8_t b = cpu->b;
    uint8_t f = _z80_sz_flags(b) | (b & (Z80_XF|Z80_YF));
    if (val & Z80_SF) { f |= Z80_NF; }
    uint32_t t = (uint32_t)c + val;
    if (t & 0x100) { f |= Z80_HF|Z80_CF; }
    f |= _z80_szp_flags[((uint8_t)(t & 7)) ^ b] & Z80_PF;
    cpu->f = f;
    return (b != 0);
}

static inline bool _z80_outi_outd(z80_t* cpu, uint8_t val) {
    const uint8_t b = cpu->b;
    uint8_t f = _z80_sz_flags(b) | (b & (Z80_XF|Z80_YF));
    if (val & Z80_SF) { f |= Z80_NF; }
    uint32_t t = (uint32_t)cpu->l + val;
    if (t & 0x0100) { f |= Z80_HF|Z80_CF; }
    f |= _z80_szp_flags[((uint8_t)(t & 7)) ^ b] & Z80_PF;
    cpu->f = f;
    return (b != 0);
}

static inline uint8_t _z80_in(z80_t* cpu, uint8_t val) {
    cpu->f = (cpu->f & Z80_CF) | _z80_szp_flags[val];
    return val;
}

static inline uint8_t _z80_rrd(z80_t* cpu, uint8_t val) {
    const uint8_t l = cpu->a & 0x0F;
    cpu->a = (cpu->a & 0xF0) | (val & 0x0F);
    val = (val >> 4) | (l << 4);
    cpu->f = (cpu->f & Z80_CF) | _z80_szp_flags[cpu->a];
    return val;
}

static inline uint8_t _z80_rld(z80_t* cpu, uint8_t val) {
    const uint8_t l = cpu->a & 0x0F;
    cpu->a = (cpu->a & 0xF0) | (val >> 4);
    val = (val << 4) | l;
    cpu->f = (cpu->f & Z80_CF) | _z80_szp_flags[cpu->a];
    return val;
}

static inline uint8_t _z80_rlc(z80_t* cpu, uint8_t val) {
    uint8_t res = (val<<1) | (val>>7);
    cpu->f = _z80_szp_flags[res] | ((val>>7) & Z80_CF);
    return res;
}

static inline uint8_t _z80_rrc(z80_t* cpu, uint8_t val) {
    uint8_t res = (val>>1) | (val<<7);
    cpu->f = _z80_szp_flags[res] | (val & Z80_CF);
    return res;
}

static inline uint8_t _z80_rl(z80_t* cpu, uint8_t val) {
    uint8_t res = (val<<1) | (cpu->f & Z80_CF);
    cpu->f = _z80_szp_flags[res] | ((val>>7) & Z80_CF);
    return res;
}

static inline uint8_t _z80_rr(z80_t* cpu, uint8_t val) {
    uint8_t res = (val>>1) | ((cpu->f & Z80_CF)<<7);
    cpu->f = _z80_szp_flags[res] | (val & Z80_CF);
    return res;
}

static inline uint8_t _z80_sla(z80_t* cpu, uint8_t val) {
    uint8_t res = val<<1;
    cpu->f = _z80_szp_flags[res] | ((val>>7) & Z80_CF);
    return res;
}

static inline uint8_t _z80_sra(z80_t* cpu, uint8_t val) {
    uint8_t res = (val>>1) | (val & 0x80);
    cpu->f = _z80_szp_flags[res] | (val & Z80_CF);
    return res;
}

static inline uint8_t _z80_sll(z80_t* cpu, uint8_t val) {
    uint8_t res = (val<<1) | 1;
    cpu->f = _z80_szp_flags[res] | ((val>>7) & Z80_CF);
    return res;
}

static inline uint8_t _z80_srl(z80_t* cpu, uint8_t val) {
    uint8_t res = val>>1;
    cpu->f = _z80_szp_flags[res] | (val & Z80_CF);
    return res;
}

static inline uint64_t _z80_set_ab(uint64_t pins, uint16_t ab) {
    return (pins & ~0xFFFF) | ab;
}

static inline uint64_t _z80_set_ab_x(uint64_t pins, uint16_t ab, uint64_t x) {
    return (pins & ~0xFFFF) | ab | x;
}

static inline uint64_t _z80_set_ab_db(uint64_t pins, uint16_t ab, uint8_t db) {
    return (pins & ~0xFFFFFF) | (db<<16) | ab;
}

static inline uint64_t _z80_set_ab_db_x(uint64_t pins, uint16_t ab, uint8_t db, uint64_t x) {
    return (pins & ~0xFFFFFF) | (db<<16) | ab | x;
}

static inline uint8_t _z80_get_db(uint64_t pins) {
    return (uint8_t)(pins>>16);
}

// CB-prefix block action
static inline bool _z80_cb_action(z80_t* cpu, uint8_t z0, uint8_t z1) {
    const uint8_t x = cpu->opcode>>6;
    const uint8_t y = (cpu->opcode>>3)&7;
    uint8_t val, res;
    switch (z0) {
        case 0: val = cpu->b; break;
        case 1: val = cpu->c; break;
        case 2: val = cpu->d; break;
        case 3: val = cpu->e; break;
        case 4: val = cpu->h; break;
        case 5: val = cpu->l; break;
        case 6: val = cpu->dlatch; break;   // (HL)
        case 7: val = cpu->a; break;
        default: _Z80_UNREACHABLE;
    }
    switch (x) {
        case 0: // rot/shift
            switch (y) {
                case 0: res = _z80_rlc(cpu, val); break;
                case 1: res = _z80_rrc(cpu, val); break;
                case 2: res = _z80_rl(cpu, val); break;
                case 3: res = _z80_rr(cpu, val); break;
                case 4: res = _z80_sla(cpu, val); break;
                case 5: res = _z80_sra(cpu, val); break;
                case 6: res = _z80_sll(cpu, val); break;
                case 7: res = _z80_srl(cpu, val); break;
                default: _Z80_UNREACHABLE;
            }
            break;
        case 1: // bit
            res = val & (1<<y);
            cpu->f = (cpu->f & Z80_CF) | Z80_HF | (res ? (res & Z80_SF) : (Z80_ZF|Z80_PF));
            if (z0 == 6) {
                cpu->f |= (cpu->wz >> 8) & (Z80_YF|Z80_XF);
            }
            else {
                cpu->f |= val & (Z80_YF|Z80_XF);
            }
            break;
        case 2: // res
            res = val & ~(1 << y);
            break;
        case 3: // set
            res = val | (1 << y);
            break;
        default: _Z80_UNREACHABLE;
    }
    // don't write result back for BIT
    if (x != 1) {
        cpu->dlatch = res;
        switch (z1) {
            case 0: cpu->b = res; break;
            case 1: cpu->c = res; break;
            case 2: cpu->d = res; break;
            case 3: cpu->e = res; break;
            case 4: cpu->h = res; break;
            case 5: cpu->l = res; break;
            case 6: break;   // (HL)
            case 7: cpu->a = res; break;
            default: _Z80_UNREACHABLE;
        }
        return true;
    }
    else {
        return false;
    }
}

// compute the effective memory address for DD+CB/FD+CB instructions
static inline void _z80_ddfdcb_addr(z80_t* cpu, uint64_t pins) {
    uint8_t d = _z80_get_db(pins);
    cpu->addr = cpu->hlx[cpu->hlx_idx].hl + (int8_t)d;
    cpu->wz = cpu->addr;
}

// load the opcode from data bus for DD+CB/FD+CB instructions
static inline void _z80_ddfdcb_opcode(z80_t* cpu, uint8_t oc) {
    cpu->opcode = oc;
}

// special case opstate table slots
#define _Z80_OPSTATE_SLOT_CB        (512)
#define _Z80_OPSTATE_SLOT_CBHL      (512+1)
#define _Z80_OPSTATE_SLOT_DDFDCB    (512+2)
#define _Z80_OPSTATE_SLOT_INT_IM0   (512+3)
#define _Z80_OPSTATE_SLOT_INT_IM1   (512+4)
#define _Z80_OPSTATE_SLOT_INT_IM2   (512+5)
#define _Z80_OPSTATE_SLOT_NMI       (512+6)

#define _Z80_OPSTATE_STEP_INDIRECT (5)          // see case-branch '6'
#define _Z80_OPSTATE_STEP_INDIRECT_IMM8 (13)    // see case-branch '14'


#define _Z80_OPSTATE_NUM_SPECIAL_OPS (7)

static const z80_opstate_t _z80_opstate_table[2*256 + _Z80_OPSTATE_NUM_SPECIAL_OPS] = {
    {   27,   27 },  //  00: NOP (M:1 T:4 steps:1)
    {   28,   28 },  //  01: LD BC,nn (M:3 T:10 steps:7)
    {   35,   35 },  //  02: LD (BC),A (M:2 T:7 steps:4)
    {   39,   39 },  //  03: INC BC (M:2 T:6 steps:3)
    {   42,   42 },  //  04: INC B (M:1 T:4 steps:1)
    {   43,   43 },  //  05: DEC B (M:1 T:4 steps:1)
    {   44,   44 },  //  06: LD B,n (M:2 T:7 steps:4)
    {   48,   48 },  //  07: RLCA (M:1 T:4 steps:1)
    {   49,   49 },  //  08: EX AF,AF' (M:1 T:4 steps:1)
    {   50,   50 },  //  09: ADD HL,BC (M:2 T:11 steps:8)
    {   58,   58 },  //  0A: LD A,(BC) (M:2 T:7 steps:4)
    {   62,   62 },  //  0B: DEC BC (M:2 T:6 steps:3)
    {   65,   65 },  //  0C: INC C (M:1 T:4 steps:1)
    {   66,   66 },  //  0D: DEC C (M:1 T:4 steps:1)
    {   67,   67 },  //  0E: LD C,n (M:2 T:7 steps:4)
    {   71,   71 },  //  0F: RRCA (M:1 T:4 steps:1)
    {   72,   72 },  //  10: DJNZ d (M:4 T:13 steps:10)
    {   82,   82 },  //  11: LD DE,nn (M:3 T:10 steps:7)
    {   89,   89 },  //  12: LD (DE),A (M:2 T:7 steps:4)
    {   93,   93 },  //  13: INC DE (M:2 T:6 steps:3)
    {   96,   96 },  //  14: INC D (M:1 T:4 steps:1)
    {   97,   97 },  //  15: DEC D (M:1 T:4 steps:1)
    {   98,   98 },  //  16: LD D,n (M:2 T:7 steps:4)
    {  102,  102 },  //  17: RLA (M:1 T:4 steps:1)
    {  103,  103 },  //  18: JR d (M:3 T:12 steps:9)
    {  112,  112 },  //  19: ADD HL,DE (M:2 T:11 steps:8)
    {  120,  120 },  //  1A: LD A,(DE) (M:2 T:7 steps:4)
    {  124,  124 },  //  1B: DEC DE (M:2 T:6 steps:3)
    {  127,  127 },  //  1C: INC E (M:1 T:4 steps:1)
    {  128,  128 },  //  1D: DEC E (M:1 T:4 steps:1)
    {  129,  129 },  //  1E: LD E,n (M:2 T:7 steps:4)
    {  133,  133 },  //  1F: RRA (M:1 T:4 steps:1)
    {  134,  134 },  //  20: JR NZ,d (M:3 T:12 steps:9)
    {  143,  143 },  //  21: LD HL,nn (M:3 T:10 steps:7)
    {  150,  150 },  //  22: LD (nn),HL (M:5 T:16 steps:13)
    {  163,  163 },  //  23: INC HL (M:2 T:6 steps:3)
    {  166,  166 },  //  24: INC H (M:1 T:4 steps:1)
    {  167,  167 },  //  25: DEC H (M:1 T:4 steps:1)
    {  168,  168 },  //  26: LD H,n (M:2 T:7 steps:4)
    {  172,  172 },  //  27: DAA (M:1 T:4 steps:1)
    {  173,  173 },  //  28: JR Z,d (M:3 T:12 steps:9)
    {  182,  182 },  //  29: ADD HL,HL (M:2 T:11 steps:8)
    {  190,  190 },  //  2A: LD HL,(nn) (M:5 T:16 steps:13)
    {  203,  203 },  //  2B: DEC HL (M:2 T:6 steps:3)
    {  206,  206 },  //  2C: INC L (M:1 T:4 steps:1)
    {  207,  207 },  //  2D: DEC L (M:1 T:4 steps:1)
    {  208,  208 },  //  2E: LD L,n (M:2 T:7 steps:4)
    {  212,  212 },  //  2F: CPL (M:1 T:4 steps:1)
    {  213,  213 },  //  30: JR NC,d (M:3 T:12 steps:9)
    {  222,  222 },  //  31: LD SP,nn (M:3 T:10 steps:7)
    {  229,  229 },  //  32: LD (nn),A (M:4 T:13 steps:10)
    {  239,  239 },  //  33: INC SP (M:2 T:6 steps:3)
    {  242, _Z80_OPSTATE_STEP_INDIRECT },  //  34: INC (HL) (M:3 T:11 steps:8)
    {  250, _Z80_OPSTATE_STEP_INDIRECT },  //  35: DEC (HL) (M:3 T:11 steps:8)
    {  258, _Z80_OPSTATE_STEP_INDIRECT_IMM8 },  //  36: LD (HL),n (M:3 T:10 steps:7)
    {  265,  265 },  //  37: SCF (M:1 T:4 steps:1)
    {  266,  266 },  //  38: JR C,d (M:3 T:12 steps:9)
    {  275,  275 },  //  39: ADD HL,SP (M:2 T:11 steps:8)
    {  283,  283 },  //  3A: LD A,(nn) (M:4 T:13 steps:10)
    {  293,  293 },  //  3B: DEC SP (M:2 T:6 steps:3)
    {  296,  296 },  //  3C: INC A (M:1 T:4 steps:1)
    {  297,  297 },  //  3D: DEC A (M:1 T:4 steps:1)
    {  298,  298 },  //  3E: LD A,n (M:2 T:7 steps:4)
    {  302,  302 },  //  3F: CCF (M:1 T:4 steps:1)
    {  303,  303 },  //  40: LD B,B (M:1 T:4 steps:1)
    {  304,  304 },  //  41: LD B,C (M:1 T:4 steps:1)
    {  305,  305 },  //  42: LD B,D (M:1 T:4 steps:1)
    {  306,  306 },  //  43: LD B,E (M:1 T:4 steps:1)
    {  307,  307 },  //  44: LD B,H (M:1 T:4 steps:1)
    {  308,  308 },  //  45: LD B,L (M:1 T:4 steps:1)
    {  309, _Z80_OPSTATE_STEP_INDIRECT },  //  46: LD B,(HL) (M:2 T:7 steps:4)
    {  313,  313 },  //  47: LD B,A (M:1 T:4 steps:1)
    {  314,  314 },  //  48: LD C,B (M:1 T:4 steps:1)
    {  315,  315 },  //  49: LD C,C (M:1 T:4 steps:1)
    {  316,  316 },  //  4A: LD C,D (M:1 T:4 steps:1)
    {  317,  317 },  //  4B: LD C,E (M:1 T:4 steps:1)
    {  318,  318 },  //  4C: LD C,H (M:1 T:4 steps:1)
    {  319,  319 },  //  4D: LD C,L (M:1 T:4 steps:1)
    {  320, _Z80_OPSTATE_STEP_INDIRECT },  //  4E: LD C,(HL) (M:2 T:7 steps:4)
    {  324,  324 },  //  4F: LD C,A (M:1 T:4 steps:1)
    {  325,  325 },  //  50: LD D,B (M:1 T:4 steps:1)
    {  326,  326 },  //  51: LD D,C (M:1 T:4 steps:1)
    {  327,  327 },  //  52: LD D,D (M:1 T:4 steps:1)
    {  328,  328 },  //  53: LD D,E (M:1 T:4 steps:1)
    {  329,  329 },  //  54: LD D,H (M:1 T:4 steps:1)
    {  330,  330 },  //  55: LD D,L (M:1 T:4 steps:1)
    {  331, _Z80_OPSTATE_STEP_INDIRECT },  //  56: LD D,(HL) (M:2 T:7 steps:4)
    {  335,  335 },  //  57: LD D,A (M:1 T:4 steps:1)
    {  336,  336 },  //  58: LD E,B (M:1 T:4 steps:1)
    {  337,  337 },  //  59: LD E,C (M:1 T:4 steps:1)
    {  338,  338 },  //  5A: LD E,D (M:1 T:4 steps:1)
    {  339,  339 },  //  5B: LD E,E (M:1 T:4 steps:1)
    {  340,  340 },  //  5C: LD E,H (M:1 T:4 steps:1)
    {  341,  341 },  //  5D: LD E,L (M:1 T:4 steps:1)
    {  342, _Z80_OPSTATE_STEP_INDIRECT },  //  5E: LD E,(HL) (M:2 T:7 steps:4)
    {  346,  346 },  //  5F: LD E,A (M:1 T:4 steps:1)
    {  347,  347 },  //  60: LD H,B (M:1 T:4 steps:1)
    {  348,  348 },  //  61: LD H,C (M:1 T:4 steps:1)
    {  349,  349 },  //  62: LD H,D (M:1 T:4 steps:1)
    {  350,  350 },  //  63: LD H,E (M:1 T:4 steps:1)
    {  351,  351 },  //  64: LD H,H (M:1 T:4 steps:1)
    {  352,  352 },  //  65: LD H,L (M:1 T:4 steps:1)
    {  353, _Z80_OPSTATE_STEP_INDIRECT },  //  66: LD H,(HL) (M:2 T:7 steps:4)
    {  357,  357 },  //  67: LD H,A (M:1 T:4 steps:1)
    {  358,  358 },  //  68: LD L,B (M:1 T:4 steps:1)
    {  359,  359 },  //  69: LD L,C (M:1 T:4 steps:1)
    {  360,  360 },  //  6A: LD L,D (M:1 T:4 steps:1)
    {  361,  361 },  //  6B: LD L,E (M:1 T:4 steps:1)
    {  362,  362 },  //  6C: LD L,H (M:1 T:4 steps:1)
    {  363,  363 },  //  6D: LD L,L (M:1 T:4 steps:1)
    {  364, _Z80_OPSTATE_STEP_INDIRECT },  //  6E: LD L,(HL) (M:2 T:7 steps:4)
    {  368,  368 },  //  6F: LD L,A (M:1 T:4 steps:1)
    {  369, _Z80_OPSTATE_STEP_INDIRECT },  //  70: LD (HL),B (M:2 T:7 steps:4)
    {  373, _Z80_OPSTATE_STEP_INDIRECT },  //  71: LD (HL),C (M:2 T:7 steps:4)
    {  377, _Z80_OPSTATE_STEP_INDIRECT },  //  72: LD (HL),D (M:2 T:7 steps:4)
    {  381, _Z80_OPSTATE_STEP_INDIRECT },  //  73: LD (HL),E (M:2 T:7 steps:4)
    {  385, _Z80_OPSTATE_STEP_INDIRECT },  //  74: LD (HL),H (M:2 T:7 steps:4)
    {  389, _Z80_OPSTATE_STEP_INDIRECT },  //  75: LD (HL),L (M:2 T:7 steps:4)
    {  393,  393 },  //  76: HALT (M:1 T:4 steps:1)
    {  394, _Z80_OPSTATE_STEP_INDIRECT },  //  77: LD (HL),A (M:2 T:7 steps:4)
    {  398,  398 },  //  78: LD A,B (M:1 T:4 steps:1)
    {  399,  399 },  //  79: LD A,C (M:1 T:4 steps:1)
    {  400,  400 },  //  7A: LD A,D (M:1 T:4 steps:1)
    {  401,  401 },  //  7B: LD A,E (M:1 T:4 steps:1)
    {  402,  402 },  //  7C: LD A,H (M:1 T:4 steps:1)
    {  403,  403 },  //  7D: LD A,L (M:1 T:4 steps:1)
    {  404, _Z80_OPSTATE_STEP_INDIRECT },  //  7E: LD A,(HL) (M:2 T:7 steps:4)
    {  408,  408 },  //  7F: LD A,A (M:1 T:4 steps:1)
    {  409,  409 },  //  80: ADD B (M:1 T:4 steps:1)
    {  410,  410 },  //  81: ADD C (M:1 T:4 steps:1)
    {  411,  411 },  //  82: ADD D (M:1 T:4 steps:1)
    {  412,  412 },  //  83: ADD E (M:1 T:4 steps:1)
    {  413,  413 },  //  84: ADD H (M:1 T:4 steps:1)
    {  414,  414 },  //  85: ADD L (M:1 T:4 steps:1)
    {  415, _Z80_OPSTATE_STEP_INDIRECT },  //  86: ADD (HL) (M:2 T:7 steps:4)
    {  419,  419 },  //  87: ADD A (M:1 T:4 steps:1)
    {  420,  420 },  //  88: ADC B (M:1 T:4 steps:1)
    {  421,  421 },  //  89: ADC C (M:1 T:4 steps:1)
    {  422,  422 },  //  8A: ADC D (M:1 T:4 steps:1)
    {  423,  423 },  //  8B: ADC E (M:1 T:4 steps:1)
    {  424,  424 },  //  8C: ADC H (M:1 T:4 steps:1)
    {  425,  425 },  //  8D: ADC L (M:1 T:4 steps:1)
    {  426, _Z80_OPSTATE_STEP_INDIRECT },  //  8E: ADC (HL) (M:2 T:7 steps:4)
    {  430,  430 },  //  8F: ADC A (M:1 T:4 steps:1)
    {  431,  431 },  //  90: SUB B (M:1 T:4 steps:1)
    {  432,  432 },  //  91: SUB C (M:1 T:4 steps:1)
    {  433,  433 },  //  92: SUB D (M:1 T:4 steps:1)
    {  434,  434 },  //  93: SUB E (M:1 T:4 steps:1)
    {  435,  435 },  //  94: SUB H (M:1 T:4 steps:1)
    {  436,  436 },  //  95: SUB L (M:1 T:4 steps:1)
    {  437, _Z80_OPSTATE_STEP_INDIRECT },  //  96: SUB (HL) (M:2 T:7 steps:4)
    {  441,  441 },  //  97: SUB A (M:1 T:4 steps:1)
    {  442,  442 },  //  98: SBC B (M:1 T:4 steps:1)
    {  443,  443 },  //  99: SBC C (M:1 T:4 steps:1)
    {  444,  444 },  //  9A: SBC D (M:1 T:4 steps:1)
    {  445,  445 },  //  9B: SBC E (M:1 T:4 steps:1)
    {  446,  446 },  //  9C: SBC H (M:1 T:4 steps:1)
    {  447,  447 },  //  9D: SBC L (M:1 T:4 steps:1)
    {  448, _Z80_OPSTATE_STEP_INDIRECT },  //  9E: SBC (HL) (M:2 T:7 steps:4)
    {  452,  452 },  //  9F: SBC A (M:1 T:4 steps:1)
    {  453,  453 },  //  A0: AND B (M:1 T:4 steps:1)
    {  454,  454 },  //  A1: AND C (M:1 T:4 steps:1)
    {  455,  455 },  //  A2: AND D (M:1 T:4 steps:1)
    {  456,  456 },  //  A3: AND E (M:1 T:4 steps:1)
    {  457,  457 },  //  A4: AND H (M:1 T:4 steps:1)
    {  458,  458 },  //  A5: AND L (M:1 T:4 steps:1)
    {  459, _Z80_OPSTATE_STEP_INDIRECT },  //  A6: AND (HL) (M:2 T:7 steps:4)
    {  463,  463 },  //  A7: AND A (M:1 T:4 steps:1)
    {  464,  464 },  //  A8: XOR B (M:1 T:4 steps:1)
    {  465,  465 },  //  A9: XOR C (M:1 T:4 steps:1)
    {  466,  466 },  //  AA: XOR D (M:1 T:4 steps:1)
    {  467,  467 },  //  AB: XOR E (M:1 T:4 steps:1)
    {  468,  468 },  //  AC: XOR H (M:1 T:4 steps:1)
    {  469,  469 },  //  AD: XOR L (M:1 T:4 steps:1)
    {  470, _Z80_OPSTATE_STEP_INDIRECT },  //  AE: XOR (HL) (M:2 T:7 steps:4)
    {  474,  474 },  //  AF: XOR A (M:1 T:4 steps:1)
    {  475,  475 },  //  B0: OR B (M:1 T:4 steps:1)
    {  476,  476 },  //  B1: OR C (M:1 T:4 steps:1)
    {  477,  477 },  //  B2: OR D (M:1 T:4 steps:1)
    {  478,  478 },  //  B3: OR E (M:1 T:4 steps:1)
    {  479,  479 },  //  B4: OR H (M:1 T:4 steps:1)
    {  480,  480 },  //  B5: OR L (M:1 T:4 steps:1)
    {  481, _Z80_OPSTATE_STEP_INDIRECT },  //  B6: OR (HL) (M:2 T:7 steps:4)
    {  485,  485 },  //  B7: OR A (M:1 T:4 steps:1)
    {  486,  486 },  //  B8: CP B (M:1 T:4 steps:1)
    {  487,  487 },  //  B9: CP C (M:1 T:4 steps:1)
    {  488,  488 },  //  BA: CP D (M:1 T:4 steps:1)
    {  489,  489 },  //  BB: CP E (M:1 T:4 steps:1)
    {  490,  490 },  //  BC: CP H (M:1 T:4 steps:1)
    {  491,  491 },  //  BD: CP L (M:1 T:4 steps:1)
    {  492, _Z80_OPSTATE_STEP_INDIRECT },  //  BE: CP (HL) (M:2 T:7 steps:4)
    {  496,  496 },  //  BF: CP A (M:1 T:4 steps:1)
    {  497,  497 },  //  C0: RET NZ (M:4 T:11 steps:8)
    {  505,  505 },  //  C1: POP BC (M:3 T:10 steps:7)
    {  512,  512 },  //  C2: JP NZ,nn (M:3 T:10 steps:7)
    {  519,  519 },  //  C3: JP nn (M:3 T:10 steps:7)
    {  526,  526 },  //  C4: CALL NZ,nn (M:6 T:17 steps:14)
    {  540,  540 },  //  C5: PUSH BC (M:4 T:11 steps:8)
    {  548,  548 },  //  C6: ADD n (M:2 T:7 steps:4)
    {  552,  552 },  //  C7: RST 0h (M:4 T:11 steps:8)
    {  560,  560 },  //  C8: RET Z (M:4 T:11 steps:8)
    {  568,  568 },  //  C9: RET (M:3 T:10 steps:7)
    {  575,  575 },  //  CA: JP Z,nn (M:3 T:10 steps:7)
    {  582,  582 },  //  CB: CB prefix (M:1 T:4 steps:1)
    {  583,  583 },  //  CC: CALL Z,nn (M:6 T:17 steps:14)
    {  597,  597 },  //  CD: CALL nn (M:5 T:17 steps:14)
    {  611,  611 },  //  CE: ADC n (M:2 T:7 steps:4)
    {  615,  615 },  //  CF: RST 8h (M:4 T:11 steps:8)
    {  623,  623 },  //  D0: RET NC (M:4 T:11 steps:8)
    {  631,  631 },  //  D1: POP DE (M:3 T:10 steps:7)
    {  638,  638 },  //  D2: JP NC,nn (M:3 T:10 steps:7)
    {  645,  645 },  //  D3: OUT (n),A (M:3 T:11 steps:8)
    {  653,  653 },  //  D4: CALL NC,nn (M:6 T:17 steps:14)
    {  667,  667 },  //  D5: PUSH DE (M:4 T:11 steps:8)
    {  675,  675 },  //  D6: SUB n (M:2 T:7 steps:4)
    {  679,  679 },  //  D7: RST 10h (M:4 T:11 steps:8)
    {  687,  687 },  //  D8: RET C (M:4 T:11 steps:8)
    {  695,  695 },  //  D9: EXX (M:1 T:4 steps:1)
    {  696,  696 },  //  DA: JP C,nn (M:3 T:10 steps:7)
    {  703,  703 },  //  DB: IN A,(n) (M:3 T:11 steps:8)
    {  711,  711 },  //  DC: CALL C,nn (M:6 T:17 steps:14)
    {  725,  725 },  //  DD: DD prefix (M:1 T:4 steps:1)
    {  726,  726 },  //  DE: SBC n (M:2 T:7 steps:4)
    {  730,  730 },  //  DF: RST 18h (M:4 T:11 steps:8)
    {  738,  738 },  //  E0: RET PO (M:4 T:11 steps:8)
    {  746,  746 },  //  E1: POP HL (M:3 T:10 steps:7)
    {  753,  753 },  //  E2: JP PO,nn (M:3 T:10 steps:7)
    {  760,  760 },  //  E3: EX (SP),HL (M:5 T:19 steps:16)
    {  776,  776 },  //  E4: CALL PO,nn (M:6 T:17 steps:14)
    {  790,  790 },  //  E5: PUSH HL (M:4 T:11 steps:8)
    {  798,  798 },  //  E6: AND n (M:2 T:7 steps:4)
    {  802,  802 },  //  E7: RST 20h (M:4 T:11 steps:8)
    {  810,  810 },  //  E8: RET PE (M:4 T:11 steps:8)
    {  818,  818 },  //  E9: JP HL (M:1 T:4 steps:1)
    {  819,  819 },  //  EA: JP PE,nn (M:3 T:10 steps:7)
    {  826,  826 },  //  EB: EX DE,HL (M:1 T:4 steps:1)
    {  827,  827 },  //  EC: CALL PE,nn (M:6 T:17 steps:14)
    {  841,  841 },  //  ED: ED prefix (M:1 T:4 steps:1)
    {  842,  842 },  //  EE: XOR n (M:2 T:7 steps:4)
    {  846,  846 },  //  EF: RST 28h (M:4 T:11 steps:8)
    {  854,  854 },  //  F0: RET P (M:4 T:11 steps:8)
    {  862,  862 },  //  F1: POP AF (M:3 T:10 steps:7)
    {  869,  869 },  //  F2: JP P,nn (M:3 T:10 steps:7)
    {  876,  876 },  //  F3: DI (M:1 T:4 steps:1)
    {  877,  877 },  //  F4: CALL P,nn (M:6 T:17 steps:14)
    {  891,  891 },  //  F5: PUSH AF (M:4 T:11 steps:8)
    {  899,  899 },  //  F6: OR n (M:2 T:7 steps:4)
    {  903,  903 },  //  F7: RST 30h (M:4 T:11 steps:8)
    {  911,  911 },  //  F8: RET M (M:4 T:11 steps:8)
    {  919,  919 },  //  F9: LD SP,HL (M:2 T:6 steps:3)
    {  922,  922 },  //  FA: JP M,nn (M:3 T:10 steps:7)
    {  929,  929 },  //  FB: EI (M:1 T:4 steps:1)
    {  930,  930 },  //  FC: CALL M,nn (M:6 T:17 steps:14)
    {  944,  944 },  //  FD: FD prefix (M:1 T:4 steps:1)
    {  945,  945 },  //  FE: CP n (M:2 T:7 steps:4)
    {  949,  949 },  //  FF: RST 38h (M:4 T:11 steps:8)
    {  957,  957 },  // ED 00: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 01: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 02: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 03: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 04: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 05: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 06: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 07: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 08: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 09: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 0A: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 0B: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 0C: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 0D: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 0E: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 0F: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 10: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 11: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 12: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 13: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 14: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 15: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 16: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 17: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 18: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 19: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 1A: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 1B: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 1C: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 1D: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 1E: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 1F: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 20: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 21: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 22: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 23: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 24: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 25: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 26: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 27: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 28: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 29: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 2A: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 2B: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 2C: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 2D: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 2E: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 2F: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 30: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 31: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 32: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 33: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 34: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 35: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 36: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 37: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 38: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 39: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 3A: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 3B: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 3C: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 3D: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 3E: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 3F: ED NOP (M:1 T:4 steps:1)
    {  958,  958 },  // ED 40: IN B,(C) (M:2 T:8 steps:5)
    {  963,  963 },  // ED 41: OUT (C),B (M:2 T:8 steps:5)
    {  968,  968 },  // ED 42: SBC HL,BC (M:2 T:11 steps:8)
    {  976,  976 },  // ED 43: LD (nn),BC (M:5 T:16 steps:13)
    {  989,  989 },  // ED 44: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 45: RETI/RETN (M:3 T:10 steps:7)
    {  997,  997 },  // ED 46: IM 0 (M:1 T:4 steps:1)
    {  998,  998 },  // ED 47: LD I,A (M:2 T:5 steps:2)
    { 1000, 1000 },  // ED 48: IN C,(C) (M:2 T:8 steps:5)
    { 1005, 1005 },  // ED 49: OUT (C),C (M:2 T:8 steps:5)
    { 1010, 1010 },  // ED 4A: ADC HL,BC (M:2 T:11 steps:8)
    { 1018, 1018 },  // ED 4B: LD BC,(nn) (M:5 T:16 steps:13)
    {  989,  989 },  // ED 4C: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 4D: RETI/RETN (M:3 T:10 steps:7)
    { 1031, 1031 },  // ED 4E: IM 0 (M:1 T:4 steps:1)
    { 1032, 1032 },  // ED 4F: LD R,A (M:2 T:5 steps:2)
    { 1034, 1034 },  // ED 50: IN D,(C) (M:2 T:8 steps:5)
    { 1039, 1039 },  // ED 51: OUT (C),D (M:2 T:8 steps:5)
    { 1044, 1044 },  // ED 52: SBC HL,DE (M:2 T:11 steps:8)
    { 1052, 1052 },  // ED 53: LD (nn),DE (M:5 T:16 steps:13)
    {  989,  989 },  // ED 54: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 55: RETI/RETN (M:3 T:10 steps:7)
    { 1065, 1065 },  // ED 56: IM 1 (M:1 T:4 steps:1)
    { 1066, 1066 },  // ED 57: LD A,I (M:2 T:5 steps:2)
    { 1068, 1068 },  // ED 58: IN E,(C) (M:2 T:8 steps:5)
    { 1073, 1073 },  // ED 59: OUT (C),E (M:2 T:8 steps:5)
    { 1078, 1078 },  // ED 5A: ADC HL,DE (M:2 T:11 steps:8)
    { 1086, 1086 },  // ED 5B: LD DE,(nn) (M:5 T:16 steps:13)
    {  989,  989 },  // ED 5C: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 5D: RETI/RETN (M:3 T:10 steps:7)
    { 1099, 1099 },  // ED 5E: IM 2 (M:1 T:4 steps:1)
    { 1100, 1100 },  // ED 5F: LD A,R (M:2 T:5 steps:2)
    { 1102, 1102 },  // ED 60: IN H,(C) (M:2 T:8 steps:5)
    { 1107, 1107 },  // ED 61: OUT (C),H (M:2 T:8 steps:5)
    { 1112, 1112 },  // ED 62: SBC HL,HL (M:2 T:11 steps:8)
    { 1120, 1120 },  // ED 63: LD (nn),HL (M:5 T:16 steps:13)
    {  989,  989 },  // ED 64: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 65: RETI/RETN (M:3 T:10 steps:7)
    { 1133, 1133 },  // ED 66: IM 0 (M:1 T:4 steps:1)
    { 1134, 1134 },  // ED 67: RRD (M:4 T:14 steps:11)
    { 1145, 1145 },  // ED 68: IN L,(C) (M:2 T:8 steps:5)
    { 1150, 1150 },  // ED 69: OUT (C),L (M:2 T:8 steps:5)
    { 1155, 1155 },  // ED 6A: ADC HL,HL (M:2 T:11 steps:8)
    { 1163, 1163 },  // ED 6B: LD HL,(nn) (M:5 T:16 steps:13)
    {  989,  989 },  // ED 6C: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 6D: RETI/RETN (M:3 T:10 steps:7)
    { 1176, 1176 },  // ED 6E: IM 0 (M:1 T:4 steps:1)
    { 1177, 1177 },  // ED 6F: RLD (M:4 T:14 steps:11)
    { 1188, 1188 },  // ED 70: IN (C) (M:2 T:8 steps:5)
    { 1193, 1193 },  // ED 71: OUT (C),0 (M:2 T:8 steps:5)
    { 1198, 1198 },  // ED 72: SBC HL,SP (M:2 T:11 steps:8)
    { 1206, 1206 },  // ED 73: LD (nn),SP (M:5 T:16 steps:13)
    {  989,  989 },  // ED 74: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 75: RETI/RETN (M:3 T:10 steps:7)
    { 1219, 1219 },  // ED 76: IM 1 (M:1 T:4 steps:1)
    {  957,  957 },  // ED 77: ED NOP (M:1 T:4 steps:1)
    { 1220, 1220 },  // ED 78: IN A,(C) (M:2 T:8 steps:5)
    { 1225, 1225 },  // ED 79: OUT (C),A (M:2 T:8 steps:5)
    { 1230, 1230 },  // ED 7A: ADC HL,SP (M:2 T:11 steps:8)
    { 1238, 1238 },  // ED 7B: LD SP,(nn) (M:5 T:16 steps:13)
    {  989,  989 },  // ED 7C: NEG (M:1 T:4 steps:1)
    {  990,  990 },  // ED 7D: RETI/RETN (M:3 T:10 steps:7)
    { 1251, 1251 },  // ED 7E: IM 2 (M:1 T:4 steps:1)
    {  957,  957 },  // ED 7F: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 80: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 81: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 82: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 83: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 84: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 85: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 86: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 87: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 88: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 89: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 8A: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 8B: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 8C: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 8D: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 8E: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 8F: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 90: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 91: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 92: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 93: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 94: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 95: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 96: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 97: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 98: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 99: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 9A: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 9B: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 9C: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 9D: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 9E: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED 9F: ED NOP (M:1 T:4 steps:1)
    { 1252, 1252 },  // ED A0: LDI (M:4 T:12 steps:9)
    { 1261, 1261 },  // ED A1: CPI (M:3 T:12 steps:9)
    { 1270, 1270 },  // ED A2: INI (M:4 T:12 steps:9)
    { 1279, 1279 },  // ED A3: OUTI (M:4 T:12 steps:9)
    {  957,  957 },  // ED A4: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED A5: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED A6: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED A7: ED NOP (M:1 T:4 steps:1)
    { 1288, 1288 },  // ED A8: LDD (M:4 T:12 steps:9)
    { 1297, 1297 },  // ED A9: CPD (M:3 T:12 steps:9)
    { 1306, 1306 },  // ED AA: IND (M:4 T:12 steps:9)
    { 1315, 1315 },  // ED AB: OUTD (M:4 T:12 steps:9)
    {  957,  957 },  // ED AC: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED AD: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED AE: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED AF: ED NOP (M:1 T:4 steps:1)
    { 1324, 1324 },  // ED B0: LDIR (M:5 T:17 steps:14)
    { 1338, 1338 },  // ED B1: CPIR (M:4 T:17 steps:14)
    { 1352, 1352 },  // ED B2: INIR (M:5 T:17 steps:14)
    { 1366, 1366 },  // ED B3: OTIR (M:5 T:17 steps:14)
    {  957,  957 },  // ED B4: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED B5: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED B6: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED B7: ED NOP (M:1 T:4 steps:1)
    { 1380, 1380 },  // ED B8: LDDR (M:5 T:17 steps:14)
    { 1394, 1394 },  // ED B9: CPDR (M:4 T:17 steps:14)
    { 1408, 1408 },  // ED BA: INDR (M:5 T:17 steps:14)
    { 1422, 1422 },  // ED BB: OTDR (M:5 T:17 steps:14)
    {  957,  957 },  // ED BC: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED BD: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED BE: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED BF: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C0: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C1: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C2: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C3: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C4: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C5: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C6: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C7: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C8: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED C9: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED CA: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED CB: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED CC: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED CD: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED CE: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED CF: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D0: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D1: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D2: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D3: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D4: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D5: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D6: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D7: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D8: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED D9: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED DA: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED DB: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED DC: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED DD: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED DE: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED DF: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E0: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E1: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E2: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E3: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E4: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E5: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E6: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E7: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E8: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED E9: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED EA: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED EB: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED EC: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED ED: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED EE: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED EF: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F0: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F1: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F2: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F3: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F4: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F5: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F6: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F7: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F8: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED F9: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED FA: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED FB: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED FC: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED FD: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED FE: ED NOP (M:1 T:4 steps:1)
    {  957,  957 },  // ED FF: ED NOP (M:1 T:4 steps:1)
    { 1436, 1436 },  // CB 00: cb (M:1 T:4 steps:1)
    { 1437, 1437 },  // CB 01: cbhl (M:3 T:11 steps:8)
    { 1445, 1445 },  // CB 02: ddfdcb (M:6 T:18 steps:15)
    { 1460, 1460 },  //  03: int_im0 (M:6 T:9 steps:6)
    { 1466, 1466 },  //  04: int_im1 (M:7 T:16 steps:13)
    { 1479, 1479 },  //  05: int_im2 (M:9 T:22 steps:19)
    { 1498, 1498 },  //  06: nmi (M:5 T:14 steps:11)

};

// initiate refresh cycle
static inline uint64_t _z80_refresh(z80_t* cpu, uint64_t pins) {
    pins = _z80_set_ab_x(pins, cpu->ir, Z80_MREQ|Z80_RFSH);
    cpu->r = (cpu->r & 0x80) | ((cpu->r + 1) & 0x7F);
    return pins;
}

// initiate M1 cycle of NMI
static inline uint64_t _z80_nmi_step0(z80_t* cpu, uint64_t pins) {
    // the next regular opcode which is on the data bus is ignored!

    // disable interrupts
    cpu->iff1 = false;

    // if in HALT state, continue
    if (pins & Z80_HALT) {
        pins &= ~Z80_HALT;
        cpu->pc++;
    }
    return pins;
}

// IM0..IM2 initial step
static inline uint64_t _z80_int012_step0(z80_t* cpu, uint64_t pins) {
    // disable interrupts
    cpu->iff1 = cpu->iff2 = false;
    // if in HALT state, continue
    if (pins & Z80_HALT) {
        pins &= ~Z80_HALT;
        cpu->pc++;
    }
    return pins;
}

// IM0..IM2 step 1: issue M1|IORQ cycle
static inline uint64_t _z80_int012_step1(z80_t* cpu, uint64_t pins) {
    (void)cpu;
    // issue M1|IORQ to get opcode byte
    return pins | (Z80_M1|Z80_IORQ);
}

// IM0 step 2: load data bus into opcode
static inline uint64_t _z80_int0_step2(z80_t* cpu, uint64_t pins) {
    // store opcode byte
    cpu->opcode = _z80_get_db(pins);
    return pins;
}

// IM0 step 3: refresh cycle and start executing loaded opcode
static inline uint64_t _z80_int0_step3(z80_t* cpu, uint64_t pins) {
    // branch to interrupt 'payload' instruction (usually an RST)
    cpu->step = _z80_opstate_table[cpu->opcode].step;
    return pins;
}

// initiate a fetch machine cycle for regular (non-prefixed) instructions, or initiate interrupt handling
static inline uint64_t _z80_fetch(z80_t* cpu, uint64_t pins) {
    cpu->prefix_state = 0;
    // shortcut no interrupts requested
    if (cpu->int_bits == 0) {
        cpu->step = 0xFFFF;
        return _z80_set_ab_x(pins, cpu->pc++, Z80_M1|Z80_MREQ|Z80_RD);
    }
    else if (cpu->int_bits & Z80_NMI) {
        // non-maskable interrupt starts with a regular M1 machine cycle
        cpu->step = _z80_opstate_table[_Z80_OPSTATE_SLOT_NMI].step;
        cpu->int_bits = 0;
        // NOTE: PC is *not* incremented!
        return _z80_set_ab_x(pins, cpu->pc, Z80_M1|Z80_MREQ|Z80_RD);
    }
    else if (cpu->int_bits & Z80_INT) {
        if (cpu->iff1) {
            // maskable interrupts start with a special M1 machine cycle which
            // doesn't fetch the next opcode, but instead activate the
            // pins M1|IOQR to request a special byte which is handled differently
            // depending on interrupt mode
            cpu->step = _z80_opstate_table[_Z80_OPSTATE_SLOT_INT_IM0 + cpu->im].step;
            cpu->int_bits = 0;
            // NOTE: PC is not incremented, and no pins are activated here
            return pins;
        }
        else {
            // oops, maskable interrupt requested but disabled
            cpu->step = 0xFFFF;
            return _z80_set_ab_x(pins, cpu->pc++, Z80_M1|Z80_MREQ|Z80_RD);
        }
    }
    else {
        _Z80_UNREACHABLE;
        return pins;
    }
}

static inline uint64_t _z80_fetch_cb(z80_t* cpu, uint64_t pins) {
    cpu->prefix = (cpu->prefix & (_Z80_PREFIX_DD|_Z80_PREFIX_FD)) | _Z80_PREFIX_CB;
    if (cpu->prefix & (_Z80_PREFIX_DD|_Z80_PREFIX_FD)) {
        // this is a DD+CB / FD+CB instruction, continue
        // execution on the special DDCB/FDCB decoder block which
        // loads the d-offset first and then the opcode in a 
        // regular memory read machine cycle
        cpu->step = _z80_opstate_table[_Z80_OPSTATE_SLOT_DDFDCB].step;
    }
    else {
        // this is a regular CB-prefixed instruction, continue
        // execution on a special fetch machine cycle which doesn't
        // handle DD/FD prefix and then branches either to the
        // special CB or CBHL decoder block
        cpu->step = 21; // => step 22: opcode fetch for CB prefixed instructions
        pins = _z80_set_ab_x(pins, cpu->pc++, Z80_M1|Z80_MREQ|Z80_RD);
    }
    return pins;
}

static inline uint64_t _z80_fetch_dd(z80_t* cpu, uint64_t pins) {
    cpu->step = 2;   // => step 3: opcode fetch for DD/FD prefixed instructions
    cpu->hlx_idx = 1;
    cpu->prefix = _Z80_PREFIX_DD;
    return _z80_set_ab_x(pins, cpu->pc++, Z80_M1|Z80_MREQ|Z80_RD);
}

static inline uint64_t _z80_fetch_fd(z80_t* cpu, uint64_t pins) {
    cpu->step = 2;   // => step 3: opcode fetch for DD/FD prefixed instructions
    cpu->hlx_idx = 2;
    cpu->prefix = _Z80_PREFIX_FD;
    return _z80_set_ab_x(pins, cpu->pc++, Z80_M1|Z80_MREQ|Z80_RD);
}

static inline uint64_t _z80_fetch_ed(z80_t* cpu, uint64_t pins) {
    cpu->step = 24; // => step 25: opcode fetch for ED prefixed instructions
    cpu->hlx_idx = 0;
    cpu->prefix = _Z80_PREFIX_ED;
    return _z80_set_ab_x(pins, cpu->pc++, Z80_M1|Z80_MREQ|Z80_RD);
}

uint64_t z80_prefetch(z80_t* cpu, uint16_t new_pc) {
    cpu->pc = new_pc;
    // overlapped M1:T1 of the NOP instruction to initiate opcode fetch at new pc
    cpu->step = _z80_opstate_table[0].step + 1;
    return 0;
}

// pin helper macros
#define _sa(ab)             pins=_z80_set_ab(pins,ab)
#define _sax(ab,x)          pins=_z80_set_ab_x(pins,ab,x)
#define _sad(ab,d)          pins=_z80_set_ab_db(pins,ab,d)
#define _sadx(ab,d,x)       pins=_z80_set_ab_db_x(pins,ab,d,x)
#define _gd()               _z80_get_db(pins)

// high level helper macros
#define _skip(n)        cpu->step+=(n);
#define _fetch_dd()     pins=_z80_fetch_dd(cpu,pins);
#define _fetch_fd()     pins=_z80_fetch_fd(cpu,pins);
#define _fetch_ed()     pins=_z80_fetch_ed(cpu,pins);
#define _fetch_cb()     pins=_z80_fetch_cb(cpu,pins);
#define _mread(ab)      _sax(ab,Z80_MREQ|Z80_RD)
#define _mwrite(ab,d)   _sadx(ab,d,Z80_MREQ|Z80_WR)
#define _ioread(ab)     _sax(ab,Z80_IORQ|Z80_RD)
#define _iowrite(ab,d)  _sadx(ab,d,Z80_IORQ|Z80_WR)
#define _wait()         {if(pins&Z80_WAIT)goto track_int_bits;}
#define _cc_nz          (!(cpu->f&Z80_ZF))
#define _cc_z           (cpu->f&Z80_ZF)
#define _cc_nc          (!(cpu->f&Z80_CF))
#define _cc_c           (cpu->f&Z80_CF)
#define _cc_po          (!(cpu->f&Z80_PF))
#define _cc_pe          (cpu->f&Z80_PF)
#define _cc_p           (!(cpu->f&Z80_SF))
#define _cc_m           (cpu->f&Z80_SF)

uint64_t z80_tick(z80_t* cpu, uint64_t pins) {
    pins &= ~(Z80_CTRL_PIN_MASK|Z80_RETI);
    switch (cpu->step) {
        //=== shared fetch machine cycle for non-DD/FD-prefixed ops
        // M1/T2: load opcode from data bus
        case 0: _wait(); cpu->opcode = _gd(); goto step_next;
        // M1/T3: refresh cycle
        case 1: pins = _z80_refresh(cpu, pins); goto step_next;
        // M1/T4: branch to instruction 'payload'
        case 2: {
            cpu->step = _z80_opstate_table[cpu->opcode].step;
            // this is only needed for (HL) ops, but probably pointless to do a 
            // conditional branch just for this
            cpu->addr = cpu->hlx[cpu->hlx_idx].hl;
        } goto step_next;
        //=== shared fetch machine cycle for DD/FD-prefixed ops
        // M1/T2: load opcode from data bus
        case 3: _wait(); cpu->opcode = _gd(); goto step_next;
        // M1/T3: refresh cycle
        case 4: pins = _z80_refresh(cpu, pins); goto step_next;
        // M1/T4: branch to instruction 'payload'
        case 5: {
            // special case: if this is a prefixed indirect+immediate, which is
            // just LD (IX/IY+d),n, then the immediate-load is 'hidden' within
            // the 8-tcycle d-offset load+computation
            // (this is encoded in the 'alt_step' table item)
            cpu->step = _z80_opstate_table[cpu->opcode].alt_step;
            cpu->addr = cpu->hlx[cpu->hlx_idx].hl;
        } goto step_next;
        //=== optional d-loading cycle for (IX+d), (IY+d)
        //--- mread
        case 6: goto step_next;
        case 7: _wait();_mread(cpu->pc++); goto step_next;
        case 8: cpu->addr += (int8_t)_gd(); cpu->wz = cpu->addr; goto step_next;
        //--- filler ticks
        case 9: goto step_next;
        case 10: goto step_next;
        case 11: goto step_next;
        case 12: goto step_next;
        case 13: {
            // branch to actual instruction
            cpu->step = _z80_opstate_table[cpu->opcode].step;
        } goto step_next;
        //=== special case d-loading cycle for (IX+d),n where the immediate load
        //    is hidden in the d-cycle load
        //--- mread for d offset
        case 14: goto step_next;
        case 15: _wait();_mread(cpu->pc++); goto step_next;
        case 16: cpu->addr += (int8_t)_gd(); cpu->wz = cpu->addr; goto step_next;
        //--- mread for n
        case 17: goto step_next;
        case 18: _wait();_mread(cpu->pc++); goto step_next;
        case 19: cpu->dlatch=_gd(); goto step_next;
        //--- filler tick
        case 20: goto step_next;
        case 21: {
            // branch to ld (hl),n and skip the original mread cycle for loading 'n'
            cpu->step = _z80_opstate_table[cpu->opcode].step + 3;
        } goto step_next;
        //=== special opcode fetch machine cycle for CB-prefixed instructions
        case 22: _wait(); cpu->opcode = _gd(); goto step_next;
        case 23: pins = _z80_refresh(cpu, pins); goto step_next;
        case 24: {
            if ((cpu->opcode & 7) == 6) {
                // this is a (HL) instruction
                cpu->addr = cpu->hl;
                cpu->step = _z80_opstate_table[_Z80_OPSTATE_SLOT_CBHL].step;
            }
            else {
                cpu->step = _z80_opstate_table[_Z80_OPSTATE_SLOT_CB].step;
            }
        } goto step_next;
        //=== special opcode fetch machine cycle for ED-prefixed instructions
        // M1/T2: load opcode from data bus
        case 25: _wait(); cpu->opcode = _gd(); goto step_next;
        // M1/T3: refresh cycle
        case 26: pins = _z80_refresh(cpu, pins); goto step_next;
        // M1/T4: branch to instruction 'payload'
        case 27: {
            cpu->step = _z80_opstate_table[cpu->opcode + 0x100].step;
        } goto step_next;
        //=== from here on code-generated
        
        //  00: NOP (M:1 T:4)
        // -- overlapped
        case   28: goto fetch_next;
        
        //  01: LD BC,nn (M:3 T:10)
        // -- mread
        case   29: goto step_next;
        case   30: _wait();_mread(cpu->pc++);goto step_next;
        case   31: cpu->c=_gd();goto step_next;
        // -- mread
        case   32: goto step_next;
        case   33: _wait();_mread(cpu->pc++);goto step_next;
        case   34: cpu->b=_gd();goto step_next;
        // -- overlapped
        case   35: goto fetch_next;
        
        //  02: LD (BC),A (M:2 T:7)
        // -- mwrite
        case   36: goto step_next;
        case   37: _wait();_mwrite(cpu->bc,cpu->a);cpu->wzl=cpu->c+1;cpu->wzh=cpu->a;goto step_next;
        case   38: goto step_next;
        // -- overlapped
        case   39: goto fetch_next;
        
        //  03: INC BC (M:2 T:6)
        // -- generic
        case   40: cpu->bc++;goto step_next;
        case   41: goto step_next;
        // -- overlapped
        case   42: goto fetch_next;
        
        //  04: INC B (M:1 T:4)
        // -- overlapped
        case   43: cpu->b=_z80_inc8(cpu,cpu->b);goto fetch_next;
        
        //  05: DEC B (M:1 T:4)
        // -- overlapped
        case   44: cpu->b=_z80_dec8(cpu,cpu->b);goto fetch_next;
        
        //  06: LD B,n (M:2 T:7)
        // -- mread
        case   45: goto step_next;
        case   46: _wait();_mread(cpu->pc++);goto step_next;
        case   47: cpu->b=_gd();goto step_next;
        // -- overlapped
        case   48: goto fetch_next;
        
        //  07: RLCA (M:1 T:4)
        // -- overlapped
        case   49: _z80_rlca(cpu);goto fetch_next;
        
        //  08: EX AF,AF' (M:1 T:4)
        // -- overlapped
        case   50: _z80_ex_af_af2(cpu);goto fetch_next;
        
        //  09: ADD HL,BC (M:2 T:11)
        // -- generic
        case   51: _z80_add16(cpu,cpu->bc);goto step_next;
        case   52: goto step_next;
        case   53: goto step_next;
        case   54: goto step_next;
        case   55: goto step_next;
        case   56: goto step_next;
        case   57: goto step_next;
        // -- overlapped
        case   58: goto fetch_next;
        
        //  0A: LD A,(BC) (M:2 T:7)
        // -- mread
        case   59: goto step_next;
        case   60: _wait();_mread(cpu->bc);goto step_next;
        case   61: cpu->a=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case   62: goto fetch_next;
        
        //  0B: DEC BC (M:2 T:6)
        // -- generic
        case   63: cpu->bc--;goto step_next;
        case   64: goto step_next;
        // -- overlapped
        case   65: goto fetch_next;
        
        //  0C: INC C (M:1 T:4)
        // -- overlapped
        case   66: cpu->c=_z80_inc8(cpu,cpu->c);goto fetch_next;
        
        //  0D: DEC C (M:1 T:4)
        // -- overlapped
        case   67: cpu->c=_z80_dec8(cpu,cpu->c);goto fetch_next;
        
        //  0E: LD C,n (M:2 T:7)
        // -- mread
        case   68: goto step_next;
        case   69: _wait();_mread(cpu->pc++);goto step_next;
        case   70: cpu->c=_gd();goto step_next;
        // -- overlapped
        case   71: goto fetch_next;
        
        //  0F: RRCA (M:1 T:4)
        // -- overlapped
        case   72: _z80_rrca(cpu);goto fetch_next;
        
        //  10: DJNZ d (M:4 T:13)
        // -- generic
        case   73: goto step_next;
        // -- mread
        case   74: goto step_next;
        case   75: _wait();_mread(cpu->pc++);goto step_next;
        case   76: cpu->dlatch=_gd();if(--cpu->b==0){_skip(5);};goto step_next;
        // -- generic
        case   77: cpu->pc+=(int8_t)cpu->dlatch;cpu->wz=cpu->pc;goto step_next;
        case   78: goto step_next;
        case   79: goto step_next;
        case   80: goto step_next;
        case   81: goto step_next;
        // -- overlapped
        case   82: goto fetch_next;
        
        //  11: LD DE,nn (M:3 T:10)
        // -- mread
        case   83: goto step_next;
        case   84: _wait();_mread(cpu->pc++);goto step_next;
        case   85: cpu->e=_gd();goto step_next;
        // -- mread
        case   86: goto step_next;
        case   87: _wait();_mread(cpu->pc++);goto step_next;
        case   88: cpu->d=_gd();goto step_next;
        // -- overlapped
        case   89: goto fetch_next;
        
        //  12: LD (DE),A (M:2 T:7)
        // -- mwrite
        case   90: goto step_next;
        case   91: _wait();_mwrite(cpu->de,cpu->a);cpu->wzl=cpu->e+1;cpu->wzh=cpu->a;goto step_next;
        case   92: goto step_next;
        // -- overlapped
        case   93: goto fetch_next;
        
        //  13: INC DE (M:2 T:6)
        // -- generic
        case   94: cpu->de++;goto step_next;
        case   95: goto step_next;
        // -- overlapped
        case   96: goto fetch_next;
        
        //  14: INC D (M:1 T:4)
        // -- overlapped
        case   97: cpu->d=_z80_inc8(cpu,cpu->d);goto fetch_next;
        
        //  15: DEC D (M:1 T:4)
        // -- overlapped
        case   98: cpu->d=_z80_dec8(cpu,cpu->d);goto fetch_next;
        
        //  16: LD D,n (M:2 T:7)
        // -- mread
        case   99: goto step_next;
        case  100: _wait();_mread(cpu->pc++);goto step_next;
        case  101: cpu->d=_gd();goto step_next;
        // -- overlapped
        case  102: goto fetch_next;
        
        //  17: RLA (M:1 T:4)
        // -- overlapped
        case  103: _z80_rla(cpu);goto fetch_next;
        
        //  18: JR d (M:3 T:12)
        // -- mread
        case  104: goto step_next;
        case  105: _wait();_mread(cpu->pc++);goto step_next;
        case  106: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case  107: cpu->pc+=(int8_t)cpu->dlatch;cpu->wz=cpu->pc;goto step_next;
        case  108: goto step_next;
        case  109: goto step_next;
        case  110: goto step_next;
        case  111: goto step_next;
        // -- overlapped
        case  112: goto fetch_next;
        
        //  19: ADD HL,DE (M:2 T:11)
        // -- generic
        case  113: _z80_add16(cpu,cpu->de);goto step_next;
        case  114: goto step_next;
        case  115: goto step_next;
        case  116: goto step_next;
        case  117: goto step_next;
        case  118: goto step_next;
        case  119: goto step_next;
        // -- overlapped
        case  120: goto fetch_next;
        
        //  1A: LD A,(DE) (M:2 T:7)
        // -- mread
        case  121: goto step_next;
        case  122: _wait();_mread(cpu->de);goto step_next;
        case  123: cpu->a=_gd();cpu->wz=cpu->de+1;goto step_next;
        // -- overlapped
        case  124: goto fetch_next;
        
        //  1B: DEC DE (M:2 T:6)
        // -- generic
        case  125: cpu->de--;goto step_next;
        case  126: goto step_next;
        // -- overlapped
        case  127: goto fetch_next;
        
        //  1C: INC E (M:1 T:4)
        // -- overlapped
        case  128: cpu->e=_z80_inc8(cpu,cpu->e);goto fetch_next;
        
        //  1D: DEC E (M:1 T:4)
        // -- overlapped
        case  129: cpu->e=_z80_dec8(cpu,cpu->e);goto fetch_next;
        
        //  1E: LD E,n (M:2 T:7)
        // -- mread
        case  130: goto step_next;
        case  131: _wait();_mread(cpu->pc++);goto step_next;
        case  132: cpu->e=_gd();goto step_next;
        // -- overlapped
        case  133: goto fetch_next;
        
        //  1F: RRA (M:1 T:4)
        // -- overlapped
        case  134: _z80_rra(cpu);goto fetch_next;
        
        //  20: JR NZ,d (M:3 T:12)
        // -- mread
        case  135: goto step_next;
        case  136: _wait();_mread(cpu->pc++);goto step_next;
        case  137: cpu->dlatch=_gd();if(!(_cc_nz)){_skip(5);};goto step_next;
        // -- generic
        case  138: cpu->pc+=(int8_t)cpu->dlatch;cpu->wz=cpu->pc;goto step_next;
        case  139: goto step_next;
        case  140: goto step_next;
        case  141: goto step_next;
        case  142: goto step_next;
        // -- overlapped
        case  143: goto fetch_next;
        
        //  21: LD HL,nn (M:3 T:10)
        // -- mread
        case  144: goto step_next;
        case  145: _wait();_mread(cpu->pc++);goto step_next;
        case  146: cpu->hlx[cpu->hlx_idx].l=_gd();goto step_next;
        // -- mread
        case  147: goto step_next;
        case  148: _wait();_mread(cpu->pc++);goto step_next;
        case  149: cpu->hlx[cpu->hlx_idx].h=_gd();goto step_next;
        // -- overlapped
        case  150: goto fetch_next;
        
        //  22: LD (nn),HL (M:5 T:16)
        // -- mread
        case  151: goto step_next;
        case  152: _wait();_mread(cpu->pc++);goto step_next;
        case  153: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  154: goto step_next;
        case  155: _wait();_mread(cpu->pc++);goto step_next;
        case  156: cpu->wzh=_gd();goto step_next;
        // -- mwrite
        case  157: goto step_next;
        case  158: _wait();_mwrite(cpu->wz++,cpu->hlx[cpu->hlx_idx].l);goto step_next;
        case  159: goto step_next;
        // -- mwrite
        case  160: goto step_next;
        case  161: _wait();_mwrite(cpu->wz,cpu->hlx[cpu->hlx_idx].h);goto step_next;
        case  162: goto step_next;
        // -- overlapped
        case  163: goto fetch_next;
        
        //  23: INC HL (M:2 T:6)
        // -- generic
        case  164: cpu->hlx[cpu->hlx_idx].hl++;goto step_next;
        case  165: goto step_next;
        // -- overlapped
        case  166: goto fetch_next;
        
        //  24: INC H (M:1 T:4)
        // -- overlapped
        case  167: cpu->hlx[cpu->hlx_idx].h=_z80_inc8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  25: DEC H (M:1 T:4)
        // -- overlapped
        case  168: cpu->hlx[cpu->hlx_idx].h=_z80_dec8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  26: LD H,n (M:2 T:7)
        // -- mread
        case  169: goto step_next;
        case  170: _wait();_mread(cpu->pc++);goto step_next;
        case  171: cpu->hlx[cpu->hlx_idx].h=_gd();goto step_next;
        // -- overlapped
        case  172: goto fetch_next;
        
        //  27: DAA (M:1 T:4)
        // -- overlapped
        case  173: _z80_daa(cpu);goto fetch_next;
        
        //  28: JR Z,d (M:3 T:12)
        // -- mread
        case  174: goto step_next;
        case  175: _wait();_mread(cpu->pc++);goto step_next;
        case  176: cpu->dlatch=_gd();if(!(_cc_z)){_skip(5);};goto step_next;
        // -- generic
        case  177: cpu->pc+=(int8_t)cpu->dlatch;cpu->wz=cpu->pc;goto step_next;
        case  178: goto step_next;
        case  179: goto step_next;
        case  180: goto step_next;
        case  181: goto step_next;
        // -- overlapped
        case  182: goto fetch_next;
        
        //  29: ADD HL,HL (M:2 T:11)
        // -- generic
        case  183: _z80_add16(cpu,cpu->hlx[cpu->hlx_idx].hl);goto step_next;
        case  184: goto step_next;
        case  185: goto step_next;
        case  186: goto step_next;
        case  187: goto step_next;
        case  188: goto step_next;
        case  189: goto step_next;
        // -- overlapped
        case  190: goto fetch_next;
        
        //  2A: LD HL,(nn) (M:5 T:16)
        // -- mread
        case  191: goto step_next;
        case  192: _wait();_mread(cpu->pc++);goto step_next;
        case  193: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  194: goto step_next;
        case  195: _wait();_mread(cpu->pc++);goto step_next;
        case  196: cpu->wzh=_gd();goto step_next;
        // -- mread
        case  197: goto step_next;
        case  198: _wait();_mread(cpu->wz++);goto step_next;
        case  199: cpu->hlx[cpu->hlx_idx].l=_gd();goto step_next;
        // -- mread
        case  200: goto step_next;
        case  201: _wait();_mread(cpu->wz);goto step_next;
        case  202: cpu->hlx[cpu->hlx_idx].h=_gd();goto step_next;
        // -- overlapped
        case  203: goto fetch_next;
        
        //  2B: DEC HL (M:2 T:6)
        // -- generic
        case  204: cpu->hlx[cpu->hlx_idx].hl--;goto step_next;
        case  205: goto step_next;
        // -- overlapped
        case  206: goto fetch_next;
        
        //  2C: INC L (M:1 T:4)
        // -- overlapped
        case  207: cpu->hlx[cpu->hlx_idx].l=_z80_inc8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  2D: DEC L (M:1 T:4)
        // -- overlapped
        case  208: cpu->hlx[cpu->hlx_idx].l=_z80_dec8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  2E: LD L,n (M:2 T:7)
        // -- mread
        case  209: goto step_next;
        case  210: _wait();_mread(cpu->pc++);goto step_next;
        case  211: cpu->hlx[cpu->hlx_idx].l=_gd();goto step_next;
        // -- overlapped
        case  212: goto fetch_next;
        
        //  2F: CPL (M:1 T:4)
        // -- overlapped
        case  213: _z80_cpl(cpu);goto fetch_next;
        
        //  30: JR NC,d (M:3 T:12)
        // -- mread
        case  214: goto step_next;
        case  215: _wait();_mread(cpu->pc++);goto step_next;
        case  216: cpu->dlatch=_gd();if(!(_cc_nc)){_skip(5);};goto step_next;
        // -- generic
        case  217: cpu->pc+=(int8_t)cpu->dlatch;cpu->wz=cpu->pc;goto step_next;
        case  218: goto step_next;
        case  219: goto step_next;
        case  220: goto step_next;
        case  221: goto step_next;
        // -- overlapped
        case  222: goto fetch_next;
        
        //  31: LD SP,nn (M:3 T:10)
        // -- mread
        case  223: goto step_next;
        case  224: _wait();_mread(cpu->pc++);goto step_next;
        case  225: cpu->spl=_gd();goto step_next;
        // -- mread
        case  226: goto step_next;
        case  227: _wait();_mread(cpu->pc++);goto step_next;
        case  228: cpu->sph=_gd();goto step_next;
        // -- overlapped
        case  229: goto fetch_next;
        
        //  32: LD (nn),A (M:4 T:13)
        // -- mread
        case  230: goto step_next;
        case  231: _wait();_mread(cpu->pc++);goto step_next;
        case  232: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  233: goto step_next;
        case  234: _wait();_mread(cpu->pc++);goto step_next;
        case  235: cpu->wzh=_gd();goto step_next;
        // -- mwrite
        case  236: goto step_next;
        case  237: _wait();_mwrite(cpu->wz++,cpu->a);cpu->wzh=cpu->a;goto step_next;
        case  238: goto step_next;
        // -- overlapped
        case  239: goto fetch_next;
        
        //  33: INC SP (M:2 T:6)
        // -- generic
        case  240: cpu->sp++;goto step_next;
        case  241: goto step_next;
        // -- overlapped
        case  242: goto fetch_next;
        
        //  34: INC (HL) (M:3 T:11)
        // -- mread
        case  243: goto step_next;
        case  244: _wait();_mread(cpu->addr);goto step_next;
        case  245: cpu->dlatch=_gd();cpu->dlatch=_z80_inc8(cpu,cpu->dlatch);goto step_next;
        case  246: goto step_next;
        // -- mwrite
        case  247: goto step_next;
        case  248: _wait();_mwrite(cpu->addr,cpu->dlatch);goto step_next;
        case  249: goto step_next;
        // -- overlapped
        case  250: goto fetch_next;
        
        //  35: DEC (HL) (M:3 T:11)
        // -- mread
        case  251: goto step_next;
        case  252: _wait();_mread(cpu->addr);goto step_next;
        case  253: cpu->dlatch=_gd();cpu->dlatch=_z80_dec8(cpu,cpu->dlatch);goto step_next;
        case  254: goto step_next;
        // -- mwrite
        case  255: goto step_next;
        case  256: _wait();_mwrite(cpu->addr,cpu->dlatch);goto step_next;
        case  257: goto step_next;
        // -- overlapped
        case  258: goto fetch_next;
        
        //  36: LD (HL),n (M:3 T:10)
        // -- mread
        case  259: goto step_next;
        case  260: _wait();_mread(cpu->pc++);goto step_next;
        case  261: cpu->dlatch=_gd();goto step_next;
        // -- mwrite
        case  262: goto step_next;
        case  263: _wait();_mwrite(cpu->addr,cpu->dlatch);goto step_next;
        case  264: goto step_next;
        // -- overlapped
        case  265: goto fetch_next;
        
        //  37: SCF (M:1 T:4)
        // -- overlapped
        case  266: _z80_scf(cpu);goto fetch_next;
        
        //  38: JR C,d (M:3 T:12)
        // -- mread
        case  267: goto step_next;
        case  268: _wait();_mread(cpu->pc++);goto step_next;
        case  269: cpu->dlatch=_gd();if(!(_cc_c)){_skip(5);};goto step_next;
        // -- generic
        case  270: cpu->pc+=(int8_t)cpu->dlatch;cpu->wz=cpu->pc;goto step_next;
        case  271: goto step_next;
        case  272: goto step_next;
        case  273: goto step_next;
        case  274: goto step_next;
        // -- overlapped
        case  275: goto fetch_next;
        
        //  39: ADD HL,SP (M:2 T:11)
        // -- generic
        case  276: _z80_add16(cpu,cpu->sp);goto step_next;
        case  277: goto step_next;
        case  278: goto step_next;
        case  279: goto step_next;
        case  280: goto step_next;
        case  281: goto step_next;
        case  282: goto step_next;
        // -- overlapped
        case  283: goto fetch_next;
        
        //  3A: LD A,(nn) (M:4 T:13)
        // -- mread
        case  284: goto step_next;
        case  285: _wait();_mread(cpu->pc++);goto step_next;
        case  286: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  287: goto step_next;
        case  288: _wait();_mread(cpu->pc++);goto step_next;
        case  289: cpu->wzh=_gd();goto step_next;
        // -- mread
        case  290: goto step_next;
        case  291: _wait();_mread(cpu->wz++);goto step_next;
        case  292: cpu->a=_gd();goto step_next;
        // -- overlapped
        case  293: goto fetch_next;
        
        //  3B: DEC SP (M:2 T:6)
        // -- generic
        case  294: cpu->sp--;goto step_next;
        case  295: goto step_next;
        // -- overlapped
        case  296: goto fetch_next;
        
        //  3C: INC A (M:1 T:4)
        // -- overlapped
        case  297: cpu->a=_z80_inc8(cpu,cpu->a);goto fetch_next;
        
        //  3D: DEC A (M:1 T:4)
        // -- overlapped
        case  298: cpu->a=_z80_dec8(cpu,cpu->a);goto fetch_next;
        
        //  3E: LD A,n (M:2 T:7)
        // -- mread
        case  299: goto step_next;
        case  300: _wait();_mread(cpu->pc++);goto step_next;
        case  301: cpu->a=_gd();goto step_next;
        // -- overlapped
        case  302: goto fetch_next;
        
        //  3F: CCF (M:1 T:4)
        // -- overlapped
        case  303: _z80_ccf(cpu);goto fetch_next;
        
        //  40: LD B,B (M:1 T:4)
        // -- overlapped
        case  304: cpu->b=cpu->b;goto fetch_next;
        
        //  41: LD B,C (M:1 T:4)
        // -- overlapped
        case  305: cpu->b=cpu->c;goto fetch_next;
        
        //  42: LD B,D (M:1 T:4)
        // -- overlapped
        case  306: cpu->b=cpu->d;goto fetch_next;
        
        //  43: LD B,E (M:1 T:4)
        // -- overlapped
        case  307: cpu->b=cpu->e;goto fetch_next;
        
        //  44: LD B,H (M:1 T:4)
        // -- overlapped
        case  308: cpu->b=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  45: LD B,L (M:1 T:4)
        // -- overlapped
        case  309: cpu->b=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  46: LD B,(HL) (M:2 T:7)
        // -- mread
        case  310: goto step_next;
        case  311: _wait();_mread(cpu->addr);goto step_next;
        case  312: cpu->b=_gd();goto step_next;
        // -- overlapped
        case  313: goto fetch_next;
        
        //  47: LD B,A (M:1 T:4)
        // -- overlapped
        case  314: cpu->b=cpu->a;goto fetch_next;
        
        //  48: LD C,B (M:1 T:4)
        // -- overlapped
        case  315: cpu->c=cpu->b;goto fetch_next;
        
        //  49: LD C,C (M:1 T:4)
        // -- overlapped
        case  316: cpu->c=cpu->c;goto fetch_next;
        
        //  4A: LD C,D (M:1 T:4)
        // -- overlapped
        case  317: cpu->c=cpu->d;goto fetch_next;
        
        //  4B: LD C,E (M:1 T:4)
        // -- overlapped
        case  318: cpu->c=cpu->e;goto fetch_next;
        
        //  4C: LD C,H (M:1 T:4)
        // -- overlapped
        case  319: cpu->c=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  4D: LD C,L (M:1 T:4)
        // -- overlapped
        case  320: cpu->c=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  4E: LD C,(HL) (M:2 T:7)
        // -- mread
        case  321: goto step_next;
        case  322: _wait();_mread(cpu->addr);goto step_next;
        case  323: cpu->c=_gd();goto step_next;
        // -- overlapped
        case  324: goto fetch_next;
        
        //  4F: LD C,A (M:1 T:4)
        // -- overlapped
        case  325: cpu->c=cpu->a;goto fetch_next;
        
        //  50: LD D,B (M:1 T:4)
        // -- overlapped
        case  326: cpu->d=cpu->b;goto fetch_next;
        
        //  51: LD D,C (M:1 T:4)
        // -- overlapped
        case  327: cpu->d=cpu->c;goto fetch_next;
        
        //  52: LD D,D (M:1 T:4)
        // -- overlapped
        case  328: cpu->d=cpu->d;goto fetch_next;
        
        //  53: LD D,E (M:1 T:4)
        // -- overlapped
        case  329: cpu->d=cpu->e;goto fetch_next;
        
        //  54: LD D,H (M:1 T:4)
        // -- overlapped
        case  330: cpu->d=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  55: LD D,L (M:1 T:4)
        // -- overlapped
        case  331: cpu->d=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  56: LD D,(HL) (M:2 T:7)
        // -- mread
        case  332: goto step_next;
        case  333: _wait();_mread(cpu->addr);goto step_next;
        case  334: cpu->d=_gd();goto step_next;
        // -- overlapped
        case  335: goto fetch_next;
        
        //  57: LD D,A (M:1 T:4)
        // -- overlapped
        case  336: cpu->d=cpu->a;goto fetch_next;
        
        //  58: LD E,B (M:1 T:4)
        // -- overlapped
        case  337: cpu->e=cpu->b;goto fetch_next;
        
        //  59: LD E,C (M:1 T:4)
        // -- overlapped
        case  338: cpu->e=cpu->c;goto fetch_next;
        
        //  5A: LD E,D (M:1 T:4)
        // -- overlapped
        case  339: cpu->e=cpu->d;goto fetch_next;
        
        //  5B: LD E,E (M:1 T:4)
        // -- overlapped
        case  340: cpu->e=cpu->e;goto fetch_next;
        
        //  5C: LD E,H (M:1 T:4)
        // -- overlapped
        case  341: cpu->e=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  5D: LD E,L (M:1 T:4)
        // -- overlapped
        case  342: cpu->e=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  5E: LD E,(HL) (M:2 T:7)
        // -- mread
        case  343: goto step_next;
        case  344: _wait();_mread(cpu->addr);goto step_next;
        case  345: cpu->e=_gd();goto step_next;
        // -- overlapped
        case  346: goto fetch_next;
        
        //  5F: LD E,A (M:1 T:4)
        // -- overlapped
        case  347: cpu->e=cpu->a;goto fetch_next;
        
        //  60: LD H,B (M:1 T:4)
        // -- overlapped
        case  348: cpu->hlx[cpu->hlx_idx].h=cpu->b;goto fetch_next;
        
        //  61: LD H,C (M:1 T:4)
        // -- overlapped
        case  349: cpu->hlx[cpu->hlx_idx].h=cpu->c;goto fetch_next;
        
        //  62: LD H,D (M:1 T:4)
        // -- overlapped
        case  350: cpu->hlx[cpu->hlx_idx].h=cpu->d;goto fetch_next;
        
        //  63: LD H,E (M:1 T:4)
        // -- overlapped
        case  351: cpu->hlx[cpu->hlx_idx].h=cpu->e;goto fetch_next;
        
        //  64: LD H,H (M:1 T:4)
        // -- overlapped
        case  352: cpu->hlx[cpu->hlx_idx].h=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  65: LD H,L (M:1 T:4)
        // -- overlapped
        case  353: cpu->hlx[cpu->hlx_idx].h=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  66: LD H,(HL) (M:2 T:7)
        // -- mread
        case  354: goto step_next;
        case  355: _wait();_mread(cpu->addr);goto step_next;
        case  356: cpu->h=_gd();goto step_next;
        // -- overlapped
        case  357: goto fetch_next;
        
        //  67: LD H,A (M:1 T:4)
        // -- overlapped
        case  358: cpu->hlx[cpu->hlx_idx].h=cpu->a;goto fetch_next;
        
        //  68: LD L,B (M:1 T:4)
        // -- overlapped
        case  359: cpu->hlx[cpu->hlx_idx].l=cpu->b;goto fetch_next;
        
        //  69: LD L,C (M:1 T:4)
        // -- overlapped
        case  360: cpu->hlx[cpu->hlx_idx].l=cpu->c;goto fetch_next;
        
        //  6A: LD L,D (M:1 T:4)
        // -- overlapped
        case  361: cpu->hlx[cpu->hlx_idx].l=cpu->d;goto fetch_next;
        
        //  6B: LD L,E (M:1 T:4)
        // -- overlapped
        case  362: cpu->hlx[cpu->hlx_idx].l=cpu->e;goto fetch_next;
        
        //  6C: LD L,H (M:1 T:4)
        // -- overlapped
        case  363: cpu->hlx[cpu->hlx_idx].l=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  6D: LD L,L (M:1 T:4)
        // -- overlapped
        case  364: cpu->hlx[cpu->hlx_idx].l=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  6E: LD L,(HL) (M:2 T:7)
        // -- mread
        case  365: goto step_next;
        case  366: _wait();_mread(cpu->addr);goto step_next;
        case  367: cpu->l=_gd();goto step_next;
        // -- overlapped
        case  368: goto fetch_next;
        
        //  6F: LD L,A (M:1 T:4)
        // -- overlapped
        case  369: cpu->hlx[cpu->hlx_idx].l=cpu->a;goto fetch_next;
        
        //  70: LD (HL),B (M:2 T:7)
        // -- mwrite
        case  370: goto step_next;
        case  371: _wait();_mwrite(cpu->addr,cpu->b);goto step_next;
        case  372: goto step_next;
        // -- overlapped
        case  373: goto fetch_next;
        
        //  71: LD (HL),C (M:2 T:7)
        // -- mwrite
        case  374: goto step_next;
        case  375: _wait();_mwrite(cpu->addr,cpu->c);goto step_next;
        case  376: goto step_next;
        // -- overlapped
        case  377: goto fetch_next;
        
        //  72: LD (HL),D (M:2 T:7)
        // -- mwrite
        case  378: goto step_next;
        case  379: _wait();_mwrite(cpu->addr,cpu->d);goto step_next;
        case  380: goto step_next;
        // -- overlapped
        case  381: goto fetch_next;
        
        //  73: LD (HL),E (M:2 T:7)
        // -- mwrite
        case  382: goto step_next;
        case  383: _wait();_mwrite(cpu->addr,cpu->e);goto step_next;
        case  384: goto step_next;
        // -- overlapped
        case  385: goto fetch_next;
        
        //  74: LD (HL),H (M:2 T:7)
        // -- mwrite
        case  386: goto step_next;
        case  387: _wait();_mwrite(cpu->addr,cpu->h);goto step_next;
        case  388: goto step_next;
        // -- overlapped
        case  389: goto fetch_next;
        
        //  75: LD (HL),L (M:2 T:7)
        // -- mwrite
        case  390: goto step_next;
        case  391: _wait();_mwrite(cpu->addr,cpu->l);goto step_next;
        case  392: goto step_next;
        // -- overlapped
        case  393: goto fetch_next;
        
        //  76: HALT (M:1 T:4)
        // -- overlapped
        case  394: pins=_z80_halt(cpu,pins);goto fetch_next;
        
        //  77: LD (HL),A (M:2 T:7)
        // -- mwrite
        case  395: goto step_next;
        case  396: _wait();_mwrite(cpu->addr,cpu->a);goto step_next;
        case  397: goto step_next;
        // -- overlapped
        case  398: goto fetch_next;
        
        //  78: LD A,B (M:1 T:4)
        // -- overlapped
        case  399: cpu->a=cpu->b;goto fetch_next;
        
        //  79: LD A,C (M:1 T:4)
        // -- overlapped
        case  400: cpu->a=cpu->c;goto fetch_next;
        
        //  7A: LD A,D (M:1 T:4)
        // -- overlapped
        case  401: cpu->a=cpu->d;goto fetch_next;
        
        //  7B: LD A,E (M:1 T:4)
        // -- overlapped
        case  402: cpu->a=cpu->e;goto fetch_next;
        
        //  7C: LD A,H (M:1 T:4)
        // -- overlapped
        case  403: cpu->a=cpu->hlx[cpu->hlx_idx].h;goto fetch_next;
        
        //  7D: LD A,L (M:1 T:4)
        // -- overlapped
        case  404: cpu->a=cpu->hlx[cpu->hlx_idx].l;goto fetch_next;
        
        //  7E: LD A,(HL) (M:2 T:7)
        // -- mread
        case  405: goto step_next;
        case  406: _wait();_mread(cpu->addr);goto step_next;
        case  407: cpu->a=_gd();goto step_next;
        // -- overlapped
        case  408: goto fetch_next;
        
        //  7F: LD A,A (M:1 T:4)
        // -- overlapped
        case  409: cpu->a=cpu->a;goto fetch_next;
        
        //  80: ADD B (M:1 T:4)
        // -- overlapped
        case  410: _z80_add8(cpu,cpu->b);goto fetch_next;
        
        //  81: ADD C (M:1 T:4)
        // -- overlapped
        case  411: _z80_add8(cpu,cpu->c);goto fetch_next;
        
        //  82: ADD D (M:1 T:4)
        // -- overlapped
        case  412: _z80_add8(cpu,cpu->d);goto fetch_next;
        
        //  83: ADD E (M:1 T:4)
        // -- overlapped
        case  413: _z80_add8(cpu,cpu->e);goto fetch_next;
        
        //  84: ADD H (M:1 T:4)
        // -- overlapped
        case  414: _z80_add8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  85: ADD L (M:1 T:4)
        // -- overlapped
        case  415: _z80_add8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  86: ADD (HL) (M:2 T:7)
        // -- mread
        case  416: goto step_next;
        case  417: _wait();_mread(cpu->addr);goto step_next;
        case  418: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  419: _z80_add8(cpu,cpu->dlatch);goto fetch_next;
        
        //  87: ADD A (M:1 T:4)
        // -- overlapped
        case  420: _z80_add8(cpu,cpu->a);goto fetch_next;
        
        //  88: ADC B (M:1 T:4)
        // -- overlapped
        case  421: _z80_adc8(cpu,cpu->b);goto fetch_next;
        
        //  89: ADC C (M:1 T:4)
        // -- overlapped
        case  422: _z80_adc8(cpu,cpu->c);goto fetch_next;
        
        //  8A: ADC D (M:1 T:4)
        // -- overlapped
        case  423: _z80_adc8(cpu,cpu->d);goto fetch_next;
        
        //  8B: ADC E (M:1 T:4)
        // -- overlapped
        case  424: _z80_adc8(cpu,cpu->e);goto fetch_next;
        
        //  8C: ADC H (M:1 T:4)
        // -- overlapped
        case  425: _z80_adc8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  8D: ADC L (M:1 T:4)
        // -- overlapped
        case  426: _z80_adc8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  8E: ADC (HL) (M:2 T:7)
        // -- mread
        case  427: goto step_next;
        case  428: _wait();_mread(cpu->addr);goto step_next;
        case  429: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  430: _z80_adc8(cpu,cpu->dlatch);goto fetch_next;
        
        //  8F: ADC A (M:1 T:4)
        // -- overlapped
        case  431: _z80_adc8(cpu,cpu->a);goto fetch_next;
        
        //  90: SUB B (M:1 T:4)
        // -- overlapped
        case  432: _z80_sub8(cpu,cpu->b);goto fetch_next;
        
        //  91: SUB C (M:1 T:4)
        // -- overlapped
        case  433: _z80_sub8(cpu,cpu->c);goto fetch_next;
        
        //  92: SUB D (M:1 T:4)
        // -- overlapped
        case  434: _z80_sub8(cpu,cpu->d);goto fetch_next;
        
        //  93: SUB E (M:1 T:4)
        // -- overlapped
        case  435: _z80_sub8(cpu,cpu->e);goto fetch_next;
        
        //  94: SUB H (M:1 T:4)
        // -- overlapped
        case  436: _z80_sub8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  95: SUB L (M:1 T:4)
        // -- overlapped
        case  437: _z80_sub8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  96: SUB (HL) (M:2 T:7)
        // -- mread
        case  438: goto step_next;
        case  439: _wait();_mread(cpu->addr);goto step_next;
        case  440: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  441: _z80_sub8(cpu,cpu->dlatch);goto fetch_next;
        
        //  97: SUB A (M:1 T:4)
        // -- overlapped
        case  442: _z80_sub8(cpu,cpu->a);goto fetch_next;
        
        //  98: SBC B (M:1 T:4)
        // -- overlapped
        case  443: _z80_sbc8(cpu,cpu->b);goto fetch_next;
        
        //  99: SBC C (M:1 T:4)
        // -- overlapped
        case  444: _z80_sbc8(cpu,cpu->c);goto fetch_next;
        
        //  9A: SBC D (M:1 T:4)
        // -- overlapped
        case  445: _z80_sbc8(cpu,cpu->d);goto fetch_next;
        
        //  9B: SBC E (M:1 T:4)
        // -- overlapped
        case  446: _z80_sbc8(cpu,cpu->e);goto fetch_next;
        
        //  9C: SBC H (M:1 T:4)
        // -- overlapped
        case  447: _z80_sbc8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  9D: SBC L (M:1 T:4)
        // -- overlapped
        case  448: _z80_sbc8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  9E: SBC (HL) (M:2 T:7)
        // -- mread
        case  449: goto step_next;
        case  450: _wait();_mread(cpu->addr);goto step_next;
        case  451: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  452: _z80_sbc8(cpu,cpu->dlatch);goto fetch_next;
        
        //  9F: SBC A (M:1 T:4)
        // -- overlapped
        case  453: _z80_sbc8(cpu,cpu->a);goto fetch_next;
        
        //  A0: AND B (M:1 T:4)
        // -- overlapped
        case  454: _z80_and8(cpu,cpu->b);goto fetch_next;
        
        //  A1: AND C (M:1 T:4)
        // -- overlapped
        case  455: _z80_and8(cpu,cpu->c);goto fetch_next;
        
        //  A2: AND D (M:1 T:4)
        // -- overlapped
        case  456: _z80_and8(cpu,cpu->d);goto fetch_next;
        
        //  A3: AND E (M:1 T:4)
        // -- overlapped
        case  457: _z80_and8(cpu,cpu->e);goto fetch_next;
        
        //  A4: AND H (M:1 T:4)
        // -- overlapped
        case  458: _z80_and8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  A5: AND L (M:1 T:4)
        // -- overlapped
        case  459: _z80_and8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  A6: AND (HL) (M:2 T:7)
        // -- mread
        case  460: goto step_next;
        case  461: _wait();_mread(cpu->addr);goto step_next;
        case  462: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  463: _z80_and8(cpu,cpu->dlatch);goto fetch_next;
        
        //  A7: AND A (M:1 T:4)
        // -- overlapped
        case  464: _z80_and8(cpu,cpu->a);goto fetch_next;
        
        //  A8: XOR B (M:1 T:4)
        // -- overlapped
        case  465: _z80_xor8(cpu,cpu->b);goto fetch_next;
        
        //  A9: XOR C (M:1 T:4)
        // -- overlapped
        case  466: _z80_xor8(cpu,cpu->c);goto fetch_next;
        
        //  AA: XOR D (M:1 T:4)
        // -- overlapped
        case  467: _z80_xor8(cpu,cpu->d);goto fetch_next;
        
        //  AB: XOR E (M:1 T:4)
        // -- overlapped
        case  468: _z80_xor8(cpu,cpu->e);goto fetch_next;
        
        //  AC: XOR H (M:1 T:4)
        // -- overlapped
        case  469: _z80_xor8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  AD: XOR L (M:1 T:4)
        // -- overlapped
        case  470: _z80_xor8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  AE: XOR (HL) (M:2 T:7)
        // -- mread
        case  471: goto step_next;
        case  472: _wait();_mread(cpu->addr);goto step_next;
        case  473: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  474: _z80_xor8(cpu,cpu->dlatch);goto fetch_next;
        
        //  AF: XOR A (M:1 T:4)
        // -- overlapped
        case  475: _z80_xor8(cpu,cpu->a);goto fetch_next;
        
        //  B0: OR B (M:1 T:4)
        // -- overlapped
        case  476: _z80_or8(cpu,cpu->b);goto fetch_next;
        
        //  B1: OR C (M:1 T:4)
        // -- overlapped
        case  477: _z80_or8(cpu,cpu->c);goto fetch_next;
        
        //  B2: OR D (M:1 T:4)
        // -- overlapped
        case  478: _z80_or8(cpu,cpu->d);goto fetch_next;
        
        //  B3: OR E (M:1 T:4)
        // -- overlapped
        case  479: _z80_or8(cpu,cpu->e);goto fetch_next;
        
        //  B4: OR H (M:1 T:4)
        // -- overlapped
        case  480: _z80_or8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  B5: OR L (M:1 T:4)
        // -- overlapped
        case  481: _z80_or8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  B6: OR (HL) (M:2 T:7)
        // -- mread
        case  482: goto step_next;
        case  483: _wait();_mread(cpu->addr);goto step_next;
        case  484: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  485: _z80_or8(cpu,cpu->dlatch);goto fetch_next;
        
        //  B7: OR A (M:1 T:4)
        // -- overlapped
        case  486: _z80_or8(cpu,cpu->a);goto fetch_next;
        
        //  B8: CP B (M:1 T:4)
        // -- overlapped
        case  487: _z80_cp8(cpu,cpu->b);goto fetch_next;
        
        //  B9: CP C (M:1 T:4)
        // -- overlapped
        case  488: _z80_cp8(cpu,cpu->c);goto fetch_next;
        
        //  BA: CP D (M:1 T:4)
        // -- overlapped
        case  489: _z80_cp8(cpu,cpu->d);goto fetch_next;
        
        //  BB: CP E (M:1 T:4)
        // -- overlapped
        case  490: _z80_cp8(cpu,cpu->e);goto fetch_next;
        
        //  BC: CP H (M:1 T:4)
        // -- overlapped
        case  491: _z80_cp8(cpu,cpu->hlx[cpu->hlx_idx].h);goto fetch_next;
        
        //  BD: CP L (M:1 T:4)
        // -- overlapped
        case  492: _z80_cp8(cpu,cpu->hlx[cpu->hlx_idx].l);goto fetch_next;
        
        //  BE: CP (HL) (M:2 T:7)
        // -- mread
        case  493: goto step_next;
        case  494: _wait();_mread(cpu->addr);goto step_next;
        case  495: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  496: _z80_cp8(cpu,cpu->dlatch);goto fetch_next;
        
        //  BF: CP A (M:1 T:4)
        // -- overlapped
        case  497: _z80_cp8(cpu,cpu->a);goto fetch_next;
        
        //  C0: RET NZ (M:4 T:11)
        // -- generic
        case  498: if(!_cc_nz){_skip(6);};goto step_next;
        // -- mread
        case  499: goto step_next;
        case  500: _wait();_mread(cpu->sp++);goto step_next;
        case  501: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  502: goto step_next;
        case  503: _wait();_mread(cpu->sp++);goto step_next;
        case  504: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  505: goto fetch_next;
        
        //  C1: POP BC (M:3 T:10)
        // -- mread
        case  506: goto step_next;
        case  507: _wait();_mread(cpu->sp++);goto step_next;
        case  508: cpu->c=_gd();goto step_next;
        // -- mread
        case  509: goto step_next;
        case  510: _wait();_mread(cpu->sp++);goto step_next;
        case  511: cpu->b=_gd();goto step_next;
        // -- overlapped
        case  512: goto fetch_next;
        
        //  C2: JP NZ,nn (M:3 T:10)
        // -- mread
        case  513: goto step_next;
        case  514: _wait();_mread(cpu->pc++);goto step_next;
        case  515: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  516: goto step_next;
        case  517: _wait();_mread(cpu->pc++);goto step_next;
        case  518: cpu->wzh=_gd();if(_cc_nz){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  519: goto fetch_next;
        
        //  C3: JP nn (M:3 T:10)
        // -- mread
        case  520: goto step_next;
        case  521: _wait();_mread(cpu->pc++);goto step_next;
        case  522: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  523: goto step_next;
        case  524: _wait();_mread(cpu->pc++);goto step_next;
        case  525: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  526: goto fetch_next;
        
        //  C4: CALL NZ,nn (M:6 T:17)
        // -- mread
        case  527: goto step_next;
        case  528: _wait();_mread(cpu->pc++);goto step_next;
        case  529: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  530: goto step_next;
        case  531: _wait();_mread(cpu->pc++);goto step_next;
        case  532: cpu->wzh=_gd();if (!_cc_nz){_skip(7);};goto step_next;
        // -- generic
        case  533: goto step_next;
        // -- mwrite
        case  534: goto step_next;
        case  535: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  536: goto step_next;
        // -- mwrite
        case  537: goto step_next;
        case  538: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  539: goto step_next;
        // -- overlapped
        case  540: goto fetch_next;
        
        //  C5: PUSH BC (M:4 T:11)
        // -- generic
        case  541: goto step_next;
        // -- mwrite
        case  542: goto step_next;
        case  543: _wait();_mwrite(--cpu->sp,cpu->b);goto step_next;
        case  544: goto step_next;
        // -- mwrite
        case  545: goto step_next;
        case  546: _wait();_mwrite(--cpu->sp,cpu->c);goto step_next;
        case  547: goto step_next;
        // -- overlapped
        case  548: goto fetch_next;
        
        //  C6: ADD n (M:2 T:7)
        // -- mread
        case  549: goto step_next;
        case  550: _wait();_mread(cpu->pc++);goto step_next;
        case  551: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  552: _z80_add8(cpu,cpu->dlatch);goto fetch_next;
        
        //  C7: RST 0h (M:4 T:11)
        // -- generic
        case  553: goto step_next;
        // -- mwrite
        case  554: goto step_next;
        case  555: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  556: goto step_next;
        // -- mwrite
        case  557: goto step_next;
        case  558: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x00;cpu->pc=cpu->wz;goto step_next;
        case  559: goto step_next;
        // -- overlapped
        case  560: goto fetch_next;
        
        //  C8: RET Z (M:4 T:11)
        // -- generic
        case  561: if(!_cc_z){_skip(6);};goto step_next;
        // -- mread
        case  562: goto step_next;
        case  563: _wait();_mread(cpu->sp++);goto step_next;
        case  564: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  565: goto step_next;
        case  566: _wait();_mread(cpu->sp++);goto step_next;
        case  567: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  568: goto fetch_next;
        
        //  C9: RET (M:3 T:10)
        // -- mread
        case  569: goto step_next;
        case  570: _wait();_mread(cpu->sp++);goto step_next;
        case  571: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  572: goto step_next;
        case  573: _wait();_mread(cpu->sp++);goto step_next;
        case  574: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  575: goto fetch_next;
        
        //  CA: JP Z,nn (M:3 T:10)
        // -- mread
        case  576: goto step_next;
        case  577: _wait();_mread(cpu->pc++);goto step_next;
        case  578: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  579: goto step_next;
        case  580: _wait();_mread(cpu->pc++);goto step_next;
        case  581: cpu->wzh=_gd();if(_cc_z){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  582: goto fetch_next;
        
        //  CB: CB prefix (M:1 T:4)
        // -- overlapped
        case  583: _fetch_cb();goto step_next;
        
        //  CC: CALL Z,nn (M:6 T:17)
        // -- mread
        case  584: goto step_next;
        case  585: _wait();_mread(cpu->pc++);goto step_next;
        case  586: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  587: goto step_next;
        case  588: _wait();_mread(cpu->pc++);goto step_next;
        case  589: cpu->wzh=_gd();if (!_cc_z){_skip(7);};goto step_next;
        // -- generic
        case  590: goto step_next;
        // -- mwrite
        case  591: goto step_next;
        case  592: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  593: goto step_next;
        // -- mwrite
        case  594: goto step_next;
        case  595: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  596: goto step_next;
        // -- overlapped
        case  597: goto fetch_next;
        
        //  CD: CALL nn (M:5 T:17)
        // -- mread
        case  598: goto step_next;
        case  599: _wait();_mread(cpu->pc++);goto step_next;
        case  600: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  601: goto step_next;
        case  602: _wait();_mread(cpu->pc++);goto step_next;
        case  603: cpu->wzh=_gd();goto step_next;
        case  604: goto step_next;
        // -- mwrite
        case  605: goto step_next;
        case  606: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  607: goto step_next;
        // -- mwrite
        case  608: goto step_next;
        case  609: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  610: goto step_next;
        // -- overlapped
        case  611: goto fetch_next;
        
        //  CE: ADC n (M:2 T:7)
        // -- mread
        case  612: goto step_next;
        case  613: _wait();_mread(cpu->pc++);goto step_next;
        case  614: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  615: _z80_adc8(cpu,cpu->dlatch);goto fetch_next;
        
        //  CF: RST 8h (M:4 T:11)
        // -- generic
        case  616: goto step_next;
        // -- mwrite
        case  617: goto step_next;
        case  618: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  619: goto step_next;
        // -- mwrite
        case  620: goto step_next;
        case  621: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x08;cpu->pc=cpu->wz;goto step_next;
        case  622: goto step_next;
        // -- overlapped
        case  623: goto fetch_next;
        
        //  D0: RET NC (M:4 T:11)
        // -- generic
        case  624: if(!_cc_nc){_skip(6);};goto step_next;
        // -- mread
        case  625: goto step_next;
        case  626: _wait();_mread(cpu->sp++);goto step_next;
        case  627: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  628: goto step_next;
        case  629: _wait();_mread(cpu->sp++);goto step_next;
        case  630: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  631: goto fetch_next;
        
        //  D1: POP DE (M:3 T:10)
        // -- mread
        case  632: goto step_next;
        case  633: _wait();_mread(cpu->sp++);goto step_next;
        case  634: cpu->e=_gd();goto step_next;
        // -- mread
        case  635: goto step_next;
        case  636: _wait();_mread(cpu->sp++);goto step_next;
        case  637: cpu->d=_gd();goto step_next;
        // -- overlapped
        case  638: goto fetch_next;
        
        //  D2: JP NC,nn (M:3 T:10)
        // -- mread
        case  639: goto step_next;
        case  640: _wait();_mread(cpu->pc++);goto step_next;
        case  641: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  642: goto step_next;
        case  643: _wait();_mread(cpu->pc++);goto step_next;
        case  644: cpu->wzh=_gd();if(_cc_nc){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  645: goto fetch_next;
        
        //  D3: OUT (n),A (M:3 T:11)
        // -- mread
        case  646: goto step_next;
        case  647: _wait();_mread(cpu->pc++);goto step_next;
        case  648: cpu->wzl=_gd();cpu->wzh=cpu->a;goto step_next;
        // -- iowrite
        case  649: goto step_next;
        case  650: _iowrite(cpu->wz,cpu->a);goto step_next;
        case  651: _wait();cpu->wzl++;goto step_next;
        case  652: goto step_next;
        // -- overlapped
        case  653: goto fetch_next;
        
        //  D4: CALL NC,nn (M:6 T:17)
        // -- mread
        case  654: goto step_next;
        case  655: _wait();_mread(cpu->pc++);goto step_next;
        case  656: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  657: goto step_next;
        case  658: _wait();_mread(cpu->pc++);goto step_next;
        case  659: cpu->wzh=_gd();if (!_cc_nc){_skip(7);};goto step_next;
        // -- generic
        case  660: goto step_next;
        // -- mwrite
        case  661: goto step_next;
        case  662: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  663: goto step_next;
        // -- mwrite
        case  664: goto step_next;
        case  665: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  666: goto step_next;
        // -- overlapped
        case  667: goto fetch_next;
        
        //  D5: PUSH DE (M:4 T:11)
        // -- generic
        case  668: goto step_next;
        // -- mwrite
        case  669: goto step_next;
        case  670: _wait();_mwrite(--cpu->sp,cpu->d);goto step_next;
        case  671: goto step_next;
        // -- mwrite
        case  672: goto step_next;
        case  673: _wait();_mwrite(--cpu->sp,cpu->e);goto step_next;
        case  674: goto step_next;
        // -- overlapped
        case  675: goto fetch_next;
        
        //  D6: SUB n (M:2 T:7)
        // -- mread
        case  676: goto step_next;
        case  677: _wait();_mread(cpu->pc++);goto step_next;
        case  678: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  679: _z80_sub8(cpu,cpu->dlatch);goto fetch_next;
        
        //  D7: RST 10h (M:4 T:11)
        // -- generic
        case  680: goto step_next;
        // -- mwrite
        case  681: goto step_next;
        case  682: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  683: goto step_next;
        // -- mwrite
        case  684: goto step_next;
        case  685: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x10;cpu->pc=cpu->wz;goto step_next;
        case  686: goto step_next;
        // -- overlapped
        case  687: goto fetch_next;
        
        //  D8: RET C (M:4 T:11)
        // -- generic
        case  688: if(!_cc_c){_skip(6);};goto step_next;
        // -- mread
        case  689: goto step_next;
        case  690: _wait();_mread(cpu->sp++);goto step_next;
        case  691: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  692: goto step_next;
        case  693: _wait();_mread(cpu->sp++);goto step_next;
        case  694: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  695: goto fetch_next;
        
        //  D9: EXX (M:1 T:4)
        // -- overlapped
        case  696: _z80_exx(cpu);goto fetch_next;
        
        //  DA: JP C,nn (M:3 T:10)
        // -- mread
        case  697: goto step_next;
        case  698: _wait();_mread(cpu->pc++);goto step_next;
        case  699: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  700: goto step_next;
        case  701: _wait();_mread(cpu->pc++);goto step_next;
        case  702: cpu->wzh=_gd();if(_cc_c){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  703: goto fetch_next;
        
        //  DB: IN A,(n) (M:3 T:11)
        // -- mread
        case  704: goto step_next;
        case  705: _wait();_mread(cpu->pc++);goto step_next;
        case  706: cpu->wzl=_gd();cpu->wzh=cpu->a;goto step_next;
        // -- ioread
        case  707: goto step_next;
        case  708: goto step_next;
        case  709: _wait();_ioread(cpu->wz++);goto step_next;
        case  710: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  711: cpu->a=cpu->dlatch;goto fetch_next;
        
        //  DC: CALL C,nn (M:6 T:17)
        // -- mread
        case  712: goto step_next;
        case  713: _wait();_mread(cpu->pc++);goto step_next;
        case  714: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  715: goto step_next;
        case  716: _wait();_mread(cpu->pc++);goto step_next;
        case  717: cpu->wzh=_gd();if (!_cc_c){_skip(7);};goto step_next;
        // -- generic
        case  718: goto step_next;
        // -- mwrite
        case  719: goto step_next;
        case  720: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  721: goto step_next;
        // -- mwrite
        case  722: goto step_next;
        case  723: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  724: goto step_next;
        // -- overlapped
        case  725: goto fetch_next;
        
        //  DD: DD prefix (M:1 T:4)
        // -- overlapped
        case  726: _fetch_dd();goto step_next;
        
        //  DE: SBC n (M:2 T:7)
        // -- mread
        case  727: goto step_next;
        case  728: _wait();_mread(cpu->pc++);goto step_next;
        case  729: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  730: _z80_sbc8(cpu,cpu->dlatch);goto fetch_next;
        
        //  DF: RST 18h (M:4 T:11)
        // -- generic
        case  731: goto step_next;
        // -- mwrite
        case  732: goto step_next;
        case  733: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  734: goto step_next;
        // -- mwrite
        case  735: goto step_next;
        case  736: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x18;cpu->pc=cpu->wz;goto step_next;
        case  737: goto step_next;
        // -- overlapped
        case  738: goto fetch_next;
        
        //  E0: RET PO (M:4 T:11)
        // -- generic
        case  739: if(!_cc_po){_skip(6);};goto step_next;
        // -- mread
        case  740: goto step_next;
        case  741: _wait();_mread(cpu->sp++);goto step_next;
        case  742: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  743: goto step_next;
        case  744: _wait();_mread(cpu->sp++);goto step_next;
        case  745: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  746: goto fetch_next;
        
        //  E1: POP HL (M:3 T:10)
        // -- mread
        case  747: goto step_next;
        case  748: _wait();_mread(cpu->sp++);goto step_next;
        case  749: cpu->hlx[cpu->hlx_idx].l=_gd();goto step_next;
        // -- mread
        case  750: goto step_next;
        case  751: _wait();_mread(cpu->sp++);goto step_next;
        case  752: cpu->hlx[cpu->hlx_idx].h=_gd();goto step_next;
        // -- overlapped
        case  753: goto fetch_next;
        
        //  E2: JP PO,nn (M:3 T:10)
        // -- mread
        case  754: goto step_next;
        case  755: _wait();_mread(cpu->pc++);goto step_next;
        case  756: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  757: goto step_next;
        case  758: _wait();_mread(cpu->pc++);goto step_next;
        case  759: cpu->wzh=_gd();if(_cc_po){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  760: goto fetch_next;
        
        //  E3: EX (SP),HL (M:5 T:19)
        // -- mread
        case  761: goto step_next;
        case  762: _wait();_mread(cpu->sp);goto step_next;
        case  763: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  764: goto step_next;
        case  765: _wait();_mread(cpu->sp+1);goto step_next;
        case  766: cpu->wzh=_gd();goto step_next;
        case  767: goto step_next;
        // -- mwrite
        case  768: goto step_next;
        case  769: _wait();_mwrite(cpu->sp+1,cpu->hlx[cpu->hlx_idx].h);goto step_next;
        case  770: goto step_next;
        // -- mwrite
        case  771: goto step_next;
        case  772: _wait();_mwrite(cpu->sp,cpu->hlx[cpu->hlx_idx].l);cpu->hlx[cpu->hlx_idx].hl=cpu->wz;goto step_next;
        case  773: goto step_next;
        case  774: goto step_next;
        case  775: goto step_next;
        // -- overlapped
        case  776: goto fetch_next;
        
        //  E4: CALL PO,nn (M:6 T:17)
        // -- mread
        case  777: goto step_next;
        case  778: _wait();_mread(cpu->pc++);goto step_next;
        case  779: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  780: goto step_next;
        case  781: _wait();_mread(cpu->pc++);goto step_next;
        case  782: cpu->wzh=_gd();if (!_cc_po){_skip(7);};goto step_next;
        // -- generic
        case  783: goto step_next;
        // -- mwrite
        case  784: goto step_next;
        case  785: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  786: goto step_next;
        // -- mwrite
        case  787: goto step_next;
        case  788: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  789: goto step_next;
        // -- overlapped
        case  790: goto fetch_next;
        
        //  E5: PUSH HL (M:4 T:11)
        // -- generic
        case  791: goto step_next;
        // -- mwrite
        case  792: goto step_next;
        case  793: _wait();_mwrite(--cpu->sp,cpu->hlx[cpu->hlx_idx].h);goto step_next;
        case  794: goto step_next;
        // -- mwrite
        case  795: goto step_next;
        case  796: _wait();_mwrite(--cpu->sp,cpu->hlx[cpu->hlx_idx].l);goto step_next;
        case  797: goto step_next;
        // -- overlapped
        case  798: goto fetch_next;
        
        //  E6: AND n (M:2 T:7)
        // -- mread
        case  799: goto step_next;
        case  800: _wait();_mread(cpu->pc++);goto step_next;
        case  801: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  802: _z80_and8(cpu,cpu->dlatch);goto fetch_next;
        
        //  E7: RST 20h (M:4 T:11)
        // -- generic
        case  803: goto step_next;
        // -- mwrite
        case  804: goto step_next;
        case  805: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  806: goto step_next;
        // -- mwrite
        case  807: goto step_next;
        case  808: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x20;cpu->pc=cpu->wz;goto step_next;
        case  809: goto step_next;
        // -- overlapped
        case  810: goto fetch_next;
        
        //  E8: RET PE (M:4 T:11)
        // -- generic
        case  811: if(!_cc_pe){_skip(6);};goto step_next;
        // -- mread
        case  812: goto step_next;
        case  813: _wait();_mread(cpu->sp++);goto step_next;
        case  814: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  815: goto step_next;
        case  816: _wait();_mread(cpu->sp++);goto step_next;
        case  817: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  818: goto fetch_next;
        
        //  E9: JP HL (M:1 T:4)
        // -- overlapped
        case  819: cpu->pc=cpu->hlx[cpu->hlx_idx].hl;goto fetch_next;
        
        //  EA: JP PE,nn (M:3 T:10)
        // -- mread
        case  820: goto step_next;
        case  821: _wait();_mread(cpu->pc++);goto step_next;
        case  822: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  823: goto step_next;
        case  824: _wait();_mread(cpu->pc++);goto step_next;
        case  825: cpu->wzh=_gd();if(_cc_pe){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  826: goto fetch_next;
        
        //  EB: EX DE,HL (M:1 T:4)
        // -- overlapped
        case  827: _z80_ex_de_hl(cpu);goto fetch_next;
        
        //  EC: CALL PE,nn (M:6 T:17)
        // -- mread
        case  828: goto step_next;
        case  829: _wait();_mread(cpu->pc++);goto step_next;
        case  830: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  831: goto step_next;
        case  832: _wait();_mread(cpu->pc++);goto step_next;
        case  833: cpu->wzh=_gd();if (!_cc_pe){_skip(7);};goto step_next;
        // -- generic
        case  834: goto step_next;
        // -- mwrite
        case  835: goto step_next;
        case  836: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  837: goto step_next;
        // -- mwrite
        case  838: goto step_next;
        case  839: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  840: goto step_next;
        // -- overlapped
        case  841: goto fetch_next;
        
        //  ED: ED prefix (M:1 T:4)
        // -- overlapped
        case  842: _fetch_ed();goto step_next;
        
        //  EE: XOR n (M:2 T:7)
        // -- mread
        case  843: goto step_next;
        case  844: _wait();_mread(cpu->pc++);goto step_next;
        case  845: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  846: _z80_xor8(cpu,cpu->dlatch);goto fetch_next;
        
        //  EF: RST 28h (M:4 T:11)
        // -- generic
        case  847: goto step_next;
        // -- mwrite
        case  848: goto step_next;
        case  849: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  850: goto step_next;
        // -- mwrite
        case  851: goto step_next;
        case  852: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x28;cpu->pc=cpu->wz;goto step_next;
        case  853: goto step_next;
        // -- overlapped
        case  854: goto fetch_next;
        
        //  F0: RET P (M:4 T:11)
        // -- generic
        case  855: if(!_cc_p){_skip(6);};goto step_next;
        // -- mread
        case  856: goto step_next;
        case  857: _wait();_mread(cpu->sp++);goto step_next;
        case  858: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  859: goto step_next;
        case  860: _wait();_mread(cpu->sp++);goto step_next;
        case  861: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  862: goto fetch_next;
        
        //  F1: POP AF (M:3 T:10)
        // -- mread
        case  863: goto step_next;
        case  864: _wait();_mread(cpu->sp++);goto step_next;
        case  865: cpu->f=_gd();goto step_next;
        // -- mread
        case  866: goto step_next;
        case  867: _wait();_mread(cpu->sp++);goto step_next;
        case  868: cpu->a=_gd();goto step_next;
        // -- overlapped
        case  869: goto fetch_next;
        
        //  F2: JP P,nn (M:3 T:10)
        // -- mread
        case  870: goto step_next;
        case  871: _wait();_mread(cpu->pc++);goto step_next;
        case  872: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  873: goto step_next;
        case  874: _wait();_mread(cpu->pc++);goto step_next;
        case  875: cpu->wzh=_gd();if(_cc_p){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  876: goto fetch_next;
        
        //  F3: DI (M:1 T:4)
        // -- overlapped
        case  877: cpu->iff1=cpu->iff2=false;goto fetch_next;
        
        //  F4: CALL P,nn (M:6 T:17)
        // -- mread
        case  878: goto step_next;
        case  879: _wait();_mread(cpu->pc++);goto step_next;
        case  880: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  881: goto step_next;
        case  882: _wait();_mread(cpu->pc++);goto step_next;
        case  883: cpu->wzh=_gd();if (!_cc_p){_skip(7);};goto step_next;
        // -- generic
        case  884: goto step_next;
        // -- mwrite
        case  885: goto step_next;
        case  886: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  887: goto step_next;
        // -- mwrite
        case  888: goto step_next;
        case  889: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  890: goto step_next;
        // -- overlapped
        case  891: goto fetch_next;
        
        //  F5: PUSH AF (M:4 T:11)
        // -- generic
        case  892: goto step_next;
        // -- mwrite
        case  893: goto step_next;
        case  894: _wait();_mwrite(--cpu->sp,cpu->a);goto step_next;
        case  895: goto step_next;
        // -- mwrite
        case  896: goto step_next;
        case  897: _wait();_mwrite(--cpu->sp,cpu->f);goto step_next;
        case  898: goto step_next;
        // -- overlapped
        case  899: goto fetch_next;
        
        //  F6: OR n (M:2 T:7)
        // -- mread
        case  900: goto step_next;
        case  901: _wait();_mread(cpu->pc++);goto step_next;
        case  902: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  903: _z80_or8(cpu,cpu->dlatch);goto fetch_next;
        
        //  F7: RST 30h (M:4 T:11)
        // -- generic
        case  904: goto step_next;
        // -- mwrite
        case  905: goto step_next;
        case  906: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  907: goto step_next;
        // -- mwrite
        case  908: goto step_next;
        case  909: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x30;cpu->pc=cpu->wz;goto step_next;
        case  910: goto step_next;
        // -- overlapped
        case  911: goto fetch_next;
        
        //  F8: RET M (M:4 T:11)
        // -- generic
        case  912: if(!_cc_m){_skip(6);};goto step_next;
        // -- mread
        case  913: goto step_next;
        case  914: _wait();_mread(cpu->sp++);goto step_next;
        case  915: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  916: goto step_next;
        case  917: _wait();_mread(cpu->sp++);goto step_next;
        case  918: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  919: goto fetch_next;
        
        //  F9: LD SP,HL (M:2 T:6)
        // -- generic
        case  920: cpu->sp=cpu->hlx[cpu->hlx_idx].hl;goto step_next;
        case  921: goto step_next;
        // -- overlapped
        case  922: goto fetch_next;
        
        //  FA: JP M,nn (M:3 T:10)
        // -- mread
        case  923: goto step_next;
        case  924: _wait();_mread(cpu->pc++);goto step_next;
        case  925: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  926: goto step_next;
        case  927: _wait();_mread(cpu->pc++);goto step_next;
        case  928: cpu->wzh=_gd();if(_cc_m){cpu->pc=cpu->wz;};goto step_next;
        // -- overlapped
        case  929: goto fetch_next;
        
        //  FB: EI (M:1 T:4)
        // -- overlapped
        case  930: cpu->iff1=cpu->iff2=false;pins=_z80_fetch(cpu,pins);cpu->iff1=cpu->iff2=true;goto step_next;
        
        //  FC: CALL M,nn (M:6 T:17)
        // -- mread
        case  931: goto step_next;
        case  932: _wait();_mread(cpu->pc++);goto step_next;
        case  933: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  934: goto step_next;
        case  935: _wait();_mread(cpu->pc++);goto step_next;
        case  936: cpu->wzh=_gd();if (!_cc_m){_skip(7);};goto step_next;
        // -- generic
        case  937: goto step_next;
        // -- mwrite
        case  938: goto step_next;
        case  939: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  940: goto step_next;
        // -- mwrite
        case  941: goto step_next;
        case  942: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->pc=cpu->wz;goto step_next;
        case  943: goto step_next;
        // -- overlapped
        case  944: goto fetch_next;
        
        //  FD: FD prefix (M:1 T:4)
        // -- overlapped
        case  945: _fetch_fd();goto step_next;
        
        //  FE: CP n (M:2 T:7)
        // -- mread
        case  946: goto step_next;
        case  947: _wait();_mread(cpu->pc++);goto step_next;
        case  948: cpu->dlatch=_gd();goto step_next;
        // -- overlapped
        case  949: _z80_cp8(cpu,cpu->dlatch);goto fetch_next;
        
        //  FF: RST 38h (M:4 T:11)
        // -- generic
        case  950: goto step_next;
        // -- mwrite
        case  951: goto step_next;
        case  952: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case  953: goto step_next;
        // -- mwrite
        case  954: goto step_next;
        case  955: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=0x38;cpu->pc=cpu->wz;goto step_next;
        case  956: goto step_next;
        // -- overlapped
        case  957: goto fetch_next;
        
        // ED 00: ED NOP (M:1 T:4)
        // -- overlapped
        case  958: goto fetch_next;
        
        // ED 40: IN B,(C) (M:2 T:8)
        // -- ioread
        case  959: goto step_next;
        case  960: goto step_next;
        case  961: _wait();_ioread(cpu->bc);goto step_next;
        case  962: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case  963: cpu->b=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 41: OUT (C),B (M:2 T:8)
        // -- iowrite
        case  964: goto step_next;
        case  965: _iowrite(cpu->bc,cpu->b);goto step_next;
        case  966: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case  967: goto step_next;
        // -- overlapped
        case  968: goto fetch_next;
        
        // ED 42: SBC HL,BC (M:2 T:11)
        // -- generic
        case  969: _z80_sbc16(cpu,cpu->bc);goto step_next;
        case  970: goto step_next;
        case  971: goto step_next;
        case  972: goto step_next;
        case  973: goto step_next;
        case  974: goto step_next;
        case  975: goto step_next;
        // -- overlapped
        case  976: goto fetch_next;
        
        // ED 43: LD (nn),BC (M:5 T:16)
        // -- mread
        case  977: goto step_next;
        case  978: _wait();_mread(cpu->pc++);goto step_next;
        case  979: cpu->wzl=_gd();goto step_next;
        // -- mread
        case  980: goto step_next;
        case  981: _wait();_mread(cpu->pc++);goto step_next;
        case  982: cpu->wzh=_gd();goto step_next;
        // -- mwrite
        case  983: goto step_next;
        case  984: _wait();_mwrite(cpu->wz++,cpu->c);goto step_next;
        case  985: goto step_next;
        // -- mwrite
        case  986: goto step_next;
        case  987: _wait();_mwrite(cpu->wz,cpu->b);goto step_next;
        case  988: goto step_next;
        // -- overlapped
        case  989: goto fetch_next;
        
        // ED 44: NEG (M:1 T:4)
        // -- overlapped
        case  990: _z80_neg8(cpu);goto fetch_next;
        
        // ED 45: RETI/RETN (M:3 T:10)
        // -- mread
        case  991: goto step_next;
        case  992: _wait();_mread(cpu->sp++);goto step_next;
        case  993: cpu->wzl=_gd();pins|=Z80_RETI;goto step_next;
        // -- mread
        case  994: goto step_next;
        case  995: _wait();_mread(cpu->sp++);goto step_next;
        case  996: cpu->wzh=_gd();cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case  997: pins=_z80_fetch(cpu,pins);cpu->iff1=cpu->iff2;goto step_next;
        
        // ED 46: IM 0 (M:1 T:4)
        // -- overlapped
        case  998: cpu->im=0;goto fetch_next;
        
        // ED 47: LD I,A (M:2 T:5)
        // -- generic
        case  999: goto step_next;
        // -- overlapped
        case 1000: cpu->i=cpu->a;goto fetch_next;
        
        // ED 48: IN C,(C) (M:2 T:8)
        // -- ioread
        case 1001: goto step_next;
        case 1002: goto step_next;
        case 1003: _wait();_ioread(cpu->bc);goto step_next;
        case 1004: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1005: cpu->c=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 49: OUT (C),C (M:2 T:8)
        // -- iowrite
        case 1006: goto step_next;
        case 1007: _iowrite(cpu->bc,cpu->c);goto step_next;
        case 1008: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1009: goto step_next;
        // -- overlapped
        case 1010: goto fetch_next;
        
        // ED 4A: ADC HL,BC (M:2 T:11)
        // -- generic
        case 1011: _z80_adc16(cpu,cpu->bc);goto step_next;
        case 1012: goto step_next;
        case 1013: goto step_next;
        case 1014: goto step_next;
        case 1015: goto step_next;
        case 1016: goto step_next;
        case 1017: goto step_next;
        // -- overlapped
        case 1018: goto fetch_next;
        
        // ED 4B: LD BC,(nn) (M:5 T:16)
        // -- mread
        case 1019: goto step_next;
        case 1020: _wait();_mread(cpu->pc++);goto step_next;
        case 1021: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1022: goto step_next;
        case 1023: _wait();_mread(cpu->pc++);goto step_next;
        case 1024: cpu->wzh=_gd();goto step_next;
        // -- mread
        case 1025: goto step_next;
        case 1026: _wait();_mread(cpu->wz++);goto step_next;
        case 1027: cpu->c=_gd();goto step_next;
        // -- mread
        case 1028: goto step_next;
        case 1029: _wait();_mread(cpu->wz);goto step_next;
        case 1030: cpu->b=_gd();goto step_next;
        // -- overlapped
        case 1031: goto fetch_next;
        
        // ED 4E: IM 0 (M:1 T:4)
        // -- overlapped
        case 1032: cpu->im=0;goto fetch_next;
        
        // ED 4F: LD R,A (M:2 T:5)
        // -- generic
        case 1033: goto step_next;
        // -- overlapped
        case 1034: cpu->r=cpu->a;goto fetch_next;
        
        // ED 50: IN D,(C) (M:2 T:8)
        // -- ioread
        case 1035: goto step_next;
        case 1036: goto step_next;
        case 1037: _wait();_ioread(cpu->bc);goto step_next;
        case 1038: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1039: cpu->d=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 51: OUT (C),D (M:2 T:8)
        // -- iowrite
        case 1040: goto step_next;
        case 1041: _iowrite(cpu->bc,cpu->d);goto step_next;
        case 1042: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1043: goto step_next;
        // -- overlapped
        case 1044: goto fetch_next;
        
        // ED 52: SBC HL,DE (M:2 T:11)
        // -- generic
        case 1045: _z80_sbc16(cpu,cpu->de);goto step_next;
        case 1046: goto step_next;
        case 1047: goto step_next;
        case 1048: goto step_next;
        case 1049: goto step_next;
        case 1050: goto step_next;
        case 1051: goto step_next;
        // -- overlapped
        case 1052: goto fetch_next;
        
        // ED 53: LD (nn),DE (M:5 T:16)
        // -- mread
        case 1053: goto step_next;
        case 1054: _wait();_mread(cpu->pc++);goto step_next;
        case 1055: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1056: goto step_next;
        case 1057: _wait();_mread(cpu->pc++);goto step_next;
        case 1058: cpu->wzh=_gd();goto step_next;
        // -- mwrite
        case 1059: goto step_next;
        case 1060: _wait();_mwrite(cpu->wz++,cpu->e);goto step_next;
        case 1061: goto step_next;
        // -- mwrite
        case 1062: goto step_next;
        case 1063: _wait();_mwrite(cpu->wz,cpu->d);goto step_next;
        case 1064: goto step_next;
        // -- overlapped
        case 1065: goto fetch_next;
        
        // ED 56: IM 1 (M:1 T:4)
        // -- overlapped
        case 1066: cpu->im=1;goto fetch_next;
        
        // ED 57: LD A,I (M:2 T:5)
        // -- generic
        case 1067: goto step_next;
        // -- overlapped
        case 1068: cpu->a=cpu->i;cpu->f=_z80_sziff2_flags(cpu, cpu->i);goto fetch_next;
        
        // ED 58: IN E,(C) (M:2 T:8)
        // -- ioread
        case 1069: goto step_next;
        case 1070: goto step_next;
        case 1071: _wait();_ioread(cpu->bc);goto step_next;
        case 1072: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1073: cpu->e=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 59: OUT (C),E (M:2 T:8)
        // -- iowrite
        case 1074: goto step_next;
        case 1075: _iowrite(cpu->bc,cpu->e);goto step_next;
        case 1076: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1077: goto step_next;
        // -- overlapped
        case 1078: goto fetch_next;
        
        // ED 5A: ADC HL,DE (M:2 T:11)
        // -- generic
        case 1079: _z80_adc16(cpu,cpu->de);goto step_next;
        case 1080: goto step_next;
        case 1081: goto step_next;
        case 1082: goto step_next;
        case 1083: goto step_next;
        case 1084: goto step_next;
        case 1085: goto step_next;
        // -- overlapped
        case 1086: goto fetch_next;
        
        // ED 5B: LD DE,(nn) (M:5 T:16)
        // -- mread
        case 1087: goto step_next;
        case 1088: _wait();_mread(cpu->pc++);goto step_next;
        case 1089: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1090: goto step_next;
        case 1091: _wait();_mread(cpu->pc++);goto step_next;
        case 1092: cpu->wzh=_gd();goto step_next;
        // -- mread
        case 1093: goto step_next;
        case 1094: _wait();_mread(cpu->wz++);goto step_next;
        case 1095: cpu->e=_gd();goto step_next;
        // -- mread
        case 1096: goto step_next;
        case 1097: _wait();_mread(cpu->wz);goto step_next;
        case 1098: cpu->d=_gd();goto step_next;
        // -- overlapped
        case 1099: goto fetch_next;
        
        // ED 5E: IM 2 (M:1 T:4)
        // -- overlapped
        case 1100: cpu->im=2;goto fetch_next;
        
        // ED 5F: LD A,R (M:2 T:5)
        // -- generic
        case 1101: goto step_next;
        // -- overlapped
        case 1102: cpu->a=cpu->r;cpu->f=_z80_sziff2_flags(cpu, cpu->r);goto fetch_next;
        
        // ED 60: IN H,(C) (M:2 T:8)
        // -- ioread
        case 1103: goto step_next;
        case 1104: goto step_next;
        case 1105: _wait();_ioread(cpu->bc);goto step_next;
        case 1106: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1107: cpu->hlx[cpu->hlx_idx].h=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 61: OUT (C),H (M:2 T:8)
        // -- iowrite
        case 1108: goto step_next;
        case 1109: _iowrite(cpu->bc,cpu->hlx[cpu->hlx_idx].h);goto step_next;
        case 1110: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1111: goto step_next;
        // -- overlapped
        case 1112: goto fetch_next;
        
        // ED 62: SBC HL,HL (M:2 T:11)
        // -- generic
        case 1113: _z80_sbc16(cpu,cpu->hl);goto step_next;
        case 1114: goto step_next;
        case 1115: goto step_next;
        case 1116: goto step_next;
        case 1117: goto step_next;
        case 1118: goto step_next;
        case 1119: goto step_next;
        // -- overlapped
        case 1120: goto fetch_next;
        
        // ED 63: LD (nn),HL (M:5 T:16)
        // -- mread
        case 1121: goto step_next;
        case 1122: _wait();_mread(cpu->pc++);goto step_next;
        case 1123: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1124: goto step_next;
        case 1125: _wait();_mread(cpu->pc++);goto step_next;
        case 1126: cpu->wzh=_gd();goto step_next;
        // -- mwrite
        case 1127: goto step_next;
        case 1128: _wait();_mwrite(cpu->wz++,cpu->l);goto step_next;
        case 1129: goto step_next;
        // -- mwrite
        case 1130: goto step_next;
        case 1131: _wait();_mwrite(cpu->wz,cpu->h);goto step_next;
        case 1132: goto step_next;
        // -- overlapped
        case 1133: goto fetch_next;
        
        // ED 66: IM 0 (M:1 T:4)
        // -- overlapped
        case 1134: cpu->im=0;goto fetch_next;
        
        // ED 67: RRD (M:4 T:14)
        // -- mread
        case 1135: goto step_next;
        case 1136: _wait();_mread(cpu->hl);goto step_next;
        case 1137: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case 1138: cpu->dlatch=_z80_rrd(cpu,cpu->dlatch);goto step_next;
        case 1139: goto step_next;
        case 1140: goto step_next;
        case 1141: goto step_next;
        // -- mwrite
        case 1142: goto step_next;
        case 1143: _wait();_mwrite(cpu->hl,cpu->dlatch);cpu->wz=cpu->hl+1;goto step_next;
        case 1144: goto step_next;
        // -- overlapped
        case 1145: goto fetch_next;
        
        // ED 68: IN L,(C) (M:2 T:8)
        // -- ioread
        case 1146: goto step_next;
        case 1147: goto step_next;
        case 1148: _wait();_ioread(cpu->bc);goto step_next;
        case 1149: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1150: cpu->hlx[cpu->hlx_idx].l=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 69: OUT (C),L (M:2 T:8)
        // -- iowrite
        case 1151: goto step_next;
        case 1152: _iowrite(cpu->bc,cpu->hlx[cpu->hlx_idx].l);goto step_next;
        case 1153: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1154: goto step_next;
        // -- overlapped
        case 1155: goto fetch_next;
        
        // ED 6A: ADC HL,HL (M:2 T:11)
        // -- generic
        case 1156: _z80_adc16(cpu,cpu->hl);goto step_next;
        case 1157: goto step_next;
        case 1158: goto step_next;
        case 1159: goto step_next;
        case 1160: goto step_next;
        case 1161: goto step_next;
        case 1162: goto step_next;
        // -- overlapped
        case 1163: goto fetch_next;
        
        // ED 6B: LD HL,(nn) (M:5 T:16)
        // -- mread
        case 1164: goto step_next;
        case 1165: _wait();_mread(cpu->pc++);goto step_next;
        case 1166: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1167: goto step_next;
        case 1168: _wait();_mread(cpu->pc++);goto step_next;
        case 1169: cpu->wzh=_gd();goto step_next;
        // -- mread
        case 1170: goto step_next;
        case 1171: _wait();_mread(cpu->wz++);goto step_next;
        case 1172: cpu->l=_gd();goto step_next;
        // -- mread
        case 1173: goto step_next;
        case 1174: _wait();_mread(cpu->wz);goto step_next;
        case 1175: cpu->h=_gd();goto step_next;
        // -- overlapped
        case 1176: goto fetch_next;
        
        // ED 6E: IM 0 (M:1 T:4)
        // -- overlapped
        case 1177: cpu->im=0;goto fetch_next;
        
        // ED 6F: RLD (M:4 T:14)
        // -- mread
        case 1178: goto step_next;
        case 1179: _wait();_mread(cpu->hl);goto step_next;
        case 1180: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case 1181: cpu->dlatch=_z80_rld(cpu,cpu->dlatch);goto step_next;
        case 1182: goto step_next;
        case 1183: goto step_next;
        case 1184: goto step_next;
        // -- mwrite
        case 1185: goto step_next;
        case 1186: _wait();_mwrite(cpu->hl,cpu->dlatch);cpu->wz=cpu->hl+1;goto step_next;
        case 1187: goto step_next;
        // -- overlapped
        case 1188: goto fetch_next;
        
        // ED 70: IN (C) (M:2 T:8)
        // -- ioread
        case 1189: goto step_next;
        case 1190: goto step_next;
        case 1191: _wait();_ioread(cpu->bc);goto step_next;
        case 1192: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1193: _z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 71: OUT (C),0 (M:2 T:8)
        // -- iowrite
        case 1194: goto step_next;
        case 1195: _iowrite(cpu->bc,0);goto step_next;
        case 1196: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1197: goto step_next;
        // -- overlapped
        case 1198: goto fetch_next;
        
        // ED 72: SBC HL,SP (M:2 T:11)
        // -- generic
        case 1199: _z80_sbc16(cpu,cpu->sp);goto step_next;
        case 1200: goto step_next;
        case 1201: goto step_next;
        case 1202: goto step_next;
        case 1203: goto step_next;
        case 1204: goto step_next;
        case 1205: goto step_next;
        // -- overlapped
        case 1206: goto fetch_next;
        
        // ED 73: LD (nn),SP (M:5 T:16)
        // -- mread
        case 1207: goto step_next;
        case 1208: _wait();_mread(cpu->pc++);goto step_next;
        case 1209: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1210: goto step_next;
        case 1211: _wait();_mread(cpu->pc++);goto step_next;
        case 1212: cpu->wzh=_gd();goto step_next;
        // -- mwrite
        case 1213: goto step_next;
        case 1214: _wait();_mwrite(cpu->wz++,cpu->spl);goto step_next;
        case 1215: goto step_next;
        // -- mwrite
        case 1216: goto step_next;
        case 1217: _wait();_mwrite(cpu->wz,cpu->sph);goto step_next;
        case 1218: goto step_next;
        // -- overlapped
        case 1219: goto fetch_next;
        
        // ED 76: IM 1 (M:1 T:4)
        // -- overlapped
        case 1220: cpu->im=1;goto fetch_next;
        
        // ED 78: IN A,(C) (M:2 T:8)
        // -- ioread
        case 1221: goto step_next;
        case 1222: goto step_next;
        case 1223: _wait();_ioread(cpu->bc);goto step_next;
        case 1224: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;goto step_next;
        // -- overlapped
        case 1225: cpu->a=_z80_in(cpu,cpu->dlatch);goto fetch_next;
        
        // ED 79: OUT (C),A (M:2 T:8)
        // -- iowrite
        case 1226: goto step_next;
        case 1227: _iowrite(cpu->bc,cpu->a);goto step_next;
        case 1228: _wait();cpu->wz=cpu->bc+1;goto step_next;
        case 1229: goto step_next;
        // -- overlapped
        case 1230: goto fetch_next;
        
        // ED 7A: ADC HL,SP (M:2 T:11)
        // -- generic
        case 1231: _z80_adc16(cpu,cpu->sp);goto step_next;
        case 1232: goto step_next;
        case 1233: goto step_next;
        case 1234: goto step_next;
        case 1235: goto step_next;
        case 1236: goto step_next;
        case 1237: goto step_next;
        // -- overlapped
        case 1238: goto fetch_next;
        
        // ED 7B: LD SP,(nn) (M:5 T:16)
        // -- mread
        case 1239: goto step_next;
        case 1240: _wait();_mread(cpu->pc++);goto step_next;
        case 1241: cpu->wzl=_gd();goto step_next;
        // -- mread
        case 1242: goto step_next;
        case 1243: _wait();_mread(cpu->pc++);goto step_next;
        case 1244: cpu->wzh=_gd();goto step_next;
        // -- mread
        case 1245: goto step_next;
        case 1246: _wait();_mread(cpu->wz++);goto step_next;
        case 1247: cpu->spl=_gd();goto step_next;
        // -- mread
        case 1248: goto step_next;
        case 1249: _wait();_mread(cpu->wz);goto step_next;
        case 1250: cpu->sph=_gd();goto step_next;
        // -- overlapped
        case 1251: goto fetch_next;
        
        // ED 7E: IM 2 (M:1 T:4)
        // -- overlapped
        case 1252: cpu->im=2;goto fetch_next;
        
        // ED A0: LDI (M:4 T:12)
        // -- mread
        case 1253: goto step_next;
        case 1254: _wait();_mread(cpu->hl++);goto step_next;
        case 1255: cpu->dlatch=_gd();goto step_next;
        // -- mwrite
        case 1256: goto step_next;
        case 1257: _wait();_mwrite(cpu->de++,cpu->dlatch);goto step_next;
        case 1258: goto step_next;
        // -- generic
        case 1259: _z80_ldi_ldd(cpu,cpu->dlatch);goto step_next;
        case 1260: goto step_next;
        // -- overlapped
        case 1261: goto fetch_next;
        
        // ED A1: CPI (M:3 T:12)
        // -- mread
        case 1262: goto step_next;
        case 1263: _wait();_mread(cpu->hl++);goto step_next;
        case 1264: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case 1265: cpu->wz++;_z80_cpi_cpd(cpu,cpu->dlatch);goto step_next;
        case 1266: goto step_next;
        case 1267: goto step_next;
        case 1268: goto step_next;
        case 1269: goto step_next;
        // -- overlapped
        case 1270: goto fetch_next;
        
        // ED A2: INI (M:4 T:12)
        // -- generic
        case 1271: goto step_next;
        // -- ioread
        case 1272: goto step_next;
        case 1273: goto step_next;
        case 1274: _wait();_ioread(cpu->bc);goto step_next;
        case 1275: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;cpu->b--;;goto step_next;
        // -- mwrite
        case 1276: goto step_next;
        case 1277: _wait();_mwrite(cpu->hl++,cpu->dlatch);_z80_ini_ind(cpu,cpu->dlatch,cpu->c+1);goto step_next;
        case 1278: goto step_next;
        // -- overlapped
        case 1279: goto fetch_next;
        
        // ED A3: OUTI (M:4 T:12)
        // -- generic
        case 1280: goto step_next;
        // -- mread
        case 1281: goto step_next;
        case 1282: _wait();_mread(cpu->hl++);goto step_next;
        case 1283: cpu->dlatch=_gd();cpu->b--;goto step_next;
        // -- iowrite
        case 1284: goto step_next;
        case 1285: _iowrite(cpu->bc,cpu->dlatch);goto step_next;
        case 1286: _wait();cpu->wz=cpu->bc+1;_z80_outi_outd(cpu,cpu->dlatch);goto step_next;
        case 1287: goto step_next;
        // -- overlapped
        case 1288: goto fetch_next;
        
        // ED A8: LDD (M:4 T:12)
        // -- mread
        case 1289: goto step_next;
        case 1290: _wait();_mread(cpu->hl--);goto step_next;
        case 1291: cpu->dlatch=_gd();goto step_next;
        // -- mwrite
        case 1292: goto step_next;
        case 1293: _wait();_mwrite(cpu->de--,cpu->dlatch);goto step_next;
        case 1294: goto step_next;
        // -- generic
        case 1295: _z80_ldi_ldd(cpu,cpu->dlatch);goto step_next;
        case 1296: goto step_next;
        // -- overlapped
        case 1297: goto fetch_next;
        
        // ED A9: CPD (M:3 T:12)
        // -- mread
        case 1298: goto step_next;
        case 1299: _wait();_mread(cpu->hl--);goto step_next;
        case 1300: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case 1301: cpu->wz--;_z80_cpi_cpd(cpu,cpu->dlatch);goto step_next;
        case 1302: goto step_next;
        case 1303: goto step_next;
        case 1304: goto step_next;
        case 1305: goto step_next;
        // -- overlapped
        case 1306: goto fetch_next;
        
        // ED AA: IND (M:4 T:12)
        // -- generic
        case 1307: goto step_next;
        // -- ioread
        case 1308: goto step_next;
        case 1309: goto step_next;
        case 1310: _wait();_ioread(cpu->bc);goto step_next;
        case 1311: cpu->dlatch=_gd();cpu->wz=cpu->bc-1;cpu->b--;;goto step_next;
        // -- mwrite
        case 1312: goto step_next;
        case 1313: _wait();_mwrite(cpu->hl--,cpu->dlatch);_z80_ini_ind(cpu,cpu->dlatch,cpu->c-1);goto step_next;
        case 1314: goto step_next;
        // -- overlapped
        case 1315: goto fetch_next;
        
        // ED AB: OUTD (M:4 T:12)
        // -- generic
        case 1316: goto step_next;
        // -- mread
        case 1317: goto step_next;
        case 1318: _wait();_mread(cpu->hl--);goto step_next;
        case 1319: cpu->dlatch=_gd();cpu->b--;goto step_next;
        // -- iowrite
        case 1320: goto step_next;
        case 1321: _iowrite(cpu->bc,cpu->dlatch);goto step_next;
        case 1322: _wait();cpu->wz=cpu->bc-1;_z80_outi_outd(cpu,cpu->dlatch);goto step_next;
        case 1323: goto step_next;
        // -- overlapped
        case 1324: goto fetch_next;
        
        // ED B0: LDIR (M:5 T:17)
        // -- mread
        case 1325: goto step_next;
        case 1326: _wait();_mread(cpu->hl++);goto step_next;
        case 1327: cpu->dlatch=_gd();goto step_next;
        // -- mwrite
        case 1328: goto step_next;
        case 1329: _wait();_mwrite(cpu->de++,cpu->dlatch);goto step_next;
        case 1330: goto step_next;
        // -- generic
        case 1331: if(!_z80_ldi_ldd(cpu,cpu->dlatch)){_skip(5);};goto step_next;
        case 1332: goto step_next;
        // -- generic
        case 1333: cpu->wz=--cpu->pc;--cpu->pc;;goto step_next;
        case 1334: goto step_next;
        case 1335: goto step_next;
        case 1336: goto step_next;
        case 1337: goto step_next;
        // -- overlapped
        case 1338: goto fetch_next;
        
        // ED B1: CPIR (M:4 T:17)
        // -- mread
        case 1339: goto step_next;
        case 1340: _wait();_mread(cpu->hl++);goto step_next;
        case 1341: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case 1342: cpu->wz++;if(!_z80_cpi_cpd(cpu,cpu->dlatch)){_skip(5);};goto step_next;
        case 1343: goto step_next;
        case 1344: goto step_next;
        case 1345: goto step_next;
        case 1346: goto step_next;
        // -- generic
        case 1347: cpu->wz=--cpu->pc;--cpu->pc;goto step_next;
        case 1348: goto step_next;
        case 1349: goto step_next;
        case 1350: goto step_next;
        case 1351: goto step_next;
        // -- overlapped
        case 1352: goto fetch_next;
        
        // ED B2: INIR (M:5 T:17)
        // -- generic
        case 1353: goto step_next;
        // -- ioread
        case 1354: goto step_next;
        case 1355: goto step_next;
        case 1356: _wait();_ioread(cpu->bc);goto step_next;
        case 1357: cpu->dlatch=_gd();cpu->wz=cpu->bc+1;cpu->b--;;goto step_next;
        // -- mwrite
        case 1358: goto step_next;
        case 1359: _wait();_mwrite(cpu->hl++,cpu->dlatch);if (!_z80_ini_ind(cpu,cpu->dlatch,cpu->c+1)){_skip(5);};goto step_next;
        case 1360: goto step_next;
        // -- generic
        case 1361: cpu->wz=--cpu->pc;--cpu->pc;goto step_next;
        case 1362: goto step_next;
        case 1363: goto step_next;
        case 1364: goto step_next;
        case 1365: goto step_next;
        // -- overlapped
        case 1366: goto fetch_next;
        
        // ED B3: OTIR (M:5 T:17)
        // -- generic
        case 1367: goto step_next;
        // -- mread
        case 1368: goto step_next;
        case 1369: _wait();_mread(cpu->hl++);goto step_next;
        case 1370: cpu->dlatch=_gd();cpu->b--;goto step_next;
        // -- iowrite
        case 1371: goto step_next;
        case 1372: _iowrite(cpu->bc,cpu->dlatch);goto step_next;
        case 1373: _wait();cpu->wz=cpu->bc+1;if(!_z80_outi_outd(cpu,cpu->dlatch)){_skip(5);};goto step_next;
        case 1374: goto step_next;
        // -- generic
        case 1375: cpu->wz=--cpu->pc;--cpu->pc;goto step_next;
        case 1376: goto step_next;
        case 1377: goto step_next;
        case 1378: goto step_next;
        case 1379: goto step_next;
        // -- overlapped
        case 1380: goto fetch_next;
        
        // ED B8: LDDR (M:5 T:17)
        // -- mread
        case 1381: goto step_next;
        case 1382: _wait();_mread(cpu->hl--);goto step_next;
        case 1383: cpu->dlatch=_gd();goto step_next;
        // -- mwrite
        case 1384: goto step_next;
        case 1385: _wait();_mwrite(cpu->de--,cpu->dlatch);goto step_next;
        case 1386: goto step_next;
        // -- generic
        case 1387: if(!_z80_ldi_ldd(cpu,cpu->dlatch)){_skip(5);};goto step_next;
        case 1388: goto step_next;
        // -- generic
        case 1389: cpu->wz=--cpu->pc;--cpu->pc;;goto step_next;
        case 1390: goto step_next;
        case 1391: goto step_next;
        case 1392: goto step_next;
        case 1393: goto step_next;
        // -- overlapped
        case 1394: goto fetch_next;
        
        // ED B9: CPDR (M:4 T:17)
        // -- mread
        case 1395: goto step_next;
        case 1396: _wait();_mread(cpu->hl--);goto step_next;
        case 1397: cpu->dlatch=_gd();goto step_next;
        // -- generic
        case 1398: cpu->wz--;if(!_z80_cpi_cpd(cpu,cpu->dlatch)){_skip(5);};goto step_next;
        case 1399: goto step_next;
        case 1400: goto step_next;
        case 1401: goto step_next;
        case 1402: goto step_next;
        // -- generic
        case 1403: cpu->wz=--cpu->pc;--cpu->pc;goto step_next;
        case 1404: goto step_next;
        case 1405: goto step_next;
        case 1406: goto step_next;
        case 1407: goto step_next;
        // -- overlapped
        case 1408: goto fetch_next;
        
        // ED BA: INDR (M:5 T:17)
        // -- generic
        case 1409: goto step_next;
        // -- ioread
        case 1410: goto step_next;
        case 1411: goto step_next;
        case 1412: _wait();_ioread(cpu->bc);goto step_next;
        case 1413: cpu->dlatch=_gd();cpu->wz=cpu->bc-1;cpu->b--;;goto step_next;
        // -- mwrite
        case 1414: goto step_next;
        case 1415: _wait();_mwrite(cpu->hl--,cpu->dlatch);if (!_z80_ini_ind(cpu,cpu->dlatch,cpu->c-1)){_skip(5);};goto step_next;
        case 1416: goto step_next;
        // -- generic
        case 1417: cpu->wz=--cpu->pc;--cpu->pc;goto step_next;
        case 1418: goto step_next;
        case 1419: goto step_next;
        case 1420: goto step_next;
        case 1421: goto step_next;
        // -- overlapped
        case 1422: goto fetch_next;
        
        // ED BB: OTDR (M:5 T:17)
        // -- generic
        case 1423: goto step_next;
        // -- mread
        case 1424: goto step_next;
        case 1425: _wait();_mread(cpu->hl--);goto step_next;
        case 1426: cpu->dlatch=_gd();cpu->b--;goto step_next;
        // -- iowrite
        case 1427: goto step_next;
        case 1428: _iowrite(cpu->bc,cpu->dlatch);goto step_next;
        case 1429: _wait();cpu->wz=cpu->bc-1;if(!_z80_outi_outd(cpu,cpu->dlatch)){_skip(5);};goto step_next;
        case 1430: goto step_next;
        // -- generic
        case 1431: cpu->wz=--cpu->pc;--cpu->pc;goto step_next;
        case 1432: goto step_next;
        case 1433: goto step_next;
        case 1434: goto step_next;
        case 1435: goto step_next;
        // -- overlapped
        case 1436: goto fetch_next;
        
        // CB 00: cb (M:1 T:4)
        // -- overlapped
        case 1437: {uint8_t z=cpu->opcode&7;_z80_cb_action(cpu,z,z);};goto fetch_next;
        
        // CB 00: cbhl (M:3 T:11)
        // -- mread
        case 1438: goto step_next;
        case 1439: _wait();_mread(cpu->hl);goto step_next;
        case 1440: cpu->dlatch=_gd();if(!_z80_cb_action(cpu,6,6)){_skip(3);};goto step_next;
        case 1441: goto step_next;
        // -- mwrite
        case 1442: goto step_next;
        case 1443: _wait();_mwrite(cpu->hl,cpu->dlatch);goto step_next;
        case 1444: goto step_next;
        // -- overlapped
        case 1445: goto fetch_next;
        
        // CB 00: ddfdcb (M:6 T:18)
        // -- generic
        case 1446: _wait();_mread(cpu->pc++);goto step_next;
        // -- generic
        case 1447: _z80_ddfdcb_addr(cpu,pins);goto step_next;
        // -- mread
        case 1448: goto step_next;
        case 1449: _wait();_mread(cpu->pc++);goto step_next;
        case 1450: cpu->dlatch=_gd();_z80_ddfdcb_opcode(cpu,cpu->dlatch);goto step_next;
        case 1451: goto step_next;
        case 1452: goto step_next;
        // -- mread
        case 1453: goto step_next;
        case 1454: _wait();_mread(cpu->addr);goto step_next;
        case 1455: cpu->dlatch=_gd();if(!_z80_cb_action(cpu,6,cpu->opcode&7)){_skip(3);};goto step_next;
        case 1456: goto step_next;
        // -- mwrite
        case 1457: goto step_next;
        case 1458: _wait();_mwrite(cpu->addr,cpu->dlatch);goto step_next;
        case 1459: goto step_next;
        // -- overlapped
        case 1460: goto fetch_next;
        
        //  00: int_im0 (M:6 T:9)
        // -- generic
        case 1461: pins=_z80_int012_step0(cpu,pins);goto step_next;
        // -- generic
        case 1462: pins=_z80_int012_step1(cpu,pins);goto step_next;
        // -- generic
        case 1463: _wait();pins=_z80_int0_step2(cpu,pins);goto step_next;
        // -- generic
        case 1464: pins=_z80_refresh(cpu,pins);goto step_next;
        // -- generic
        case 1465: pins=_z80_int0_step3(cpu,pins);goto step_next;
        // -- overlapped
        case 1466: goto fetch_next;
        
        //  00: int_im1 (M:7 T:16)
        // -- generic
        case 1467: pins=_z80_int012_step0(cpu,pins);goto step_next;
        // -- generic
        case 1468: pins=_z80_int012_step1(cpu,pins);goto step_next;
        // -- generic
        case 1469: _wait();goto step_next;
        // -- generic
        case 1470: pins=_z80_refresh(cpu,pins);goto step_next;
        case 1471: goto step_next;
        case 1472: goto step_next;
        // -- mwrite
        case 1473: goto step_next;
        case 1474: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case 1475: goto step_next;
        // -- mwrite
        case 1476: goto step_next;
        case 1477: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=cpu->pc=0x0038;goto step_next;
        case 1478: goto step_next;
        // -- overlapped
        case 1479: goto fetch_next;
        
        //  00: int_im2 (M:9 T:22)
        // -- generic
        case 1480: pins=_z80_int012_step0(cpu,pins);goto step_next;
        // -- generic
        case 1481: pins=_z80_int012_step1(cpu,pins);goto step_next;
        // -- generic
        case 1482: _wait();cpu->dlatch=_z80_get_db(pins);goto step_next;
        // -- generic
        case 1483: pins=_z80_refresh(cpu,pins);goto step_next;
        case 1484: goto step_next;
        case 1485: goto step_next;
        // -- mwrite
        case 1486: goto step_next;
        case 1487: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case 1488: goto step_next;
        // -- mwrite
        case 1489: goto step_next;
        case 1490: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wzl=cpu->dlatch;cpu->wzh=cpu->i;goto step_next;
        case 1491: goto step_next;
        // -- mread
        case 1492: goto step_next;
        case 1493: _wait();_mread(cpu->wz++);goto step_next;
        case 1494: cpu->dlatch=_gd();goto step_next;
        // -- mread
        case 1495: goto step_next;
        case 1496: _wait();_mread(cpu->wz);goto step_next;
        case 1497: cpu->wzh=_gd();cpu->wzl=cpu->dlatch;cpu->pc=cpu->wz;goto step_next;
        // -- overlapped
        case 1498: goto fetch_next;
        
        //  00: nmi (M:5 T:14)
        // -- generic
        case 1499: pins=_z80_nmi_step0(cpu,pins);goto step_next;
        // -- generic
        case 1500: pins=_z80_refresh(cpu,pins);goto step_next;
        case 1501: goto step_next;
        case 1502: goto step_next;
        // -- mwrite
        case 1503: goto step_next;
        case 1504: _wait();_mwrite(--cpu->sp,cpu->pch);goto step_next;
        case 1505: goto step_next;
        // -- mwrite
        case 1506: goto step_next;
        case 1507: _wait();_mwrite(--cpu->sp,cpu->pcl);cpu->wz=cpu->pc=0x0066;goto step_next;
        case 1508: goto step_next;
        // -- overlapped
        case 1509: goto fetch_next;

        default: _Z80_UNREACHABLE;
    }
fetch_next: pins = _z80_fetch(cpu, pins);
step_next:  cpu->step += 1;
track_int_bits: {
        // track NMI 0 => 1 edge and current INT pin state, this will track the
        // relevant interrupt status up to the last instruction cycle and will
        // be checked in the first M1 cycle (during _fetch)
        const uint64_t rising_nmi = (pins ^ cpu->pins) & pins; // NMI 0 => 1
        cpu->pins = pins;
        cpu->int_bits = ((cpu->int_bits | rising_nmi) & Z80_NMI) | (pins & Z80_INT);
    }
    return pins;
}

#undef _sa
#undef _sax
#undef _sad
#undef _sadx
#undef _gd
#undef _skip
#undef _fetch_dd
#undef _fetch_fd
#undef _fetch_ed
#undef _fetch_cb
#undef _mread
#undef _mwrite
#undef _ioread
#undef _iowrite
#undef _wait
#undef _cc_nz
#undef _cc_z
#undef _cc_nc
#undef _cc_c
#undef _cc_po
#undef _cc_pe
#undef _cc_p
#undef _cc_m

#endif // CHIPS_IMPL
