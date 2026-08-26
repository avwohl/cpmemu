/*
 * Unit tests for the CPU core's 8080 mode.
 *
 * --8080 is a real feature of the emulator and nothing tested it.  zexdoc and
 * zexall cover the Z80 core - 67 instruction groups each, no CRC mismatches -
 * but they run the CPU as a Z80, and every rule that makes 8080 mode different
 * from Z80 mode was reachable by no test at all.  tests/8080EXM.COM covers the
 * same ground from the other end and is wired into run_tests.sh --zex, but it
 * takes minutes and reports a CRC; this takes under a second and names the
 * instruction.  Two of the groups below - DAA, and CMA/STC/CMC - are here
 * because that exerciser found real bugs in both.
 *
 * This links the CPU core directly rather than going through a CP/M guest, so
 * a failure names the instruction, the operands and the flag bit rather than a
 * CRC over sixty thousand cases.  Where the input space is small enough it is
 * walked exhaustively: 3.1 million ALU cases, 65536 per 16-bit increment.
 *
 * The expected values are the documented 8080 rules, written out in ref_alu8()
 * and its neighbours, not a recording of what this emulator does.  Where the
 * manual and the silicon are reported to differ, the comment on the group says
 * which one it took and why - see test_daa(), where the manual's wording is
 * wrong for six values of the accumulator.
 *
 * Build and run:
 *     c++ -std=c++11 -Wall -Wextra -I src -o unit_8080 tests/unit_8080.cc src/libqkz80.a
 *     ./unit_8080
 * `make -C src unit` and tests/run_tests.sh both do that.
 */

#include "qkz80.h"
#include "qkz80_cpu_flags.h"

#include <stdio.h>

// ============================================================================
// Reporting
//
// One line per group, and at most a handful of example failures per group: an
// exhaustive walk that goes wrong goes wrong hundreds of thousands of times,
// and a screen of them says nothing the first three do not.
// ============================================================================

static int groups_passed = 0;
static int groups_failed = 0;

struct Group {
    const char* name;
    long cases;
    long bad;
    int shown;
};

static void group_begin(Group& g, const char* name) {
    g.name = name;
    g.cases = 0;
    g.bad = 0;
    g.shown = 0;
}

static void group_end(const Group& g) {
    if (g.bad == 0) {
        printf("PASS  8080: %s (%ld cases)\n", g.name, g.cases);
        groups_passed++;
    } else {
        printf("FAIL  8080: %s (%ld of %ld cases wrong)\n", g.name, g.bad, g.cases);
        groups_failed++;
    }
}

// Name the flag bits that differ, because "got 56 want 16" is a puzzle and
// "AC" is an answer.
static void show_flag_diff(qkz80_uint8 got, qkz80_uint8 want) {
    static const struct { qkz80_uint8 bit; const char* name; } bits[] = {
        { qkz80_cpu_flags::S,  "S"  }, { qkz80_cpu_flags::Z,  "Z"  },
        { qkz80_cpu_flags::AC, "AC" }, { qkz80_cpu_flags::P,  "P"  },
        { qkz80_cpu_flags::CY, "CY" }, { qkz80_cpu_flags::UNUSED1, "bit1" },
        { qkz80_cpu_flags::UNUSED2, "bit3" }, { qkz80_cpu_flags::UNUSED3, "bit5" }
    };
    qkz80_uint8 diff = (qkz80_uint8)(got ^ want);
    printf("        flags %02X, expected %02X, differing:", got, want);
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        if (diff & bits[i].bit) printf(" %s", bits[i].name);
    }
    printf("\n");
}

// ============================================================================
// The 8080 rules, written out
// ============================================================================

static bool even_parity(unsigned v) {
    int n = 0;
    for (int i = 0; i < 8; i++) if (v & (1u << i)) n++;
    return (n & 1) == 0;
}

// S, Z, P and the two fixed bits, which every 8080 result carries.  Bit 1 of
// the flag register reads back as 1 on an 8080 and bits 3 and 5 as 0; the
// exerciser's flag mask of 0FFh depends on that being true.
static qkz80_uint8 szp(unsigned r) {
    qkz80_uint8 f = qkz80_cpu_flags::UNUSED1;
    if (r & 0x80) f |= qkz80_cpu_flags::S;
    if ((r & 0xFF) == 0) f |= qkz80_cpu_flags::Z;
    if (even_parity(r & 0xFF)) f |= qkz80_cpu_flags::P;
    return f;
}

