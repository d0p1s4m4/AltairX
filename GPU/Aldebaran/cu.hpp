#ifndef CU_HPP
#define CU_HPP 1

#include <aldebaran.h>
#include <buffer_t.hpp>

struct ArExecute_T
{
	uint32_t unit, size, id, imm;
	int8_t rA, rB, rC;
	float fimm;
	double dimm;
};

struct vec4f
{
	float v[4]{0};

	float& x() { return v[0]; }
	float& y() { return v[1]; }
	float& z() { return v[2]; }
	float& w() { return v[3]; }
};
struct vec8h
{
	uint16_t v[8]{0};

	uint16_t& x() { return v[0]; }
	uint16_t& y() { return v[1]; }
	uint16_t& z() { return v[2]; }
	uint16_t& w() { return v[3]; }
	uint16_t& p() { return v[4]; }
	uint16_t& q() { return v[5]; }
	uint16_t& r() { return v[6]; }
	uint16_t& s() { return v[7]; }
};
struct vec4i
{
	uint32_t v[4]{0};

	uint32_t& x() { return v[0]; }
	uint32_t& y() { return v[1]; }
	uint32_t& z() { return v[2]; }
	uint32_t& w() { return v[3]; }
};

union regs
{
	vec4f vreg[AR_PROCESSOR_VREG_COUNT];
	vec8h hreg[AR_PROCESSOR_VREG_COUNT];
	vec4i ireg[AR_PROCESSOR_VREG_COUNT];

	regs() {}
};

struct ComputeUnit_t
{
	struct GPU_t& gpu;

	const uint32_t* pc;

	//uint32_t opcodes[AR_PROCESSOR_MAX_OPERATIONS];

	//ArExecute_T operations[AR_PROCESSOR_MAX_OPERATIONS];

	//uint64_t instruction;
	//uint64_t cycle;

	regs shaders[N_SHADER];

	//uint16_t delay, delayop, imm;
	//uint16_t pc;  // program-counter
	//uint16_t br;  // buffer-register
	//uint16_t lr;  // link-register

	/// \brief CPU Flags register
	///
	/// Bit 0:
	/// Bit 1: Z flag, 1 if not equal, 0 if equal
	/// Bit 2: S flag, 1 if lesser, 0 if greater (signed comparison)
	/// Bit 3: U flag, 1 if lesser, 0 if greater (unsigned comparison)
	uint16_t flags[N_SHADER];

	//int decode();
	uint32_t execute();

//private:
//	int32_t executeInstructions(uint32_t index);
//	int32_t executeInstruction(size_t shaderi, uint32_t index);
//	int decodeInstruction1();
//
//	int decodeALU_A(uint32_t opcode, ArExecute_T* output);
//	int decodeALU_B(uint32_t opcode, ArExecute_T* output);
//	int decodeLSU_SPM(uint32_t opcode, ArExecute_T* output);
//	int decodeLSU_Cache(uint32_t opcode, ArExecute_T* output);
//	int decodeCMP(uint32_t opcode, ArExecute_T* output);
//	int decodeBRU(uint32_t opcode, ArExecute_T* output);
//	int decodeVBRU(uint32_t opcode, ArExecute_T* output);
//	int decodeDMA(uint32_t opcode, ArExecute_T* output);
//	
//	int decodeInstruction2();
//
//	int decodeALUV_A(uint32_t opcode, ArExecute_T* output);
//	int decodeALUV_B(uint32_t opcode, ArExecute_T* output);
//	int decodeLSUV_SPM(uint32_t opcode, ArExecute_T* output);
//	int decodeLSUV_Cache(uint32_t opcode, ArExecute_T* output);
//	int decodeVCMP(uint32_t opcode, ArExecute_T* output);
//	int decodeVFPU_A(uint32_t opcode, ArExecute_T* output);
//	int decodeVFPU_B(uint32_t opcode, ArExecute_T* output);
//	int decodeVFPU_C(uint32_t opcode, ArExecute_T* output);
//	int decodeEFU(uint32_t opcode, ArExecute_T* output);
//	int decodePPU(uint32_t opcode, ArExecute_T* output);
//
//	void executeLS(void* reg, uint64_t offset, uint32_t size, uint32_t store);
//
//	int32_t executeALU(uint32_t unit2, vec4i* ireg, int32_t rA, int32_t opB,
//	                   int32_t opC);
//	int32_t executeLSU(uint32_t unit2, vec4f* freg, int32_t rA, int32_t opB,
//	                   int32_t opC);
//	int32_t executeCMP(uint32_t unit2, vec4i* ireg, int32_t rA, int32_t opB,
//	                   int32_t opC, const float* fopA, const float* fopB,
//	                   uint32_t size, uint32_t id);
//	int32_t executeVFPU(const ArExecute_T& op, vec4f* freg, int32_t rA, int32_t opB,
//	                    int32_t opC, const float* fopA, const float* fopB,
//	                    uint32_t size, uint32_t id);
//	int32_t executeDMA(uint32_t unit2, vec4i* ireg, int32_t rA, int32_t opB,
//	                   int32_t opC);
//	int32_t executeOther(uint32_t unit2, vec4i* ireg, vec4f* freg, vec8h* hreg,
//	                     int32_t rA, int32_t opB);
};

#endif  // CU_HPP
