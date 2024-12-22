#include "core.hpp"

#include <cstdlib>
#include <cmath>
#include <cassert>
#include <vector>
#include <iostream>

#include "memory.hpp"
#include "opcode.hpp"
#include "panic.hpp"
#include "utilities.hpp"

AxCore::AxCore(AxMemory& memory)
    : m_memory{&memory}
    , m_wram_begin{static_cast<const uint32_t*>(m_memory->map(*this, AxMemory::WRAM_BEGIN))}
{
}

void AxCore::do_store(uint64_t src, uint64_t addr, uint32_t size)
{
    if(size == 0)
    {
        const auto tmp = static_cast<uint8_t>(src);
        m_memory->store(*this, &tmp, addr, 1);
    }
    else if(size == 1)
    {
        const auto tmp = static_cast<uint16_t>(src);
        m_memory->store(*this, &tmp, addr, 2);
    }
    else if(size == 2)
    {
        const auto tmp = static_cast<uint32_t>(src);
        m_memory->store(*this, &tmp, addr, 4);
    }
    else if(size == 3)
    {
        const auto tmp = static_cast<uint64_t>(src);
        m_memory->store(*this, &tmp, addr, 8);
    }
    else
    {
        ax_panic("Wrong size in store operation ", size);
    }
}

uint64_t AxCore::do_load(uint64_t addr, uint32_t size)
{
    if(size == 0)
    {
        uint8_t dest;
        m_memory->load(*this, &dest, addr, 1);
        return static_cast<uint64_t>(dest);
    }
    else if(size == 1)
    {
        uint16_t dest;
        m_memory->load(*this, &dest, addr, 2);
        return static_cast<uint64_t>(dest);
    }
    else if(size == 2)
    {
        uint32_t dest;
        m_memory->load(*this, &dest, addr, 4);
        return static_cast<uint64_t>(dest);
    }
    else if(size == 3)
    {
        uint64_t dest;
        m_memory->load(*this, &dest, addr, 8);
        return dest;
    }

    ax_panic("Wrong size in load operation ", size);
}

uint32_t AxCore::execute(AxOpcode first, AxOpcode second)
{
    // get moveix imm24 value if present
    const auto old_pc = m_regs.pc;

    const uint64_t imm24 = first.is_bundle() && second.is_moveix() ? second.moveix_imm24() : 0ull;
    execute_unit(first, 0, imm24);

    // execute second instruction
    if(first.is_bundle() && !second.is_moveix()) // don't call execute unit for a nop
    {
        execute_unit(second, 1, imm24);
    }

    if(old_pc != m_regs.pc)
    {
        // if we jumped somewhere returns 0 so the next instruction is where we jumped!
        return 0;
    }

    return first.is_bundle() ? 2 : 1;
}

/*
UNIT ID |    UNIT NAME    |     Issue ID
        | INST 1 | INST 2 | INST 1 | INST 2
   0    |  ALU1  |  ALU2  |   0    |   8
   1    |  ALU1  |  ALU2  |   1    |   9
   2    |  LSU1  |  LSU2  |   2    |   10
   3    |  FPU1  |  FPU2  |   3    |   11
   4    |  /     |   /    |   /    |   /
   5    |  EFU   |   CU   |   5    |   13
   6    |  MDU   |   VU   |   6    |   14
   7    |  BRU   |   /    |   7    |   /
*/
void AxCore::execute_unit(AxOpcode opcode, uint32_t slot, uint64_t imm24)
{
    // reset regs before doing anything
    m_regs.gpi[REG_ZERO] = 0;
    m_regs.gpf[REG_ZERO] = 0.0;

    const auto issue = (slot << 3) | opcode.unit();
    switch(issue)
    {
    case 0:
        [[fallthrough]];
    case 1:
        [[fallthrough]];
    case 8:
        [[fallthrough]];
    case 9:
        execute_alu(opcode, imm24);
        break;
    case 2:
        [[fallthrough]];
    case 10:
        execute_lsu(opcode, imm24);
        break;
    case 3:
        [[fallthrough]];
    case 11:
        execute_fpu(opcode, imm24);
        break;
    case 5:
        execute_efu(opcode, imm24);
        break;
    case 6:
        execute_mdu(opcode, imm24);
        break;
    case 7:
        execute_bru(opcode, imm24);
        break;
    case 13:
        execute_cu(opcode, imm24);
        break;
    case 14:
        execute_vu(opcode, imm24);
        break;
    default:
        ax_panic("Wrong issue ID, opcode is ", std::hex, opcode.value);
        break;
    }
}