enum AluOp { OP_ADD, OP_ADC, OP_SUB, OP_SBB, OP_ANA, OP_XRA, OP_ORA, OP_CMP };

static const char* alu_name[8] = { "ADD", "ADC", "SUB", "SBB", "ANA", "XRA", "ORA", "CMP" };

// a <op> b with carry-in cin.  Returns the flags; *acc gets the new A.
//
// Subtraction on an 8080 is an addition of the one's complement plus one, and
// both the carry and the auxiliary carry come out of that addition: the carry
// is its complement (a borrow), the auxiliary carry is not complemented.  That
// is why SUB 00h-01h leaves AC clear and not set - 0h + Fh + 1 is Fh, which
// does not carry out of bit 3.
static qkz80_uint8 ref_alu8(AluOp op, unsigned a, unsigned b, unsigned cin, unsigned* acc) {
    unsigned r, cy = 0, ac = 0;
    switch (op) {
    case OP_ADD: case OP_ADC: {
        unsigned c = (op == OP_ADC) ? cin : 0;
        unsigned s = a + b + c;
        cy = (s > 0xFF);
        ac = (((a & 0xF) + (b & 0xF) + c) > 0xF);
        r = s & 0xFF;
        break;
    }
    case OP_SUB: case OP_SBB: case OP_CMP: {
        unsigned c = (op == OP_SBB) ? cin : 0;
        unsigned nb = (~b) & 0xFF;
        unsigned s = a + nb + (1 - c);
        cy = !(s > 0xFF);
        ac = (((a & 0xF) + (nb & 0xF) + (1 - c)) > 0xF);
        r = s & 0xFF;
        break;
    }
    // ANA and ANI set the auxiliary carry from the OR of bit 3 of the two
    // operands rather than clearing it, which is the one place the 8080's
    // logical group is not simply "carry and auxiliary carry cleared".
    case OP_ANA: r = a & b; ac = (((a | b) & 0x08) != 0); break;
    case OP_XRA: r = a ^ b; break;
    default:     r = a | b; break;
    }
    qkz80_uint8 f = szp(r);
    if (ac) f |= qkz80_cpu_flags::AC;
    if (cy) f |= qkz80_cpu_flags::CY;
    *acc = (op == OP_CMP) ? a : r;      // CMP discards the result
    return f;
}

// ============================================================================
// A CPU to run one instruction on
// ============================================================================

struct Runner {
    qkz80_cpu_mem mem_obj;
    qkz80 cpu;
    qkz80_uint8* mem;

    Runner() : mem_obj(), cpu(&mem_obj), mem(NULL) {
        cpu.set_cpu_mode(qkz80::MODE_8080);
        mem = cpu.get_mem();
    }

    // Place up to three opcode bytes at 0x100 and execute exactly one
    // instruction from there.
    void step(unsigned b0, int b1 = -1, int b2 = -1) {
        mem[0x100] = (qkz80_uint8)b0;
        if (b1 >= 0) mem[0x101] = (qkz80_uint8)b1;
        if (b2 >= 0) mem[0x102] = (qkz80_uint8)b2;
        cpu.set_reg16(0x100, qkz80::regp_PC);
        cpu.execute();
    }
    qkz80_uint8 flags() { return cpu.regs.get_flags(); }
    void set_flags(qkz80_uint8 f) { cpu.regs.set_flags(f); }
};

// ============================================================================
// The flag register itself
//
// Everything below is stated in terms of a flag byte, so the byte has to mean
// what it says first.  On an 8080 bit 1 reads back as 1 and bits 3 and 5 as 0,
// whatever was written; in Z80 mode all three are real flags and must survive.
// ============================================================================

