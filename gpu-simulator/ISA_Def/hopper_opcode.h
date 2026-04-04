// Hopper SM90 opcode mapping for Accel-Sim
// Based on ampere_opcode.h with Ampere→Hopper microarchitecture migration
// corrections derived from sm90_2.txt pipe classification data.

#ifndef HOPPER_OPCODE_H
#define HOPPER_OPCODE_H

#include <string>
#include <unordered_map>
#include "abstract_hardware_model.h"
#include "trace_opcode.h"

#define HOPPER_H100_BINART_VERSION 90

// Hopper SM90 ISA
// Key Ampere→Hopper changes:
//   - IMAD/IMUL/IDP/IDP4A moved from int_pipe to fmalighter_pipe (INTP→SP)
//   - SEL/MOV/PLOP3/FMNMX/FSETP etc moved to int_pipe (ALU/SP→INTP)
//   - BREV/POPC/FLO/F2F/F2I/I2F/FRND/FCHK moved to mio_pipe (INTP/ALU/SP→SFU)
//   - IMMA/BMMA moved from SPEC_3 to int_pipe (SPEC_3→INTP)
static const std::unordered_map<std::string, OpcodeChar> Hopper_OpcodeMap = {
    // ===== fmalighter_pipe → SP_OP =====
    // Floating Point 32 Instructions
    {"FADD", OpcodeChar(OP_FADD, SP_OP)},
    {"FADD32I", OpcodeChar(OP_FADD32I, SP_OP)},
    {"FFMA32I", OpcodeChar(OP_FFMA32I, SP_OP)},
    {"FFMA", OpcodeChar(OP_FFMA, SP_OP)},
    {"FMUL", OpcodeChar(OP_FMUL, SP_OP)},
    {"FMUL32I", OpcodeChar(OP_FMUL32I, SP_OP)},
    {"FSWZADD", OpcodeChar(OP_FSWZADD, SP_OP)},
    {"RRO", OpcodeChar(OP_RRO, SP_OP)},
    // Integer instructions migrated to fmalighter_pipe in SM90
    {"IMAD", OpcodeChar(OP_IMAD, SP_OP)},
    {"IMUL", OpcodeChar(OP_IMUL, SP_OP)},
    {"IMAD32I", OpcodeChar(OP_IMAD32I, SP_OP)},
    {"IMUL32I", OpcodeChar(OP_IMUL32I, SP_OP)},
    {"IDP", OpcodeChar(OP_IDP, SP_OP)},
    {"IDP4A", OpcodeChar(OP_IDP4A, SP_OP)},
    // DPX fmalighter_pipe
    {"VIADD", OpcodeChar(OP_VIADD, SP_OP)},

    // ===== fp16_pipe → SP_OP =====
    {"HADD2", OpcodeChar(OP_HADD2, SP_OP)},
    {"HADD2_32I", OpcodeChar(OP_HADD2_32I, SP_OP)},
    {"HFMA2", OpcodeChar(OP_HFMA2, SP_OP)},
    {"HFMA2_32I", OpcodeChar(OP_HFMA2_32I, SP_OP)},
    {"HMUL2", OpcodeChar(OP_HMUL2, SP_OP)},
    {"HMUL2_32I", OpcodeChar(OP_HMUL2_32I, SP_OP)},
    {"HSET2", OpcodeChar(OP_HSET2, SP_OP)},
    {"HSETP2", OpcodeChar(OP_HSETP2, SP_OP)},
    {"HMNMX2", OpcodeChar(OP_HMNMX2, SP_OP)},
    // DPX fp16_pipe
    {"VHMNMX", OpcodeChar(OP_VHMNMX, SP_OP)},

    // ===== mio_pipe SFU subset → SFU_OP =====
    {"MUFU", OpcodeChar(OP_MUFU, SFU_OP)},
    // Migrated from int_pipe to mio_pipe in SM90
    {"BREV", OpcodeChar(OP_BREV, SFU_OP)},
    {"POPC", OpcodeChar(OP_POPC, SFU_OP)},
    {"FLO", OpcodeChar(OP_FLO, SFU_OP)},
    // Migrated from SP/ALU to mio_pipe in SM90
    {"FCHK", OpcodeChar(OP_FCHK, SFU_OP)},
    {"F2F", OpcodeChar(OP_F2F, SFU_OP)},
    {"F2I", OpcodeChar(OP_F2I, SFU_OP)},
    {"I2F", OpcodeChar(OP_I2F, SFU_OP)},
    {"FRND", OpcodeChar(OP_FRND, SFU_OP)},
    // mio_pipe 64-bit conversions
    {"F2F64", OpcodeChar(OP_F2F64, SFU_OP)},
    {"F2I64", OpcodeChar(OP_F2I64, SFU_OP)},
    {"I2F64", OpcodeChar(OP_I2F64, SFU_OP)},
    {"FRND64", OpcodeChar(OP_FRND64, SFU_OP)},
    // mio_pipe misc SFU-class
    {"IMADSP", OpcodeChar(OP_IMADSP, SFU_OP)},
    {"AL2P", OpcodeChar(OP_AL2P, SFU_OP)},
    {"IPA", OpcodeChar(OP_IPA, SFU_OP)},
    {"ISBERD", OpcodeChar(OP_ISBERD, SFU_OP)},
    {"ISBEWR", OpcodeChar(OP_ISBEWR, SFU_OP)},
    {"OUT", OpcodeChar(OP_OUT, SFU_OP)},
    {"PIXLD", OpcodeChar(OP_PIXLD, SFU_OP)},
    {"LDTRAM", OpcodeChar(OP_LDTRAM, SFU_OP)},

    // ===== Tensor Core → SPECIALIZED_UNIT_3_OP =====
    {"HMMA", OpcodeChar(OP_HMMA, SPECIALIZED_UNIT_3_OP)},
    {"DMMA", OpcodeChar(OP_DMMA, SPECIALIZED_UNIT_3_OP)},
    // GMMA (Group MMA) - stub for Phase 0
    {"HGMMA", OpcodeChar(OP_HGMMA, SPECIALIZED_UNIT_3_OP)},
    {"IGMMA", OpcodeChar(OP_IGMMA, SPECIALIZED_UNIT_3_OP)},
    {"BGMMA", OpcodeChar(OP_BGMMA, SPECIALIZED_UNIT_3_OP)},
    {"QGMMA", OpcodeChar(OP_QGMMA, SPECIALIZED_UNIT_3_OP)},

    // ===== fma64lite_pipe → DP_OP =====
    {"DADD", OpcodeChar(OP_DADD, DP_OP)},
    {"DFMA", OpcodeChar(OP_DFMA, DP_OP)},
    {"DMUL", OpcodeChar(OP_DMUL, DP_OP)},
    {"DSETP", OpcodeChar(OP_DSETP, DP_OP)},
    {"CLMAD", OpcodeChar(OP_CLMAD, DP_OP)},
    // fma64heavy_pipe → DP_OP
    {"DMNMX", OpcodeChar(OP_DMNMX, DP_OP)},
    {"DSET", OpcodeChar(OP_DSET, DP_OP)},

    // ===== int_pipe → INTP_OP =====
    {"BMSK", OpcodeChar(OP_BMSK, INTP_OP)},
    {"IABS", OpcodeChar(OP_IABS, INTP_OP)},
    {"IADD", OpcodeChar(OP_IADD, INTP_OP)},
    {"IADD3", OpcodeChar(OP_IADD3, INTP_OP)},
    {"IADD32I", OpcodeChar(OP_IADD32I, INTP_OP)},
    {"IMNMX", OpcodeChar(OP_IMNMX, INTP_OP)},
    {"ISCADD", OpcodeChar(OP_ISCADD, INTP_OP)},
    {"ISCADD32I", OpcodeChar(OP_ISCADD32I, INTP_OP)},
    {"ISETP", OpcodeChar(OP_ISETP, INTP_OP)},
    {"LEA", OpcodeChar(OP_LEA, INTP_OP)},
    {"LOP", OpcodeChar(OP_LOP, INTP_OP)},
    {"LOP3", OpcodeChar(OP_LOP3, INTP_OP)},
    {"LOP32I", OpcodeChar(OP_LOP32I, INTP_OP)},
    {"SHF", OpcodeChar(OP_SHF, INTP_OP)},
    {"SHL", OpcodeChar(OP_SHL, INTP_OP)},
    {"SHR", OpcodeChar(OP_SHR, INTP_OP)},
    {"VABSDIFF", OpcodeChar(OP_VABSDIFF, INTP_OP)},
    {"VABSDIFF4", OpcodeChar(OP_VABSDIFF4, INTP_OP)},
    {"BFE", OpcodeChar(OP_BFE, INTP_OP)},
    {"BFI", OpcodeChar(OP_BFI, INTP_OP)},
    {"ICMP", OpcodeChar(OP_ICMP, INTP_OP)},
    {"ISET", OpcodeChar(OP_ISET, INTP_OP)},
    {"FCMP", OpcodeChar(OP_FCMP, INTP_OP)},
    {"XMAD", OpcodeChar(OP_XMAD, INTP_OP)},
    {"IDE", OpcodeChar(OP_IDE, INTP_OP)},
    // Migrated from ALU_OP to int_pipe in SM90
    {"SEL", OpcodeChar(OP_SEL, INTP_OP)},
    {"MOV", OpcodeChar(OP_MOV, INTP_OP)},
    {"MOV32I", OpcodeChar(OP_MOV32I, INTP_OP)},
    {"MOVM", OpcodeChar(OP_MOVM, INTP_OP)},
    {"PRMT", OpcodeChar(OP_PRMT, INTP_OP)},
    {"SGXT", OpcodeChar(OP_SGXT, INTP_OP)},
    {"PLOP3", OpcodeChar(OP_PLOP3, INTP_OP)},
    {"PSETP", OpcodeChar(OP_PSETP, INTP_OP)},
    {"P2R", OpcodeChar(OP_P2R, INTP_OP)},
    {"R2P", OpcodeChar(OP_R2P, INTP_OP)},
    {"CS2R", OpcodeChar(OP_CS2R, INTP_OP)},
    {"LEPC", OpcodeChar(OP_LEPC, INTP_OP)},
    {"VOTE", OpcodeChar(OP_VOTE, INTP_OP)},
    {"RPCMOV", OpcodeChar(OP_RPCMOV, INTP_OP)},
    {"I2I", OpcodeChar(OP_I2I, INTP_OP)},
    {"I2IP", OpcodeChar(OP_I2IP, INTP_OP)},
    {"F2FP", OpcodeChar(OP_F2FP, INTP_OP)},
    {"F2IP", OpcodeChar(OP_F2IP, INTP_OP)},
    {"I2FP", OpcodeChar(OP_I2FP, INTP_OP)},
    // Migrated from SP_OP to int_pipe in SM90
    {"FSEL", OpcodeChar(OP_FSEL, INTP_OP)},
    {"FMNMX", OpcodeChar(OP_FMNMX, INTP_OP)},
    {"FSET", OpcodeChar(OP_FSET, INTP_OP)},
    {"FSETP", OpcodeChar(OP_FSETP, INTP_OP)},
    // Migrated from SPEC_3 to int_pipe in SM90
    {"IMMA", OpcodeChar(OP_IMMA, INTP_OP)},
    {"BMMA", OpcodeChar(OP_BMMA, INTP_OP)},
    // int_pipe misc
    {"CSET", OpcodeChar(OP_CSET, INTP_OP)},
    {"CSETP", OpcodeChar(OP_CSETP, INTP_OP)},
    {"PSET", OpcodeChar(OP_PSET, INTP_OP)},
    {"GETFPFLAGS", OpcodeChar(OP_GETFPFLAGS, INTP_OP)},
    {"SETFPFLAGS", OpcodeChar(OP_SETFPFLAGS, INTP_OP)},
    {"SCATTER", OpcodeChar(OP_SCATTER, INTP_OP)},
    {"GATHER", OpcodeChar(OP_GATHER, INTP_OP)},
    {"SPMETADATA", OpcodeChar(OP_SPMETADATA, INTP_OP)},
    {"GENMETADATA", OpcodeChar(OP_GENMETADATA, INTP_OP)},
    {"BITEXTRACT", OpcodeChar(OP_BITEXTRACT, INTP_OP)},
    // DPX int_pipe
    {"VIMNMX", OpcodeChar(OP_VIMNMX, INTP_OP)},
    {"VIMNMX3", OpcodeChar(OP_VIMNMX3, INTP_OP)},
    {"VIADDMNMX", OpcodeChar(OP_VIADDMNMX, INTP_OP)},
    // Critic Review: int_pipe vector instructions
    {"VMAD", OpcodeChar(OP_VMAD, INTP_OP)},
    {"VADD", OpcodeChar(OP_VADD, INTP_OP)},
    {"VMNMX", OpcodeChar(OP_VMNMX, INTP_OP)},
    {"VSET", OpcodeChar(OP_VSET, INTP_OP)},
    {"VSETP", OpcodeChar(OP_VSETP, INTP_OP)},
    {"VSHL", OpcodeChar(OP_VSHL, INTP_OP)},
    {"VSHR", OpcodeChar(OP_VSHR, INTP_OP)},

    // ===== mio_pipe load/store subset =====
    // Load Instructions
    {"LD", OpcodeChar(OP_LD, LOAD_OP)},
    {"LDC", OpcodeChar(OP_LDC, ALU_OP)},
    {"LDG", OpcodeChar(OP_LDG, LOAD_OP)},
    {"LDL", OpcodeChar(OP_LDL, LOAD_OP)},
    {"LDS", OpcodeChar(OP_LDS, LOAD_OP)},
    {"LDSM", OpcodeChar(OP_LDSM, LOAD_OP)},
    {"LDGSTS", OpcodeChar(OP_LDGSTS, LOAD_OP)},
    {"LDGMC", OpcodeChar(OP_LDGMC, LOAD_OP)},
    {"LD_OLD", OpcodeChar(OP_LD_OLD, LOAD_OP)},
    {"LDG_OLD", OpcodeChar(OP_LDG_OLD, LOAD_OP)},
    {"VILD", OpcodeChar(OP_VILD, LOAD_OP)},
    {"ALD", OpcodeChar(OP_ALD, LOAD_OP)},
    // Store Instructions
    {"ST", OpcodeChar(OP_ST, STORE_OP)},
    {"STG", OpcodeChar(OP_STG, STORE_OP)},
    {"STL", OpcodeChar(OP_STL, STORE_OP)},
    {"STS", OpcodeChar(OP_STS, STORE_OP)},
    {"STSM", OpcodeChar(OP_STSM, STORE_OP)},
    {"AST", OpcodeChar(OP_AST, STORE_OP)},
    // Atomics
    {"ATOM", OpcodeChar(OP_ATOM, STORE_OP)},
    {"ATOMS", OpcodeChar(OP_ATOMS, STORE_OP)},
    {"ATOMG", OpcodeChar(OP_ATOMG, STORE_OP)},
    {"RED", OpcodeChar(OP_RED, STORE_OP)},
    {"REDG", OpcodeChar(OP_REDG, STORE_OP)},
    // DSMEM store
    {"STAS", OpcodeChar(OP_STAS, STORE_OP)},
    {"REDAS", OpcodeChar(OP_REDAS, STORE_OP)},
    // mio_pipe misc memory
    {"MATCH", OpcodeChar(OP_MATCH, ALU_OP)},
    {"QSPC", OpcodeChar(OP_QSPC, ALU_OP)},
    {"CCTL", OpcodeChar(OP_CCTL, ALU_OP)},
    {"CCTLL", OpcodeChar(OP_CCTLL, ALU_OP)},
    {"CCTLT", OpcodeChar(OP_CCTLT, ALU_OP)},
    {"SUCCTL", OpcodeChar(OP_SUCCTL, ALU_OP)},
    {"ERRBAR", OpcodeChar(OP_ERRBAR, ALU_OP)},
    {"MEMBAR", OpcodeChar(OP_MEMBAR, MEMORY_BARRIER_OP)},
    {"FENCE", OpcodeChar(OP_FENCE, MEMORY_BARRIER_OP)},
    {"LDGDEPBAR", OpcodeChar(OP_LDGDEPBAR, ALU_OP)},
    {"SHFL", OpcodeChar(OP_SHFL, ALU_OP)},
    {"FOOTPRINT", OpcodeChar(OP_FOOTPRINT, ALU_OP)},
    {"CGAERRBAR", OpcodeChar(OP_CGAERRBAR, ALU_OP)},
    // Barrier
    {"BAR", OpcodeChar(OP_BAR, BARRIER_OP)},

    // ===== Texture → SPECIALIZED_UNIT_2_OP =====
    {"TEX", OpcodeChar(OP_TEX, SPECIALIZED_UNIT_2_OP)},
    {"TLD", OpcodeChar(OP_TLD, SPECIALIZED_UNIT_2_OP)},
    {"TLD4", OpcodeChar(OP_TLD4, SPECIALIZED_UNIT_2_OP)},
    {"TMML", OpcodeChar(OP_TMML, SPECIALIZED_UNIT_2_OP)},
    {"TXD", OpcodeChar(OP_TXD, SPECIALIZED_UNIT_2_OP)},
    {"TXQ", OpcodeChar(OP_TXQ, SPECIALIZED_UNIT_2_OP)},
    {"TEXS", OpcodeChar(OP_TEXS, SPECIALIZED_UNIT_2_OP)},
    {"TLDS", OpcodeChar(OP_TLDS, SPECIALIZED_UNIT_2_OP)},
    {"TLD4S", OpcodeChar(OP_TLD4S, SPECIALIZED_UNIT_2_OP)},
    {"TXA", OpcodeChar(OP_TXA, SPECIALIZED_UNIT_2_OP)},

    // Surface Instructions
    {"SUATOM", OpcodeChar(OP_SUATOM, ALU_OP)},
    {"SULD", OpcodeChar(OP_SULD, ALU_OP)},
    {"SUQUERY", OpcodeChar(OP_SUQUERY, ALU_OP)},
    {"SURED", OpcodeChar(OP_SURED, ALU_OP)},
    {"SUST", OpcodeChar(OP_SUST, ALU_OP)},

    // ===== cbu_pipe → SPECIALIZED_UNIT_1_OP =====
    {"BMOV", OpcodeChar(OP_BMOV, SPECIALIZED_UNIT_1_OP)},
    {"BPT", OpcodeChar(OP_BPT, SPECIALIZED_UNIT_1_OP)},
    {"BRA", OpcodeChar(OP_BRA, SPECIALIZED_UNIT_1_OP)},
    {"BREAK", OpcodeChar(OP_BREAK, SPECIALIZED_UNIT_1_OP)},
    {"BRX", OpcodeChar(OP_BRX, SPECIALIZED_UNIT_1_OP)},
    {"BRXU", OpcodeChar(OP_BRXU, SPECIALIZED_UNIT_1_OP)},
    {"BSSY", OpcodeChar(OP_BSSY, SPECIALIZED_UNIT_1_OP)},
    {"BSSY_OLD", OpcodeChar(OP_BSSY_OLD, SPECIALIZED_UNIT_1_OP)},
    {"BSYNC", OpcodeChar(OP_BSYNC, SPECIALIZED_UNIT_1_OP)},
    {"CALL", OpcodeChar(OP_CALL, SPECIALIZED_UNIT_1_OP)},
    {"EXIT", OpcodeChar(OP_EXIT, EXIT_OPS)},
    {"JMP", OpcodeChar(OP_JMP, SPECIALIZED_UNIT_1_OP)},
    {"JMX", OpcodeChar(OP_JMX, SPECIALIZED_UNIT_1_OP)},
    {"JMXU", OpcodeChar(OP_JMXU, SPECIALIZED_UNIT_1_OP)},
    {"KILL", OpcodeChar(OP_KILL, SPECIALIZED_UNIT_1_OP)},
    {"KIL", OpcodeChar(OP_KIL, SPECIALIZED_UNIT_1_OP)},
    {"NANOSLEEP", OpcodeChar(OP_NANOSLEEP, SPECIALIZED_UNIT_1_OP)},
    {"NANOTRAP", OpcodeChar(OP_NANOTRAP, SPECIALIZED_UNIT_1_OP)},
    {"RET", OpcodeChar(OP_RET, SPECIALIZED_UNIT_1_OP)},
    {"RTT", OpcodeChar(OP_RTT, SPECIALIZED_UNIT_1_OP)},
    {"WARPSYNC", OpcodeChar(OP_WARPSYNC, SPECIALIZED_UNIT_1_OP)},
    {"YIELD", OpcodeChar(OP_YIELD, SPECIALIZED_UNIT_1_OP)},
    // Hopper-specific cbu_pipe control flow
    {"ELECT", OpcodeChar(OP_ELECT, SPECIALIZED_UNIT_1_OP)},
    {"ENDCOLLECTIVE", OpcodeChar(OP_ENDCOLLECTIVE, SPECIALIZED_UNIT_1_OP)},
    {"PREEXIT", OpcodeChar(OP_PREEXIT, SPECIALIZED_UNIT_1_OP)},
    {"ACQBULK", OpcodeChar(OP_ACQBULK, SPECIALIZED_UNIT_1_OP)},

    // ===== udp_pipe → SPECIALIZED_UNIT_4_OP =====
    {"R2UR", OpcodeChar(OP_R2UR, SPECIALIZED_UNIT_4_OP)},
    {"REDUX", OpcodeChar(OP_REDUX, SPECIALIZED_UNIT_4_OP)},
    {"S2UR", OpcodeChar(OP_S2UR, SPECIALIZED_UNIT_4_OP)},
    {"UBMSK", OpcodeChar(OP_UBMSK, SPECIALIZED_UNIT_4_OP)},
    {"UBREV", OpcodeChar(OP_UBREV, SPECIALIZED_UNIT_4_OP)},
    {"UCLEA", OpcodeChar(OP_UCLEA, SPECIALIZED_UNIT_4_OP)},
    {"UF2FP", OpcodeChar(OP_UF2FP, SPECIALIZED_UNIT_4_OP)},
    {"UFLO", OpcodeChar(OP_UFLO, SPECIALIZED_UNIT_4_OP)},
    {"UIADD3", OpcodeChar(OP_UIADD3, SPECIALIZED_UNIT_4_OP)},
    {"UIMAD", OpcodeChar(OP_UIMAD, SPECIALIZED_UNIT_4_OP)},
    {"UISETP", OpcodeChar(OP_UISETP, SPECIALIZED_UNIT_4_OP)},
    {"ULDC", OpcodeChar(OP_ULDC, SPECIALIZED_UNIT_4_OP)},
    {"ULEA", OpcodeChar(OP_ULEA, SPECIALIZED_UNIT_4_OP)},
    {"ULOP", OpcodeChar(OP_ULOP, SPECIALIZED_UNIT_4_OP)},
    {"ULOP3", OpcodeChar(OP_ULOP3, SPECIALIZED_UNIT_4_OP)},
    {"ULOP32I", OpcodeChar(OP_ULOP32I, SPECIALIZED_UNIT_4_OP)},
    {"UMOV", OpcodeChar(OP_UMOV, SPECIALIZED_UNIT_4_OP)},
    {"UMOV32I", OpcodeChar(OP_UMOV32I, SPECIALIZED_UNIT_4_OP)},
    {"UP2UR", OpcodeChar(OP_UP2UR, SPECIALIZED_UNIT_4_OP)},
    {"UPLOP3", OpcodeChar(OP_UPLOP3, SPECIALIZED_UNIT_4_OP)},
    {"UPOPC", OpcodeChar(OP_UPOPC, SPECIALIZED_UNIT_4_OP)},
    {"UPRMT", OpcodeChar(OP_UPRMT, SPECIALIZED_UNIT_4_OP)},
    {"UPSETP", OpcodeChar(OP_UPSETP, SPECIALIZED_UNIT_4_OP)},
    {"UR2UP", OpcodeChar(OP_UR2UP, SPECIALIZED_UNIT_4_OP)},
    {"USEL", OpcodeChar(OP_USEL, SPECIALIZED_UNIT_4_OP)},
    {"USGXT", OpcodeChar(OP_USGXT, SPECIALIZED_UNIT_4_OP)},
    {"USHF", OpcodeChar(OP_USHF, SPECIALIZED_UNIT_4_OP)},
    {"USHL", OpcodeChar(OP_USHL, SPECIALIZED_UNIT_4_OP)},
    {"USHR", OpcodeChar(OP_USHR, SPECIALIZED_UNIT_4_OP)},
    {"VOTEU", OpcodeChar(OP_VOTEU, SPECIALIZED_UNIT_4_OP)},
    {"ULEPC", OpcodeChar(OP_ULEPC, SPECIALIZED_UNIT_4_OP)},
    {"USETMAXREG", OpcodeChar(OP_USETMAXREG, SPECIALIZED_UNIT_4_OP)},
    {"USETSHMSZ", OpcodeChar(OP_USETSHMSZ, SPECIALIZED_UNIT_4_OP)},
    // TMA - stub for Phase 0
    {"UTMALDG", OpcodeChar(OP_UTMALDG, SPECIALIZED_UNIT_4_OP)},
    {"UTMASTG", OpcodeChar(OP_UTMASTG, SPECIALIZED_UNIT_4_OP)},
    {"UTMAREDG", OpcodeChar(OP_UTMAREDG, SPECIALIZED_UNIT_4_OP)},
    {"UTMACMDFLUSH", OpcodeChar(OP_UTMACMDFLUSH, SPECIALIZED_UNIT_4_OP)},
    {"UTMAPF", OpcodeChar(OP_UTMAPF, SPECIALIZED_UNIT_4_OP)},
    {"UTMACCTL", OpcodeChar(OP_UTMACCTL, SPECIALIZED_UNIT_4_OP)},
    {"UBLKCP", OpcodeChar(OP_UBLKCP, SPECIALIZED_UNIT_4_OP)},
    {"UBLKRED", OpcodeChar(OP_UBLKRED, SPECIALIZED_UNIT_4_OP)},
    {"UBLKPF", OpcodeChar(OP_UBLKPF, SPECIALIZED_UNIT_4_OP)},
    // CGA Barrier - stub for Phase 0
    {"UCGABAR_ARV", OpcodeChar(OP_UCGABAR_ARV, SPECIALIZED_UNIT_4_OP)},
    {"UCGABAR_GET", OpcodeChar(OP_UCGABAR_GET, SPECIALIZED_UNIT_4_OP)},
    {"UCGABAR_SET", OpcodeChar(OP_UCGABAR_SET, SPECIALIZED_UNIT_4_OP)},
    {"UCGABAR_WAIT", OpcodeChar(OP_UCGABAR_WAIT, SPECIALIZED_UNIT_4_OP)},
    {"UCGABARARV", OpcodeChar(OP_UCGABARARV, SPECIALIZED_UNIT_4_OP)},
    {"UCGABARGET", OpcodeChar(OP_UCGABARGET, SPECIALIZED_UNIT_4_OP)},
    {"UCGABARSET", OpcodeChar(OP_UCGABARSET, SPECIALIZED_UNIT_4_OP)},
    {"UCGABARWAIT", OpcodeChar(OP_UCGABARWAIT, SPECIALIZED_UNIT_4_OP)},

    // ===== fe_pipe → ALU_OP =====
    {"B2R", OpcodeChar(OP_B2R, ALU_OP)},
    {"CSMTEST", OpcodeChar(OP_CSMTEST, ALU_OP)},
    {"DEPBAR", OpcodeChar(OP_DEPBAR, ALU_OP)},
    {"GETLMEMBASE", OpcodeChar(OP_GETLMEMBASE, ALU_OP)},
    {"NOP", OpcodeChar(OP_NOP, ALU_OP)},
    {"PMTRIG", OpcodeChar(OP_PMTRIG, ALU_OP)},
    {"R2B", OpcodeChar(OP_R2B, ALU_OP)},
    {"S2R", OpcodeChar(OP_S2R, ALU_OP)},
    {"SETCTAID", OpcodeChar(OP_SETCTAID, ALU_OP)},
    {"SETLMEMBASE", OpcodeChar(OP_SETLMEMBASE, ALU_OP)},
    {"VOTE_VTG", OpcodeChar(OP_VOTE_VTG, ALU_OP)},
    {"STP", OpcodeChar(OP_STP, ALU_OP)},
    // mio_pipe warpgroup/sync → ALU_OP (Phase 0 stub)
    {"WARPGROUP", OpcodeChar(OP_WARPGROUP, ALU_OP)},
    {"WARPGROUPSET", OpcodeChar(OP_WARPGROUPSET, ALU_OP)},
    {"ARRIVES", OpcodeChar(OP_ARRIVES, ALU_OP)},
    {"SYNCS", OpcodeChar(OP_SYNCS, ALU_OP)},

};

#endif