namespace
{

template<typename T>
const auto tosi(T value) noexcept
{
    return static_cast<std::make_signed_t<T>>(value);
}

template<typename T>
const auto toui(T value) noexcept
{
    return static_cast<std::make_unsigned_t<T>>(value);
}

template<typename T>
void do_cmp(uint32_t& fr, T left, T right)
{
    static_assert(std::is_signed_v<T>, "do_cmp expect signed value type");

    // In C++ there will be integer promotion when doing the substraction
    // We cast to T once again to trunc the value, then make it unsigned again

#if  defined(__clang__) || defined(__GNUC__)
    T tmp2;
    if(__builtin_sub_overflow(left, right, &tmp2))
    {
        fr |= AxCore::O_MASK;
    }
    else
    {
        fr &= 0xFFFFFFFFu - AxCore::O_MASK;
    }

    const auto tmp = toui(static_cast<T>(tmp2));
#else
    const auto tmp = toui(static_cast<T>(toui(left) - toui(right)));
    // O: Set if result in a value too large for the register to contain.
    if((right > 0 && left < std::numeric_limits<T>::min() + right)
        || (right < 0 && left > std::numeric_limits<T>::max() + right))
    {
        fr |= AxCore::O_MASK;
    }
    else
    {
        fr &= 0xFFFFFFFFu - AxCore::O_MASK;
    }
#endif //  defined(__clang__) || defined(__GNUC)

    // Z: Set if the result of an operation is 0
    if(tmp == 0)
    {
        fr |= AxCore::Z_MASK;
    }
    else
    {
        fr &= 0xFFFFFFFFu - AxCore::Z_MASK;
    }

    // C: Set if the last arithmetic operation a bit beyond the size of the register.
    if(tmp > toui(left))
    {
        fr |= AxCore::C_MASK;
    }
    else
    {
        fr &= 0xFFFFFFFFu - AxCore::C_MASK;
    }

    // N: Set if the result of an operation is negative.
    if(tosi(tmp) < 0)
    {
        fr |= AxCore::N_MASK;
    }
    else
    {
        fr &= 0xFFFFFFFFu - AxCore::N_MASK;
    }
}

}