static void test_flag_register() {
    Group g;
    group_begin(g, "the flag byte forces bit 1 set and bits 3 and 5 clear");
    Runner r;
    for (unsigned v = 0; v < 256; v++) {
        g.cases++;
        r.set_flags((qkz80_uint8)v);
        qkz80_uint8 got = r.flags();
        qkz80_uint8 want = (qkz80_uint8)((v & ~(0x08 | 0x20)) | 0x02);
        if (got != want) {
            g.bad++;
            if (g.shown++ < 3) {
                printf("        wrote %02X, read back %02X, expected %02X\n", v, got, want);
            }
        }
    }
    group_end(g);

    group_begin(g, "Z80 mode leaves all eight flag bits alone");
    Runner z;
    z.cpu.set_cpu_mode(qkz80::MODE_Z80);
    for (unsigned v = 0; v < 256; v++) {
        g.cases++;
        z.set_flags((qkz80_uint8)v);
        if (z.flags() != (qkz80_uint8)v) {
            g.bad++;
            if (g.shown++ < 3) {
                printf("        wrote %02X, read back %02X\n", v, z.flags());
            }
        }
    }
    group_end(g);

    group_begin(g, "set_cpu_mode reaches the register set");
    Runner m;
    g.cases++;
    if (m.cpu.get_cpu_mode() != qkz80::MODE_8080 ||
        m.cpu.regs.cpu_mode != qkz80_reg_set::MODE_8080) {
        g.bad++;
        printf("        MODE_8080 did not reach regs.cpu_mode\n");
    }
    m.cpu.set_cpu_mode(qkz80::MODE_Z80);
    g.cases++;
    if (m.cpu.get_cpu_mode() != qkz80::MODE_Z80 ||
        m.cpu.regs.cpu_mode != qkz80_reg_set::MODE_Z80) {
        g.bad++;
        printf("        MODE_Z80 did not reach regs.cpu_mode\n");
    }
    group_end(g);
}

// ============================================================================
// The ALU group, in all three addressing forms
//
// Register, immediate and memory, because the flag rules are the instruction's
// and not the operand fetch's, and a difference between the three would be
// invisible to a test that only ever used one.
// ============================================================================

static void test_alu(const char* form_name, int base_opcode, bool immediate, bool memory) {
    Group g;
    char title[128];
    snprintf(title, sizeof title, "ALU group, %s operand", form_name);
    group_begin(g, title);

    Runner r;
    for (int op = 0; op < 8; op++) {
        for (unsigned cin = 0; cin < 2; cin++) {
            for (unsigned a = 0; a < 256; a++) {
                for (unsigned b = 0; b < 256; b++) {
                    g.cases++;
                    r.cpu.set_reg8((qkz80_uint8)a, qkz80::reg_A);
                    if (memory) {
                        r.cpu.set_reg16(0x0400, qkz80::regp_HL);
                        r.mem[0x0400] = (qkz80_uint8)b;
                    } else if (!immediate) {
                        r.cpu.set_reg8((qkz80_uint8)b, qkz80::reg_B);
                    }
                    r.set_flags((qkz80_uint8)(0x02 | (cin ? qkz80_cpu_flags::CY : 0)));
                    if (immediate) r.step((unsigned)(base_opcode + op * 8), (int)b);
                    else           r.step((unsigned)(base_opcode + op * 8));

                    unsigned want_a;
                    qkz80_uint8 want_f = ref_alu8((AluOp)op, a, b, cin, &want_a);
                    qkz80_uint8 got_f = r.flags();
                    unsigned got_a = r.cpu.get_reg8(qkz80::reg_A);
                    if (got_f != want_f || got_a != want_a) {
                        g.bad++;
                        if (g.shown++ < 4) {
                            printf("        %s %s: A=%02X operand=%02X CY-in=%u -> A=%02X, expected %02X\n",
                                   alu_name[op], form_name, a, b, cin, got_a, want_a);
                            show_flag_diff(got_f, want_f);
                        }
                    }
                }
            }
        }
    }
    group_end(g);
}

// ============================================================================
// INR and DCR
//
// The 8080 leaves the carry alone here and sets the auxiliary carry from the
// same addition every other 8-bit operation uses: INR adds 1, DCR adds FFh.
// ============================================================================

