#pragma once
#include <iostream>
#ifdef __CUDACC__
#define __excec_location__ __device__
#else
#define __excec_location__
#endif
namespace g26
{
    enum LogicalOperator 
    {
        AND = 0, OR, NAND, NOR, XOR, XNOR
    };

    class LogicalInput 
    {
    public:
        std::uint32_t bitfield; // bit 0: n0, bit 1: n2, bit 2-4: operator
        LogicalInput() = default;
        LogicalInput(bool n1, bool n2, LogicalOperator op);
        __excec_location__ void Calculate();
        __excec_location__ bool GetResult() const;
    };

    inline LogicalInput::LogicalInput(bool n1, bool n2, LogicalOperator op)
        : bitfield((n1) | (n2 << 1) | (op << 2)) {}

    __excec_location__ inline void LogicalInput::Calculate()
    {
        switch ((bitfield >> 2) & 7)
        {
        case LogicalOperator::AND: bitfield |= (((bitfield & 1) & ((bitfield & 2) >> 1)) << 5); break;
        case LogicalOperator::OR: bitfield |= (((bitfield & 1) | ((bitfield & 2) >> 1)) << 5); break;
        case LogicalOperator::NAND: bitfield |= ((!((bitfield & 1) & ((bitfield & 2) >> 1))) << 5); break;
        case LogicalOperator::NOR: bitfield |= ((!((bitfield & 1) | ((bitfield & 2) >> 1))) << 5); break;
        case LogicalOperator::XOR: bitfield |= ((((bitfield & 1) + ((bitfield & 2) >> 1)) & 1) << 5); break;
        case LogicalOperator::XNOR:  bitfield |= ((!(((bitfield & 1) + ((bitfield & 2) >> 1)) & 1)) << 5); break;
        default: break;
        }
    }

    __excec_location__ inline bool LogicalInput::GetResult() const
    { 
        return bitfield >> 5; 
    }

}