void AxCore::execute_alu(AxOpcode op, uint64_t imm24)
{
    // Define some "base" operations to compose the real operation
    // write reg A
    const auto writeback = [this, op](auto value)
    {
        m_regs.gpi[op.reg_a()] = static_cast<uint64_t>(value);
    };

    // write reg A by ORing content
    const auto orback = [this, op](auto value)
    {
        m_regs.gpi[op.reg_a()] |= static_cast<uint64_t>(value);
    };

    // read reg B
    const auto left = [this, op]()
    {
        return m_regs.gpi[op.reg_b()];
    };

    // if imm version, return imm with extended imm24
    // otherwise dereference reg C and apply shift
    const auto right = [this, op, imm24]()
    {
        if(!op.alu_has_imm())
        {
            return m_regs.gpi[op.reg_c()] << op.alu_shift();
        }

        const uint64_t tmp = sext_bitsize(op.alu_imm9(), 9);
        // apply imm24
        return tmp ^ (imm24 << 8);
    };

    // Trunc value to op size (8, 16, 32 or 64 bits)
    const auto trunc = [op](auto value)
    {
        return value & sizemask[op.size()];
    };

    const auto sext = [op](auto value)
    {
        return sext_bytesize(value, 1ull << op.size());
    };

    switch(op.operation())
    {
    // ALU-A (000)
    case AX_EXE_ALU_MOVEIX: // no-op
        break;

    case AX_EXE_ALU_MOVEI:
        writeback(sext_bitsize(op.alu_move_imm(), 18) ^ (imm24 << 18));
        break;

    case AX_EXE_ALU_EXT:
        writeback((left() >> op.ext_ins_imm1()) & ((1ull << op.ext_ins_imm2()) - 1));
        break;

    case AX_EXE_ALU_INS:
        orback((left() << op.ext_ins_imm1()) & ((1ull << op.ext_ins_imm2()) - 1));
        break;

    case AX_EXE_ALU_MAX:
        ax_panic("AX_EXE_ALU_MAX not implemented");
    case AX_EXE_ALU_UMAX:
        ax_panic("AX_EXE_ALU_UMAX not implemented");
    case AX_EXE_ALU_MIN:
        ax_panic("AX_EXE_ALU_MIN not implemented");
    case AX_EXE_ALU_UMIN:
        ax_panic("AX_EXE_ALU_UMIN not implemented");

    case AX_EXE_ALU_ADDS:
        writeback(sext(trunc(left()) + trunc(right())));
        break;

    case AX_EXE_ALU_SUBS:
        writeback(sext(trunc(left()) - trunc(right())));
        break;

    case AX_EXE_ALU_CMP:
        switch(op.size())
        {
        case 0:
            do_cmp(m_regs.fr, static_cast<int8_t>(left()), static_cast<int8_t>(right()));
            break;
        case 1:
            do_cmp(m_regs.fr, static_cast<int16_t>(left()), static_cast<int16_t>(right()));
            break;
        case 2:
            do_cmp(m_regs.fr, static_cast<int32_t>(left()), static_cast<int32_t>(right()));
            break;
        case 3:
            do_cmp(m_regs.fr, static_cast<int64_t>(left()), static_cast<int64_t>(right()));
            break;
        default:
            ax_panic("Wrong op size");
        }
        break;
    case AX_EXE_ALU_BIT:
        ax_panic("AX_EXE_ALU_CMPBIT");
    case AX_EXE_ALU_TEST:
        ax_panic("AX_EXE_ALU_TEST");
    case AX_EXE_ALU_TESTFR:
        ax_panic("AX_EXE_ALU_TESTFR");

    // ALU-B (100)
    case AX_EXE_ALU_ADD:
        writeback(trunc(trunc(left()) + trunc(right())));
        break;

    case AX_EXE_ALU_SUB:
        writeback(trunc(trunc(left()) - trunc(right())));
        break;

    case AX_EXE_ALU_XOR:
        writeback(trunc(left()) ^ trunc(right()));
        break;

    case AX_EXE_ALU_OR:
        writeback(trunc(left()) | trunc(right()));
        break;

    case AX_EXE_ALU_AND:
        writeback(trunc(left()) & trunc(right()));
        break;

    case AX_EXE_ALU_LSL:
        writeback(trunc(trunc(left()) << trunc(right())));
        break;

    case AX_EXE_ALU_ASR:
        writeback(trunc(tosi(sext(trunc(left()))) >> tosi(sext(trunc(right())))));
        break;

    case AX_EXE_ALU_LSR:
        writeback(trunc(trunc(left()) >> trunc(right())));
        break;

    case AX_EXE_ALU_SE:
        writeback(trunc(left()) == trunc(right()));
        break;

    case AX_EXE_ALU_SEN:
        writeback(trunc(left()) != trunc(right()));
        break;

    case AX_EXE_ALU_SLTS:
        writeback(sext(left()) < sext(right()));
        break;

    case AX_EXE_ALU_SLTU:
        writeback(trunc(left()) != trunc(right()));
        break;

    case AX_EXE_ALU_SAND:
        writeback((trunc(left()) & trunc(right())) != 0);
        break;

    case AX_EXE_ALU_SBIT:
        writeback((trunc(left()) & trunc(right())) == trunc(right()));
        break;

    case AX_EXE_ALU_CMOVEN:
        if(trunc(left()) == 0)
        {
            writeback(trunc(right()));
        }
        break;

    case AX_EXE_ALU_CMOVE:
        if(trunc(left()) != 0)
        {
            writeback(trunc(right()));
        }
        break;

    default:
        ax_panic("Unknown ALU operation");
    }
}