static void test_inr_dcr() {
    Group g;
    group_begin(g, "INR and DCR over all eight targets");

    // opcode 04h + target*8 for INR, 05h + target*8 for DCR, in the CPU's
    // own register order: B C D E H L M A
    static const int reg_id[8] = {
        qkz80::reg_B, qkz80::reg_C, qkz80::reg_D, qkz80::reg_E,
        qkz80::reg_H, qkz80::reg_L, -1, qkz80::reg_A
    };
    static const char* reg_name[8] = { "B", "C", "D", "E", "H", "L", "M", "A" };

    Runner r;
    for (int target = 0; target < 8; target++) {
        for (int dec = 0; dec < 2; dec++) {
            for (unsigned cin = 0; cin < 2; cin++) {
                for (unsigned v = 0; v < 256; v++) {
                    g.cases++;
                    if (target == 6) {
                        r.cpu.set_reg16(0x0400, qkz80::regp_HL);
                        r.mem[0x0400] = (qkz80_uint8)v;
                    } else {
                        r.cpu.set_reg8((qkz80_uint8)v, reg_id[target]);
                    }
                    qkz80_uint8 fin = (qkz80_uint8)(0x02 | (cin ? qkz80_cpu_flags::CY : 0));
                    r.set_flags(fin);
                    r.step((unsigned)(0x04 + target * 8 + dec));

                    unsigned addend = dec ? 0xFF : 0x01;
                    unsigned want_v = (v + addend) & 0xFF;
                    qkz80_uint8 want_f = szp(want_v);
                    if (((v & 0xF) + (addend & 0xF)) > 0xF) want_f |= qkz80_cpu_flags::AC;
                    want_f |= (qkz80_uint8)(fin & qkz80_cpu_flags::CY);   // carry untouched

                    unsigned got_v = (target == 6) ? r.mem[0x0400]
                                                   : r.cpu.get_reg8(reg_id[target]);
                    qkz80_uint8 got_f = r.flags();
                    if (got_v != want_v || got_f != want_f) {
                        g.bad++;
                        if (g.shown++ < 4) {
                            printf("        %s %s: %02X -> %02X, expected %02X\n",
                                   dec ? "DCR" : "INR", reg_name[target], v, got_v, want_v);
                            show_flag_diff(got_f, want_f);
                        }
                    }
                }
            }
        }
    }
    group_end(g);
}

// ============================================================================
// The rotates
//
// RLC, RRC, RAL and RAR touch the carry and nothing else.  On a Z80 the same
// four opcodes also clear H and N and copy bits 3 and 5 of the result, which is
// exactly the difference 8080 mode exists for.
// ============================================================================

static void test_rotates() {
    Group g;
    group_begin(g, "RLC, RRC, RAL and RAR change the carry and nothing else");
    static const char* name[4] = { "RLC", "RRC", "RAL", "RAR" };

    Runner r;
    for (int op = 0; op < 4; op++) {
        for (unsigned cin = 0; cin < 2; cin++) {
            for (unsigned a = 0; a < 256; a++) {
                g.cases++;
                r.cpu.set_reg8((qkz80_uint8)a, qkz80::reg_A);
                qkz80_uint8 fin = (qkz80_uint8)(0xD6 | (cin ? qkz80_cpu_flags::CY : 0));
                r.set_flags(fin);
                r.step((unsigned)(0x07 + op * 8));

                unsigned want_a = 0, want_cy = 0;
                switch (op) {
                case 0: want_cy = (a >> 7) & 1; want_a = ((a << 1) | want_cy) & 0xFF; break;
                case 1: want_cy = a & 1;        want_a = ((a >> 1) | (want_cy << 7)) & 0xFF; break;
                case 2: want_cy = (a >> 7) & 1; want_a = ((a << 1) | cin) & 0xFF; break;
                default:want_cy = a & 1;        want_a = ((a >> 1) | (cin << 7)) & 0xFF; break;
                }
                qkz80_uint8 want_f = (qkz80_uint8)((fin & ~qkz80_cpu_flags::CY) |
                                                   (want_cy ? qkz80_cpu_flags::CY : 0));
                qkz80_uint8 got_f = r.flags();
                unsigned got_a = r.cpu.get_reg8(qkz80::reg_A);
                if (got_a != want_a || got_f != want_f) {
                    g.bad++;
                    if (g.shown++ < 4) {
                        printf("        %s: A=%02X CY-in=%u -> A=%02X, expected %02X\n",
                               name[op], a, cin, got_a, want_a);
                        show_flag_diff(got_f, want_f);
                    }
                }
            }
        }
    }
    group_end(g);
}

// ============================================================================
// CMA, STC and CMC
// ============================================================================

static void test_cma_stc_cmc() {
    Group g;
    group_begin(g, "CMA leaves the flags alone, STC and CMC touch only the carry");
    Runner r;
    for (unsigned f = 0; f < 256; f++) {
        qkz80_uint8 fin = r.cpu.regs.fix_flags((qkz80_uint8)f);

        g.cases++;
        r.cpu.set_reg8(0x5A, qkz80::reg_A);
        r.set_flags(fin);
        r.step(0x2F);                                   // CMA
        if (r.cpu.get_reg8(qkz80::reg_A) != 0xA5 || r.flags() != fin) {
            g.bad++;
            if (g.shown++ < 4) {
                printf("        CMA: A=5A -> %02X, expected A5\n", r.cpu.get_reg8(qkz80::reg_A));
                show_flag_diff(r.flags(), fin);
            }
        }

        g.cases++;
        r.set_flags(fin);
        r.step(0x37);                                   // STC
        qkz80_uint8 want = (qkz80_uint8)(fin | qkz80_cpu_flags::CY);
        if (r.flags() != want) {
            g.bad++;
            if (g.shown++ < 4) { printf("        STC:\n"); show_flag_diff(r.flags(), want); }
        }

        g.cases++;
        r.set_flags(fin);
        r.step(0x3F);                                   // CMC
        want = (qkz80_uint8)(fin ^ qkz80_cpu_flags::CY);
        if (r.flags() != want) {
            g.bad++;
            if (g.shown++ < 4) { printf("        CMC:\n"); show_flag_diff(r.flags(), want); }
        }
    }
    group_end(g);
}

// ============================================================================
// The 16-bit group
//
// DAD is the only one of these that touches a flag at all, and only the carry.
// INX and DCX touch none, which on a Z80 is also true - they are here because
// tests/8080EXM.COM reports three of its four INX/DCX groups as mismatching and
// this says, exhaustively, that the instructions themselves are not the reason.
// ============================================================================

static void test_dad() {
    Group g;
    group_begin(g, "DAD sets the carry from bit 16 and disturbs no other flag");
    static const int pair[4] = { qkz80::regp_BC, qkz80::regp_DE, qkz80::regp_HL, qkz80::regp_SP };
    static const char* name[4] = { "DAD B", "DAD D", "DAD H", "DAD SP" };

    Runner r;
    // 256 values spread over the whole range, against 256 more, against both
    // interesting flag words: 16 bits by 16 bits would be 4 billion executions.
    for (int op = 0; op < 4; op++) {
        for (unsigned i = 0; i < 256; i++) {
            unsigned hl = (i << 8) | (i ^ 0x5A);
            for (unsigned k = 0; k < 256; k++) {
                unsigned rr = (k << 8) | (k ^ 0xA5);
                for (unsigned fi = 0; fi < 2; fi++) {
                    g.cases++;
                    r.cpu.set_reg16((qkz80_uint16)hl, qkz80::regp_HL);
                    if (op != 2) r.cpu.set_reg16((qkz80_uint16)rr, pair[op]);
                    qkz80_uint8 fin = r.cpu.regs.fix_flags((qkz80_uint8)(fi ? 0xFF : 0x00));
                    r.set_flags(fin);
                    r.step((unsigned)(0x09 + op * 0x10));

                    unsigned src = (op == 2) ? hl : rr;
                    unsigned sum = hl + src;
                    unsigned want_hl = sum & 0xFFFF;
                    qkz80_uint8 want_f = (qkz80_uint8)((fin & ~qkz80_cpu_flags::CY) |
                                                       ((sum > 0xFFFF) ? qkz80_cpu_flags::CY : 0));
                    unsigned got_hl = r.cpu.get_reg16(qkz80::regp_HL);
                    qkz80_uint8 got_f = r.flags();
                    if (got_hl != want_hl || got_f != want_f) {
                        g.bad++;
                        if (g.shown++ < 4) {
                            printf("        %s: HL=%04X operand=%04X -> %04X, expected %04X\n",
                                   name[op], hl, src, got_hl, want_hl);
                            show_flag_diff(got_f, want_f);
                        }
                    }
                }
            }
        }
    }
    group_end(g);
}