void AxCore::execute_mdu(AxOpcode op, uint64_t imm24)
{
    // Define some "base" operations to compose the real operation

    // read reg B
    const auto left = [this, op]()
    {
        return m_regs.gpi[op.reg_b()];
    };

    // if imm version, return imm with extended imm24
    // otherwise dereference reg C and apply shift
    const auto right = [this, op, imm24]()
    {
        if(!op.alu_has_imm())
        {
            return m_regs.gpi[op.reg_c()] << op.alu_shift();
        }

        const uint64_t tmp = sext_bitsize(op.alu_imm9(), 9);
        // apply imm24
        return tmp ^ (imm24 << 8);
    };

    // Trunc value to op size (8, 16, 32 or 64 bits)
    const auto trunc = [op](auto value)
    {
        return value & sizemask[op.size()];
    };

    const auto sext = [op](auto value)
    {
        return sext_bytesize(value, 1ull << op.size());
    };

    switch(op.operation())
    {
    case AX_EXE_MDU_DIV:
        m_regs.mdu[0] = trunc(tosi(sext(trunc(left()))) / tosi(trunc(sext(trunc(right())))));
        m_regs.mdu[1] = trunc(tosi(sext(trunc(left()))) % tosi(trunc(sext(trunc(right())))));
        break;
    case AX_EXE_MDU_DIVU:
        m_regs.mdu[0] = trunc(trunc(left()) / sext(trunc(right())));
        m_regs.mdu[1] = trunc(trunc(left()) % sext(trunc(right())));
        break;
    case AX_EXE_MDU_MUL:
        m_regs.mdu[2] = trunc(tosi(sext(trunc(left()))) * tosi(sext(trunc(right()))));
        break;
    case AX_EXE_MDU_MULU:
        m_regs.mdu[2] = trunc(trunc(left()) * sext(trunc(right())));
        break;
    case AX_EXE_MDU_GETMD:
        m_regs.gpi[op.reg_a()] = m_regs.mdu[op.mdu_pq()];
        break;
    case AX_EXE_MDU_SETMD:
        m_regs.mdu[op.mdu_pq()] = m_regs.gpi[op.reg_a()];
        break;

    default:
        ax_panic("Unknown MDU operation");
    }
}

void AxCore::execute_lsu(AxOpcode op, uint64_t imm24)
{
    // write in A
    const auto writeback = [this, op](auto value)
    {
        m_regs.gpi[op.reg_a()] = static_cast<uint64_t>(value);
    };

    //  get addr
    const auto addrreg = [this, op]()
    {
        return m_regs.gpi[op.reg_b()] + (m_regs.gpi[op.reg_c()] << op.lsu_shift());
    };

    // if imm version, return imm with extended imm24
    // otherwise dereference reg C and apply shift
    const auto addrimm = [this, op, imm24]()
    {
        const uint64_t tmp = sext_bitsize(op.lsu_imm10(), 10);
        const auto off = tmp ^ (imm24 << 9);
        return toui(tosi(m_regs.gpi[op.reg_b()]) + tosi(off));
    };

    // Trunc value to op size (8, 16, 32 or 64 bits)
    const auto trunc = [op](auto value)
    {
        return value & sizemask[op.size()];
    };

    const auto sext = [op](auto value)
    {
        return sext_bytesize(value, 1ull << op.size());
    };

    switch(op.operation())
    {
    // reg version
    case AX_EXE_LSU_LD:
        writeback(do_load(addrreg(), op.size()));
        break;
    case AX_EXE_LSU_LDS:
        writeback(sext(do_load(addrreg(), op.size())));
        break;
    case AX_EXE_LSU_FLD:
        ax_panic("AX_EXE_LSU_FLD not implemented");
        break;
    case AX_EXE_LSU_ST:
        do_store(m_regs.gpi[op.reg_a()], addrreg(), op.size());
        break;
    case AX_EXE_LSU_FST:
        ax_panic("AX_EXE_LSU_FST not implemented");
        break;
    case AX_EXE_LSU_LDI:
        writeback(do_load(addrimm(), op.size()));
        break;
    case AX_EXE_LSU_LDIS:
        writeback(sext(do_load(addrimm(), op.size())));
        break;
    case AX_EXE_LSU_FLDI:
        ax_panic("AX_EXE_LSU_FLDI not implemented");
        break;
    case AX_EXE_LSU_STI:
        do_store(m_regs.gpi[op.reg_a()], addrimm(), op.size());
        break;
    case AX_EXE_LSU_FSTI:
        ax_panic("AX_EXE_LSU_FSTI not implemented");
        break;
    default:
        ax_panic("Unknown LSU operation");
    }
}