static void test_inx_dcx() {
    Group g;
    group_begin(g, "INX and DCX change no flag, over all 65536 values of each pair");
    static const int pair[4] = { qkz80::regp_BC, qkz80::regp_DE, qkz80::regp_HL, qkz80::regp_SP };
    static const char* name[8] = { "INX B", "DCX B", "INX D", "DCX D",
                                   "INX H", "DCX H", "INX SP", "DCX SP" };
    Runner r;
    for (int which = 0; which < 8; which++) {
        int p = pair[which / 2];
        int dec = which & 1;
        for (unsigned v = 0; v < 65536; v++) {
            for (unsigned fi = 0; fi < 2; fi++) {
                g.cases++;
                r.cpu.set_reg16((qkz80_uint16)v, p);
                qkz80_uint8 fin = r.cpu.regs.fix_flags((qkz80_uint8)(fi ? 0xFF : 0x00));
                r.set_flags(fin);
                r.step((unsigned)(0x03 + (which / 2) * 0x10 + dec * 8));

                unsigned want = dec ? ((v - 1) & 0xFFFF) : ((v + 1) & 0xFFFF);
                unsigned got = r.cpu.get_reg16(p);
                if (got != want || r.flags() != fin) {
                    g.bad++;
                    if (g.shown++ < 4) {
                        printf("        %s: %04X -> %04X, expected %04X\n",
                               name[which], v, got, want);
                        show_flag_diff(r.flags(), fin);
                    }
                }
            }
        }
    }
    group_end(g);
}

// ============================================================================
// DAA
//
// The 8080 has no subtract flag, so its DAA always applies the addition
// correction.  The carry is set by the second correction and is never cleared -
// a DAA that finds the carry already set leaves it set.
//
// One wording in the Intel manual will not do as written.  It says the second
// correction applies when, "after the incrementing", the top four bits exceed
// nine; taken literally that is wrong for A in FAh..FFh, where adding six
// carries out of the accumulator altogether and leaves a top nibble of zero.
// The condition the hardware applies is on the accumulator before either
// correction - A greater than 99h - which is how every 8080 implementation
// that agrees with silicon writes it, and it is what this uses.  Twelve of the
// 1024 cases below are the ones that tell the two readings apart.
// ============================================================================

static void test_daa() {
    Group g;
    group_begin(g, "DAA applies the two addition corrections the manual describes");
    Runner r;
    for (unsigned a = 0; a < 256; a++) {
        for (unsigned ac = 0; ac < 2; ac++) {
            for (unsigned cy = 0; cy < 2; cy++) {
                g.cases++;
                qkz80_uint8 fin = (qkz80_uint8)(0x02 |
                                                (ac ? qkz80_cpu_flags::AC : 0) |
                                                (cy ? qkz80_cpu_flags::CY : 0));
                r.cpu.set_reg8((qkz80_uint8)a, qkz80::reg_A);
                r.set_flags(fin);
                r.step(0x27);

                unsigned correction = 0, want_cy = cy;
                if (ac || (a & 0x0F) > 9) correction += 0x06;
                if (cy || a > 0x99) { correction += 0x60; want_cy = 1; }
                unsigned res = (a + correction) & 0xFF;
                unsigned want_ac = (((a & 0x0F) + (correction & 0x0F)) > 0x0F);
                qkz80_uint8 want_f = szp(res);
                if (want_ac) want_f |= qkz80_cpu_flags::AC;
                if (want_cy) want_f |= qkz80_cpu_flags::CY;

                unsigned got = r.cpu.get_reg8(qkz80::reg_A);
                qkz80_uint8 got_f = r.flags();
                if (got != res || got_f != want_f) {
                    g.bad++;
                    if (g.shown++ < 4) {
                        printf("        DAA: A=%02X AC=%u CY=%u -> A=%02X, expected %02X\n",
                               a, ac, cy, got, res);
                        show_flag_diff(got_f, want_f);
                    }
                }
            }
        }
    }
    group_end(g);
}

// ============================================================================

int main() {
    printf("cpmemu 8080 mode unit tests\n");
    printf("===========================\n\n");

    test_flag_register();
    test_alu("register", 0x80, false, false);
    test_alu("immediate", 0xC6, true, false);
    test_alu("memory", 0x86, false, true);
    test_inr_dcr();
    test_rotates();
    test_cma_stc_cmc();
    test_daa();
    test_dad();
    test_inx_dcx();

    printf("\n%d groups passed, %d failed\n", groups_passed, groups_failed);
    return groups_failed == 0 ? 0 : 1;
}