void AxCore::execute_bru(AxOpcode op, uint64_t imm24)
{
    const auto relative23 = [op, imm24]() -> int64_t
    {
        return tosi(sext_bitsize(op.bru_imm23(), 23) ^ (imm24 << 22));
    };

    const auto relative24 = [op, imm24]() -> int64_t
    {
        return tosi(sext_bitsize(op.bru_imm24(), 24) ^ (imm24 << 23));
    };

    const auto absolute24 = [op, imm24]()
    {
        return op.bru_imm24() | (imm24 << 24);
    };

    const auto lr_value = [this, op]()
    {
        return static_cast<uint64_t>(m_regs.pc + 1 + static_cast<uint32_t>(op.is_bundle()));
    };

    const auto add_pc = [this](int64_t value)
    {
        m_regs.pc = static_cast<uint32_t>(static_cast<int64_t>(m_regs.pc) + value);
    };

    const auto z_mask = [this]()
    {
        return m_regs.fr & 1u;
    };

    const auto c_mask = [this]()
    {
        return (m_regs.fr >> 1) & 1u;
    };

    const auto n_mask = [this]()
    {
        return (m_regs.fr >> 2) & 1u;
    };

    const auto o_mask = [this]()
    {
        return (m_regs.fr >> 3) & 1u;
    };

    switch(op.operation())
    {
    case AX_EXE_BRU_BEQ:
        if(z_mask())
        {
            add_pc(relative23());
        }
        break;
    case AX_EXE_BRU_BNE:
        if(!z_mask())
        {
            add_pc(relative23());
        }
        break;
    case AX_EXE_BRU_BLT:
        if(n_mask() != o_mask())
        {
            add_pc(relative23());
        }
        break;
    case AX_EXE_BRU_BGE:
        if(z_mask() || n_mask() == o_mask())
        {
            add_pc(relative23());
        }
        break;
    case AX_EXE_BRU_BLTU:
        if(c_mask())
        {
            add_pc(relative23());
        }
        break;
    case AX_EXE_BRU_BGEU:
        if(z_mask() || !c_mask())
        {
            add_pc(relative23());
        }
        break;
    case AX_EXE_BRU_BRA:
        add_pc(relative24());
        break;
    case AX_EXE_BRU_CALLR:
        m_regs.gpi[31] = lr_value();
        add_pc(relative24());
        break;
    case AX_EXE_BRU_JUMP:
        m_regs.pc = static_cast<uint32_t>(absolute24());
        break;
    case AX_EXE_BRU_CALL:
        m_regs.gpi[31] = lr_value();
        m_regs.pc = static_cast<uint32_t>(absolute24());
        break;
    case AX_EXE_BRU_INDIRECTCALLR:
        m_regs.gpi[op.reg_a()] = lr_value();
        add_pc(tosi(m_regs.gpi[op.reg_b()]));
        break;
    case AX_EXE_BRU_INDIRECTCALL:
        m_regs.gpi[op.reg_a()] = lr_value();
        m_regs.pc = static_cast<uint32_t>(m_regs.gpi[op.reg_b()]);
        break;
    default:
        ax_panic("Unknown BRU operation");
    }
}

void AxCore::execute_fpu(AxOpcode op, uint64_t imm24)
{
    ax_panic("FPU not supported yet");
}

void AxCore::execute_efu(AxOpcode op, uint64_t imm24)
{
    ax_panic("EFU not supported yet");
}

void AxCore::execute_cu(AxOpcode op, uint64_t imm24)
{
    const auto ir_value = [this, op]()
    {
        return static_cast<uint64_t>(m_regs.pc + 1 + static_cast<uint32_t>(op.is_bundle()));
    };

    switch(op.operation())
    {
    case AX_EXE_CU_GETIR:
        ax_panic("AX_EXE_CU_GETIR not implemented");
    case AX_EXE_CU_SETFR:
        ax_panic("AX_EXE_CU_SETFR not implemented");
    case AX_EXE_CU_MMU:
        ax_panic("AX_EXE_CU_MMU not implemented");
    case AX_EXE_CU_SYNC:
        ax_panic("AX_EXE_CU_SYNC not implemented");
    case AX_EXE_CU_SYSCALL:
        m_regs.ir = ir_value();
        m_regs.pc = 0x80000000u;
        m_syscall = 1;
        break;
    case AX_EXE_CU_RETI:
        m_regs.pc = m_regs.ir;
        break;
    default:
        ax_panic("Unknown CU operation");
    }
}

void AxCore::execute_vu(AxOpcode op, uint64_t imm24)
{
    ax_panic("VU not supported yet");
}
