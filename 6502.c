#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "6502.h"

uint8_t memory[1<<16];
uint8_t A;
uint8_t X;
uint8_t Y;
uint16_t PC;
uint8_t SP;
uint8_t extra_cycles;
uint64_t total_cycles;
union StatusReg SR;

int lengths[NUM_MODES]; // instruction length table, indexed by addressing mode
uint8_t * (*get_ptr[NUM_MODES])(); // addressing mode decoder table
Instruction instructions[0x100]; // instruction data table
Instruction inst; // the current instruction (used for convenience)
int jumping; // used to check that we don't need to increment the PC after a jump
void * read_addr;
void * write_addr;

/* Flag Checks */

static inline void N_flag(int8_t val)
{
	SR.bits.sign = val < 0;
}

static inline void Z_flag(uint8_t val)
{
	SR.bits.zero = val == 0;
}

/* Stack Helpers */

static inline void stack_push(uint8_t val)
{
	memory[0x100+(SP--)] = val;
}

static inline uint8_t stack_pull()
{
	return memory[0x100+(++SP)];
}

/* Memory read/write wrappers */

static inline uint8_t * read_ptr()
{
	return read_addr = get_ptr[inst.mode]();
}

static inline uint8_t * write_ptr()
{
	return write_addr = get_ptr[inst.mode]();
}

/* Branch logic common to all branch instructions */

static inline void take_branch()
{
	uint16_t oldPC;
	oldPC = PC + 2; // PC has already moved to point to the next instruction
	PC = read_ptr() - memory;
	if ((PC ^ oldPC) & 0xff00) extra_cycles += 1; // addr crosses page boundary
	extra_cycles += 1;
}

/* Instruction Implementations */

static void inst_ADC()
{
	uint8_t operand = * read_ptr();
	unsigned int tmp = A + operand + (SR.bits.carry & 1);
	if (SR.bits.decimal) {
		tmp = (A & 0x0f) + (operand & 0x0f) + (SR.bits.carry & 1);
		if (tmp >= 10) tmp = (tmp - 10) | 0x10;
		tmp += (A & 0xf0) + (operand & 0xf0);
		if (tmp > 0x9f) tmp += 0x60;
	}
	SR.bits.carry = tmp > 0xFF;
	SR.bits.overflow =  ((A^tmp)&(operand^tmp)&0x80) != 0;
	A = tmp & 0xFF;
	N_flag(A);
	Z_flag(A);
}

static void inst_AND()
{
	A &= * read_ptr();
	N_flag(A);
	Z_flag(A);
}

static void inst_ASL()
{
	uint8_t tmp = * read_ptr();
	SR.bits.carry = (tmp & 0x80) != 0;
	tmp <<= 1;
	N_flag(tmp);
	Z_flag(tmp);
	* write_ptr() = tmp;
}

static void inst_BCC()
{
	if (!SR.bits.carry) {
		take_branch();
	}
}

static void inst_BCS()
{
	if (SR.bits.carry) {
		take_branch();
	}
}

static void inst_BEQ()
{
	if (SR.bits.zero) {
		take_branch();
	}
}

static void inst_BIT()
{
	uint8_t tmp = * read_ptr();
	N_flag(tmp);
	Z_flag(tmp & A);
	SR.bits.overflow = (tmp & 0x40) != 0;
}

static void inst_BMI()
{
	if (SR.bits.sign) {
		take_branch();
	}
}

static void inst_BNE()
{
	if (!SR.bits.zero) {
		take_branch();
	}
}

static void inst_BPL()
{
	if (!SR.bits.sign) {
		take_branch();
	}
}

static void inst_BRK()
{
	uint16_t newPC;
	memcpy(&newPC, &memory[IRQ_VEC], sizeof(newPC));
	PC += 2;
	stack_push(PC >> 8);
	stack_push(PC & 0xFF);
	SR.bits.brk = 1;
	stack_push(SR.byte);
	SR.bits.interrupt = 1;
	PC = newPC;
	jumping = 1;
}

static void inst_BVC()
{
	if (!SR.bits.overflow) {
		take_branch();
	}
}

static void inst_BVS()
{
	if (SR.bits.overflow) {
		take_branch();
	}
}

static void inst_CLC()
{
	SR.bits.carry = 0;
}

static void inst_CLD()
{
	SR.bits.decimal = 0;
}

static void inst_CLI()
{
	SR.bits.interrupt = 0;
}

static void inst_CLV()
{
	SR.bits.overflow = 0;
}

static void inst_CMP()
{
	uint8_t operand = * read_ptr();
	uint8_t tmpDiff = A - operand;
	N_flag(tmpDiff);
	Z_flag(tmpDiff);
	SR.bits.carry = A >= operand;
}

static void inst_CPX()
{
	uint8_t operand = * read_ptr();
	uint8_t tmpDiff = X - operand;
	N_flag(tmpDiff);
	Z_flag(tmpDiff);
	SR.bits.carry = X >= operand;
}

static void inst_CPY()
{
	uint8_t operand = * read_ptr();
	uint8_t tmpDiff = Y - operand;
	N_flag(tmpDiff);
	Z_flag(tmpDiff);
	SR.bits.carry = Y >= operand;
}

static void inst_DEC()
{
	uint8_t tmp = * read_ptr();
	tmp--;
	N_flag(tmp);
	Z_flag(tmp);
	* write_ptr() = tmp;
}

static void inst_DEX()
{
	X--;
	N_flag(X);
	Z_flag(X);
}

static void inst_DEY()
{
	Y--;
	N_flag(Y);
	Z_flag(Y);
}

static void inst_EOR()
{
	A ^= * read_ptr();
	N_flag(A);
	Z_flag(A);
}

static void inst_INC()
{
	uint8_t tmp = * read_ptr();
	tmp++;
	N_flag(tmp);
	Z_flag(tmp);
	* write_ptr() = tmp;
}

static void inst_INX()
{
	X++;
	N_flag(X);
	Z_flag(X);
}

static void inst_INY()
{
	Y++;
	N_flag(Y);
	Z_flag(Y);
}

static void inst_JMP()
{
	PC = read_ptr() - memory;
	jumping = 1;
}

static void inst_JSR()
{
	uint16_t newPC = read_ptr() - memory;
	PC += 2;
	stack_push(PC >> 8);
	stack_push(PC & 0xFF);
	PC = newPC;
	jumping = 1;
}

static void inst_LDA()
{
	A = * read_ptr();
	N_flag(A);
	Z_flag(A);
}

static void inst_LDX()
{
	X = * read_ptr();
	N_flag(X);
	Z_flag(X);
}

static void inst_LDY()
{
	Y = * read_ptr();
	N_flag(Y);
	Z_flag(Y);
}

static void inst_LSR()
{
	uint8_t tmp = * read_ptr();
	SR.bits.carry = tmp & 1;
	tmp >>= 1;
	N_flag(tmp);
	Z_flag(tmp);
	* write_ptr() = tmp;
}

static void inst_NOP()
{
	// thrown away, just used to compute any extra cycles for the multi-byte
	// NOP statements
	read_ptr();
}

static void inst_ORA()
{
	A |= * read_ptr();
	N_flag(A);
	Z_flag(A);
}

static void inst_PHA()
{
	stack_push(A);
}

static void inst_PHP()
{
	union StatusReg pushed_sr;

	// PHP sets the BRK flag in the byte that is pushed onto the stack,
	// but doesn't affect the status register itself. this is slightly
	// unexpected, but it's what the real hardware does.
	//
	// See http://visual6502.org/wiki/index.php?title=6502_BRK_and_B_bit
	pushed_sr.byte = SR.byte;
	pushed_sr.bits.brk = 1;
	stack_push(pushed_sr.byte);
}

static void inst_PLA()
{
	A = stack_pull();
	N_flag(A);
	Z_flag(A);
}

static void inst_PLP()
{
	SR.byte = stack_pull();
	SR.bits.unused = 1;
	SR.bits.brk = 0;
}

static void inst_ROL()
{
	int tmp = (* read_ptr()) << 1;
	tmp |= SR.bits.carry & 1;
	SR.bits.carry = tmp > 0xFF;
	tmp &= 0xFF;
	N_flag(tmp);
	Z_flag(tmp);
	* write_ptr() = tmp;
}

static void inst_ROR()
{
	int tmp = * read_ptr();
	tmp |= SR.bits.carry << 8;
	SR.bits.carry = tmp & 1;
	tmp >>= 1;
	N_flag(tmp);
	Z_flag(tmp);
	* write_ptr() = tmp;
}

static void inst_RTI()
{
	SR.byte = stack_pull();
	SR.bits.unused = 1;
	PC = stack_pull();
	PC |= stack_pull() << 8;
	//PC += 1;
	jumping = 1;
}

static void inst_RTS()
{
	PC = stack_pull();
	PC |= stack_pull() << 8;
	PC += 1;
	jumping = 1;
}

static void inst_SBC()
{
	uint8_t operand = * read_ptr();
	unsigned int tmp, lo, hi;
	tmp = A - operand - 1 + (SR.bits.carry & 1);
	SR.bits.overflow = ((A^tmp)&(A^operand)&0x80) != 0;
	if (SR.bits.decimal) {
		lo = (A & 0x0f) - (operand & 0x0f) - 1 + SR.bits.carry;
		hi = (A >> 4) - (operand >> 4);
		if (lo & 0x10) lo -= 6, hi--;
		if (hi & 0x10) hi -= 6;
		A = (hi << 4) | (lo & 0x0f);
	}
	else {
		A = tmp & 0xFF;
	}
	SR.bits.carry = tmp < 0x100;
	N_flag(A);
	Z_flag(A);
}

static void inst_SEC()
{
	SR.bits.carry = 1;
}

static void inst_SED()
{
	SR.bits.decimal = 1;
}

static void inst_SEI()
{
	SR.bits.interrupt = 1;
}

static void inst_STA()
{
	* write_ptr() = A;
	extra_cycles = 0; // STA has no addressing modes that use the extra cycle
}

static void inst_STX()
{
	* write_ptr() = X;
}

static void inst_STY()
{
	* write_ptr() = Y;
}

static void inst_TAX()
{
	X = A;
	N_flag(X);
	Z_flag(X);
}

static void inst_TAY()
{
	Y = A;
	N_flag(Y);
	Z_flag(Y);
}

static void inst_TSX()
{
	X = SP;
	N_flag(X);
	Z_flag(X);
}

static void inst_TXA()
{
	A = X;
	N_flag(A);
	Z_flag(A);
}

static void inst_TXS()
{
	SP = X;
}

static void inst_TYA()
{
	A = Y;
	N_flag(A);
	Z_flag(A);
}

/* Addressing Implementations */

uint8_t * get_IMPL()
{
	// dummy implementation; for completeness necessary for cycle counting NOP
	// instructions
	return &memory[0];
}

uint8_t * get_IMM()
{
	return &memory[(uint16_t) (PC+1)];
}

uint16_t get_uint16()
{ // used only as part of other modes
	uint16_t index;
	memcpy(&index, get_IMM(), sizeof(index)); // hooray for optimising compilers
	return index;
}

uint8_t * get_ZP()
{
	return &memory[* get_IMM()];
}

uint8_t * get_ZPX()
{
	return &memory[((* get_IMM()) + X) & 0xFF];
}

uint8_t * get_ZPY()
{
	return &memory[((* get_IMM()) + Y) & 0xFF];
}

uint8_t * get_ACC()
{
	return &A;
}

uint8_t * get_ABS()
{
	return &memory[get_uint16()];
}

uint8_t * get_ABSX()
{
	uint16_t ptr;
	ptr = (uint16_t)(get_uint16() + X);
	if ((uint8_t)ptr < X) extra_cycles ++;
	return &memory[ptr];
}

uint8_t * get_ABSY()
{
	uint16_t ptr;
	ptr = (uint16_t)(get_uint16() + Y);
	if ((uint8_t)ptr < Y) extra_cycles ++;
	return &memory[ptr];
}

uint8_t * get_IND()
{
	uint16_t ptr;
	memcpy(&ptr, get_ABS(), sizeof(ptr));
	return &memory[ptr];
}

uint8_t * get_XIND()
{
	uint16_t ptr;
	ptr = ((* get_IMM()) + X) & 0xFF;
	if (ptr == 0xff) { // check for wraparound in zero page
		ptr = memory[ptr] + (memory[ptr & 0xff00] << 8);
	}
	else {
		memcpy(&ptr, &memory[ptr], sizeof(ptr));
	}
	return &memory[ptr];
}

uint8_t * get_INDY()
{
	uint16_t ptr;
	ptr = * get_IMM();
	if (ptr == 0xff) { // check for wraparound in zero page
		ptr = memory[ptr] + (memory[ptr & 0xff00] << 8);
	}
	else {
		memcpy(&ptr, &memory[ptr], sizeof(ptr));
	}
	ptr += Y;
	if ((uint8_t)ptr < Y) extra_cycles ++;
	return &memory[ptr];
}

uint8_t * get_REL()
{
	return &memory[(uint16_t) (PC + (int8_t) * get_IMM())];
}

uint8_t * get_JMP_IND_BUG()
{
	uint8_t * addr;
	uint16_t ptr;

	ptr = get_uint16();
	if ((ptr & 0xff) == 0xff) {
		// Bug when crosses a page boundary. When using relative index ($xxff),
		// instead of using the last byte of the page and the first byte of the
		// next page, it uses the first byte of the same page. E.g. jmp ($baff)
		// would use the value at $baff as the LSB, but $ba00 as the high byte
		// instead of $bb00. This was fixed in the 65C02
		ptr = memory[ptr] + (memory[ptr & 0xff00] << 8);

	}
	else {
		addr = &memory[ptr];
		memcpy(&ptr, addr, sizeof(ptr));
	}
	return &memory[ptr];
}


/* Construction of Tables */

void init_tables() // this is only done at runtime to improve code readability.
{
	/* Instruction Lengths */

	lengths[ACC]	= 1;
	lengths[ABS]	= 3;
	lengths[ABSX]	= 3;
	lengths[ABSY]	= 3;
	lengths[IMM]	= 2;
	lengths[IMPL]	= 1;
	lengths[IND]	= 3;
	lengths[XIND]	= 2;
	lengths[INDY]	= 2;
	lengths[REL]	= 2;
	lengths[ZP]		= 2;
	lengths[ZPX]	= 2;
	lengths[ZPY]	= 2;
	lengths[JMP_IND_BUG] = 3;

	/* Addressing Modes */
	
	get_ptr[ACC]	= get_ACC;
	get_ptr[ABS]	= get_ABS;
	get_ptr[ABSX]	= get_ABSX;
	get_ptr[ABSY]	= get_ABSY;
	get_ptr[IMM]	= get_IMM;
	get_ptr[IMPL]	= get_IMPL;
	get_ptr[IND]	= get_IND;
	get_ptr[XIND]	= get_XIND;
	get_ptr[INDY]	= get_INDY;
	get_ptr[REL]	= get_REL;
	get_ptr[ZP]	= get_ZP;
	get_ptr[ZPX]	= get_ZPX;
	get_ptr[ZPY]	= get_ZPY;
	get_ptr[JMP_IND_BUG] = get_JMP_IND_BUG;

	/* Instructions */
	
	instructions[0x00] = (Instruction) {"BRK impl", inst_BRK, IMPL, 7};
	instructions[0x01] = (Instruction) {"ORA X,ind", inst_ORA, XIND, 6};
	instructions[0x02] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x03] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x04] = (Instruction) {"???", inst_NOP, ZP, 3};
	instructions[0x05] = (Instruction) {"ORA zpg", inst_ORA, ZP, 3};
	instructions[0x06] = (Instruction) {"ASL zpg", inst_ASL, ZP, 5};
	instructions[0x07] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x08] = (Instruction) {"PHP impl", inst_PHP, IMPL, 3};
	instructions[0x09] = (Instruction) {"ORA #", inst_ORA, IMM, 2};
	instructions[0x0A] = (Instruction) {"ASL A", inst_ASL, ACC, 2};
	instructions[0x0B] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x0C] = (Instruction) {"???", inst_NOP, ABS, 4};
	instructions[0x0D] = (Instruction) {"ORA abs", inst_ORA, ABS, 4};
	instructions[0x0E] = (Instruction) {"ASL abs", inst_ASL, ABS, 6};
	instructions[0x0F] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x10] = (Instruction) {"BPL rel", inst_BPL, REL, 2};
	instructions[0x11] = (Instruction) {"ORA ind,Y", inst_ORA, INDY, 5};
	instructions[0x12] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x13] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x14] = (Instruction) {"???", inst_NOP, ZP, 4};
	instructions[0x15] = (Instruction) {"ORA zpg,X", inst_ORA, ZPX, 4};
	instructions[0x16] = (Instruction) {"ASL zpg,X", inst_ASL, ZPX, 6};
	instructions[0x17] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x18] = (Instruction) {"CLC impl", inst_CLC, IMPL, 2};
	instructions[0x19] = (Instruction) {"ORA abs,Y", inst_ORA, ABSY, 4};
	instructions[0x1A] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x1B] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x1C] = (Instruction) {"???", inst_NOP, ABSX, 4};
	instructions[0x1D] = (Instruction) {"ORA abs,X", inst_ORA, ABSX, 4};
	instructions[0x1E] = (Instruction) {"ASL abs,X", inst_ASL, ABSX, 7};
	instructions[0x1F] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x20] = (Instruction) {"JSR abs", inst_JSR, ABS, 6};
	instructions[0x21] = (Instruction) {"AND X,ind", inst_AND, XIND, 6};
	instructions[0x22] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x23] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x24] = (Instruction) {"BIT zpg", inst_BIT, ZP, 3};
	instructions[0x25] = (Instruction) {"AND zpg", inst_AND, ZP, 3};
	instructions[0x26] = (Instruction) {"ROL zpg", inst_ROL, ZP, 5};
	instructions[0x27] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x28] = (Instruction) {"PLP impl", inst_PLP, IMPL, 4};
	instructions[0x29] = (Instruction) {"AND #", inst_AND, IMM, 2};
	instructions[0x2A] = (Instruction) {"ROL A", inst_ROL, ACC, 2};
	instructions[0x2B] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x2C] = (Instruction) {"BIT abs", inst_BIT, ABS, 4};
	instructions[0x2D] = (Instruction) {"AND abs", inst_AND, ABS, 4};
	instructions[0x2E] = (Instruction) {"ROL abs", inst_ROL, ABS, 6};
	instructions[0x2F] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x30] = (Instruction) {"BMI rel", inst_BMI, REL, 2};
	instructions[0x31] = (Instruction) {"AND ind,Y", inst_AND, INDY, 5};
	instructions[0x32] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x33] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x34] = (Instruction) {"???", inst_NOP, ZP, 4};
	instructions[0x35] = (Instruction) {"AND zpg,X", inst_AND, ZPX, 4};
	instructions[0x36] = (Instruction) {"ROL zpg,X", inst_ROL, ZPX, 6};
	instructions[0x37] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x38] = (Instruction) {"SEC impl", inst_SEC, IMPL, 2};
	instructions[0x39] = (Instruction) {"AND abs,Y", inst_AND, ABSY, 4};
	instructions[0x3A] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x3B] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x3C] = (Instruction) {"???", inst_NOP, ABSX, 4};
	instructions[0x3D] = (Instruction) {"AND abs,X", inst_AND, ABSX, 4};
	instructions[0x3E] = (Instruction) {"ROL abs,X", inst_ROL, ABSX, 7};
	instructions[0x3F] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x40] = (Instruction) {"RTI impl", inst_RTI, IMPL, 6};
	instructions[0x41] = (Instruction) {"EOR X,ind", inst_EOR, XIND, 6};
	instructions[0x42] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x43] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x44] = (Instruction) {"???", inst_NOP, ZP, 3};
	instructions[0x45] = (Instruction) {"EOR zpg", inst_EOR, ZP, 3};
	instructions[0x46] = (Instruction) {"LSR zpg", inst_LSR, ZP, 5};
	instructions[0x47] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x48] = (Instruction) {"PHA impl", inst_PHA, IMPL, 3};
	instructions[0x49] = (Instruction) {"EOR #", inst_EOR, IMM, 2};
	instructions[0x4A] = (Instruction) {"LSR A", inst_LSR, ACC, 2};
	instructions[0x4B] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x4C] = (Instruction) {"JMP abs", inst_JMP, ABS, 3};
	instructions[0x4D] = (Instruction) {"EOR abs", inst_EOR, ABS, 4};
	instructions[0x4E] = (Instruction) {"LSR abs", inst_LSR, ABS, 6};
	instructions[0x4F] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x50] = (Instruction) {"BVC rel", inst_BVC, REL, 2};
	instructions[0x51] = (Instruction) {"EOR ind,Y", inst_EOR, INDY, 5};
	instructions[0x52] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x53] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x54] = (Instruction) {"???", inst_NOP, ZP, 4};
	instructions[0x55] = (Instruction) {"EOR zpg,X", inst_EOR, ZPX, 4};
	instructions[0x56] = (Instruction) {"LSR zpg,X", inst_LSR, ZPX, 6};
	instructions[0x57] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x58] = (Instruction) {"CLI impl", inst_CLI, IMPL, 2};
	instructions[0x59] = (Instruction) {"EOR abs,Y", inst_EOR, ABSY, 4};
	instructions[0x5A] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x5B] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x5C] = (Instruction) {"???", inst_NOP, ABSX, 4};
	instructions[0x5D] = (Instruction) {"EOR abs,X", inst_EOR, ABSX, 4};
	instructions[0x5E] = (Instruction) {"LSR abs,X", inst_LSR, ABSX, 7};
	instructions[0x5F] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x60] = (Instruction) {"RTS impl", inst_RTS, IMPL, 6};
	instructions[0x61] = (Instruction) {"ADC X,ind", inst_ADC, XIND, 6};
	instructions[0x62] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x63] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x64] = (Instruction) {"???", inst_NOP, ZP, 3};
	instructions[0x65] = (Instruction) {"ADC zpg", inst_ADC, ZP, 3};
	instructions[0x66] = (Instruction) {"ROR zpg", inst_ROR, ZP, 5};
	instructions[0x67] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x68] = (Instruction) {"PLA impl", inst_PLA, IMPL, 4};
	instructions[0x69] = (Instruction) {"ADC #", inst_ADC, IMM, 2};
	instructions[0x6A] = (Instruction) {"ROR A", inst_ROR, ACC, 2};
	instructions[0x6B] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x6C] = (Instruction) {"JMP ind", inst_JMP, JMP_IND_BUG, 5};
	instructions[0x6D] = (Instruction) {"ADC abs", inst_ADC, ABS, 4};
	instructions[0x6E] = (Instruction) {"ROR abs", inst_ROR, ABS, 6};
	instructions[0x6F] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x70] = (Instruction) {"BVS rel", inst_BVS, REL, 2};
	instructions[0x71] = (Instruction) {"ADC ind,Y", inst_ADC, INDY, 5};
	instructions[0x72] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x73] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0x74] = (Instruction) {"???", inst_NOP, ZP, 4};
	instructions[0x75] = (Instruction) {"ADC zpg,X", inst_ADC, ZPX, 4};
	instructions[0x76] = (Instruction) {"ROR zpg,X", inst_ROR, ZPX, 6};
	instructions[0x77] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x78] = (Instruction) {"SEI impl", inst_SEI, IMPL, 2};
	instructions[0x79] = (Instruction) {"ADC abs,Y", inst_ADC, ABSY, 4};
	instructions[0x7A] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x7B] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x7C] = (Instruction) {"???", inst_NOP, ABSX, 4};
	instructions[0x7D] = (Instruction) {"ADC abs,X", inst_ADC, ABSX, 4};
	instructions[0x7E] = (Instruction) {"ROR abs,X", inst_ROR, ABSX, 7};
	instructions[0x7F] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0x80] = (Instruction) {"???", inst_NOP, IMM, 2};
	instructions[0x81] = (Instruction) {"STA X,ind", inst_STA, XIND, 6};
	instructions[0x82] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x83] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x84] = (Instruction) {"STY zpg", inst_STY, ZP, 3};
	instructions[0x85] = (Instruction) {"STA zpg", inst_STA, ZP, 3};
	instructions[0x86] = (Instruction) {"STX zpg", inst_STX, ZP, 3};
	instructions[0x87] = (Instruction) {"???", inst_NOP, IMPL, 3};
	instructions[0x88] = (Instruction) {"DEY impl", inst_DEY, IMPL, 2};
	instructions[0x89] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x8A] = (Instruction) {"TXA impl", inst_TXA, IMPL, 2};
	instructions[0x8B] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x8C] = (Instruction) {"STY abs", inst_STY, ABS, 4};
	instructions[0x8D] = (Instruction) {"STA abs", inst_STA, ABS, 4};
	instructions[0x8E] = (Instruction) {"STX abs", inst_STX, ABS, 4};
	instructions[0x8F] = (Instruction) {"???", inst_NOP, IMPL, 4};
	instructions[0x90] = (Instruction) {"BCC rel", inst_BCC, REL, 2};
	instructions[0x91] = (Instruction) {"STA ind,Y", inst_STA, INDY, 6};
	instructions[0x92] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0x93] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0x94] = (Instruction) {"STY zpg,X", inst_STY, ZPX, 4};
	instructions[0x95] = (Instruction) {"STA zpg,X", inst_STA, ZPX, 4};
	instructions[0x96] = (Instruction) {"STX zpg,Y", inst_STX, ZPY, 4};
	instructions[0x97] = (Instruction) {"???", inst_NOP, IMPL, 4};
	instructions[0x98] = (Instruction) {"TYA impl", inst_TYA, IMPL, 2};
	instructions[0x99] = (Instruction) {"STA abs,Y", inst_STA, ABSY, 5};
	instructions[0x9A] = (Instruction) {"TXS impl", inst_TXS, IMPL, 2};
	instructions[0x9B] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x9C] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x9D] = (Instruction) {"STA abs,X", inst_STA, ABSX, 5};
	instructions[0x9E] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0x9F] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0xA0] = (Instruction) {"LDY #", inst_LDY, IMM, 2};
	instructions[0xA1] = (Instruction) {"LDA X,ind", inst_LDA, XIND, 6};
	instructions[0xA2] = (Instruction) {"LDX #", inst_LDX, IMM, 2};
	instructions[0xA3] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0xA4] = (Instruction) {"LDY zpg", inst_LDY, ZP, 3};
	instructions[0xA5] = (Instruction) {"LDA zpg", inst_LDA, ZP, 3};
	instructions[0xA6] = (Instruction) {"LDX zpg", inst_LDX, ZP, 3};
	instructions[0xA7] = (Instruction) {"???", inst_NOP, IMPL, 3};
	instructions[0xA8] = (Instruction) {"TAY impl", inst_TAY, IMPL, 2};
	instructions[0xA9] = (Instruction) {"LDA #", inst_LDA, IMM, 2};
	instructions[0xAA] = (Instruction) {"TAX impl", inst_TAX, IMPL, 2};
	instructions[0xAB] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xAC] = (Instruction) {"LDY abs", inst_LDY, ABS, 4};
	instructions[0xAD] = (Instruction) {"LDA abs", inst_LDA, ABS, 4};
	instructions[0xAE] = (Instruction) {"LDX abs", inst_LDX, ABS, 4};
	instructions[0xAF] = (Instruction) {"???", inst_NOP, IMPL, 4};
	instructions[0xB0] = (Instruction) {"BCS rel", inst_BCS, REL, 2};
	instructions[0xB1] = (Instruction) {"LDA ind,Y", inst_LDA, INDY, 5};
	instructions[0xB2] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xB3] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0xB4] = (Instruction) {"LDY zpg,X", inst_LDY, ZPX, 4};
	instructions[0xB5] = (Instruction) {"LDA zpg,X", inst_LDA, ZPX, 4};
	instructions[0xB6] = (Instruction) {"LDX zpg,Y", inst_LDX, ZPY, 4};
	instructions[0xB7] = (Instruction) {"???", inst_NOP, IMPL, 4};
	instructions[0xB8] = (Instruction) {"CLV impl", inst_CLV, IMPL, 2};
	instructions[0xB9] = (Instruction) {"LDA abs,Y", inst_LDA, ABSY, 4};
	instructions[0xBA] = (Instruction) {"TSX impl", inst_TSX, IMPL, 2};
	instructions[0xBB] = (Instruction) {"???", inst_NOP, IMPL, 4};
	instructions[0xBC] = (Instruction) {"LDY abs,X", inst_LDY, ABSX, 4};
	instructions[0xBD] = (Instruction) {"LDA abs,X", inst_LDA, ABSX, 4};
	instructions[0xBE] = (Instruction) {"LDX abs,Y", inst_LDX, ABSY, 4};
	instructions[0xBF] = (Instruction) {"???", inst_NOP, IMPL, 4};
	instructions[0xC0] = (Instruction) {"CPY #", inst_CPY, IMM, 2};
	instructions[0xC1] = (Instruction) {"CMP X,ind", inst_CMP, XIND, 6};
	instructions[0xC2] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xC3] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0xC4] = (Instruction) {"CPY zpg", inst_CPY, ZP, 3};
	instructions[0xC5] = (Instruction) {"CMP zpg", inst_CMP, ZP, 3};
	instructions[0xC6] = (Instruction) {"DEC zpg", inst_DEC, ZP, 5};
	instructions[0xC7] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0xC8] = (Instruction) {"INY impl", inst_INY, IMPL, 2};
	instructions[0xC9] = (Instruction) {"CMP #", inst_CMP, IMM, 2};
	instructions[0xCA] = (Instruction) {"DEX impl", inst_DEX, IMPL, 2};
	instructions[0xCB] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xCC] = (Instruction) {"CPY abs", inst_CPY, ABS, 4};
	instructions[0xCD] = (Instruction) {"CMP abs", inst_CMP, ABS, 4};
	instructions[0xCE] = (Instruction) {"DEC abs", inst_DEC, ABS, 6};
	instructions[0xCF] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0xD0] = (Instruction) {"BNE rel", inst_BNE, REL, 2};
	instructions[0xD1] = (Instruction) {"CMP ind,Y", inst_CMP, INDY, 5};
	instructions[0xD2] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xD3] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0xD4] = (Instruction) {"???", inst_NOP, ZP, 4};
	instructions[0xD5] = (Instruction) {"CMP zpg,X", inst_CMP, ZPX, 4};
	instructions[0xD6] = (Instruction) {"DEC zpg,X", inst_DEC, ZPX, 6};
	instructions[0xD7] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0xD8] = (Instruction) {"CLD impl", inst_CLD, IMPL, 2};
	instructions[0xD9] = (Instruction) {"CMP abs,Y", inst_CMP, ABSY, 4};
	instructions[0xDA] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xDB] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0xDC] = (Instruction) {"???", inst_NOP, ABSX, 4};
	instructions[0xDD] = (Instruction) {"CMP abs,X", inst_CMP, ABSX, 4};
	instructions[0xDE] = (Instruction) {"DEC abs,X", inst_DEC, ABSX, 7};
	instructions[0xDF] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0xE0] = (Instruction) {"CPX #", inst_CPX, IMM, 2};
	instructions[0xE1] = (Instruction) {"SBC X,ind", inst_SBC, XIND, 6};
	instructions[0xE2] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xE3] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0xE4] = (Instruction) {"CPX zpg", inst_CPX, ZP, 3};
	instructions[0xE5] = (Instruction) {"SBC zpg", inst_SBC, ZP, 3};
	instructions[0xE6] = (Instruction) {"INC zpg", inst_INC, ZP, 5};
	instructions[0xE7] = (Instruction) {"???", inst_NOP, IMPL, 5};
	instructions[0xE8] = (Instruction) {"INX impl", inst_INX, IMPL, 2};
	instructions[0xE9] = (Instruction) {"SBC #", inst_SBC, IMM, 2};
	instructions[0xEA] = (Instruction) {"NOP impl", inst_NOP, IMPL, 2};
	instructions[0xEB] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xEC] = (Instruction) {"CPX abs", inst_CPX, ABS, 4};
	instructions[0xED] = (Instruction) {"SBC abs", inst_SBC, ABS, 4};
	instructions[0xEE] = (Instruction) {"INC abs", inst_INC, ABS, 6};
	instructions[0xEF] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0xF0] = (Instruction) {"BEQ rel", inst_BEQ, REL, 2};
	instructions[0xF1] = (Instruction) {"SBC ind,Y", inst_SBC, INDY, 5};
	instructions[0xF2] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xF3] = (Instruction) {"???", inst_NOP, IMPL, 8};
	instructions[0xF4] = (Instruction) {"???", inst_NOP, ZP, 4};
	instructions[0xF5] = (Instruction) {"SBC zpg,X", inst_SBC, ZPX, 4};
	instructions[0xF6] = (Instruction) {"INC zpg,X", inst_INC, ZPX, 6};
	instructions[0xF7] = (Instruction) {"???", inst_NOP, IMPL, 6};
	instructions[0xF8] = (Instruction) {"SED impl", inst_SED, IMPL, 2};
	instructions[0xF9] = (Instruction) {"SBC abs,Y", inst_SBC, ABSY, 4};
	instructions[0xFA] = (Instruction) {"???", inst_NOP, IMPL, 2};
	instructions[0xFB] = (Instruction) {"???", inst_NOP, IMPL, 7};
	instructions[0xFC] = (Instruction) {"???", inst_NOP, ABSX, 4};
	instructions[0xFD] = (Instruction) {"SBC abs,X", inst_SBC, ABSX, 4};
	instructions[0xFE] = (Instruction) {"INC abs,X", inst_INC, ABSX, 7};
	instructions[0xFF] = (Instruction) {"???", inst_NOP, IMPL, 7};
}

void reset_cpu(int _a, int _x, int _y, int _sp, int _sr, int _pc)
{
	A = _a;
	X = _x;
	Y = _y;
	SP = _sp;
	
	SR.byte = _sr;
	SR.bits.interrupt = 1;
	SR.bits.unused = 1;
	
	if (_pc < 0)
		memcpy(&PC, &memory[-_pc], sizeof(PC));
	else
		PC = _pc;

	total_cycles = 0;
}

int load_rom(char * filename, int load_addr)
{
	int loaded_size, max_size;

	memset(memory, 0, sizeof(memory)); // clear ram first
	
	FILE * fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("Error: could not open file\n");
		return -1;
	}
	
	max_size = 0x10000 - load_addr;
	loaded_size = (int)fread(&memory[load_addr], 1, (size_t)max_size, fp);
	fprintf(stderr, "Loaded $%04x bytes: $%04x - $%04x\n", loaded_size, load_addr, load_addr + loaded_size - 1);
	
	fclose(fp);
	return 0;
}

int step_cpu(int verbose) // returns cycle count
{
	inst = instructions[memory[PC]];

	if (verbose) {
		// almost match for NES dump for easier comparison
		printf("%04X  ", PC);
		if (lengths[inst.mode] == 3)
			printf("%02X %02X %02X", memory[PC], memory[PC+1], memory[PC+2]);
		else if (lengths[inst.mode] == 2)
			printf("%02X %02X   ", memory[PC], memory[PC+1]);
		else
			printf("%02X      ", memory[PC]);
		printf("  %-10s                      A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%3d\n", inst.mnemonic, A, X, Y, SR.byte, SP, (int)((total_cycles * 3) % 341));
	}

	jumping = 0;
	extra_cycles = 0;
	inst.function();
	if (jumping == 0) PC += lengths[inst.mode];

	// 7 cycle instructions (e.g. ROL $nnnn,X) don't have a penalty cycle for
	// crossing a page boundary.
	if (inst.cycles == 7) extra_cycles = 0;

	total_cycles += inst.cycles + extra_cycles;
	return inst.cycles + extra_cycles;
}

void save_memory(char * filename) { // dump memory for analysis (slows down emulation significantly)
	if (filename == NULL) filename = "memdump";
	FILE * fp = fopen(filename, "w");
	fwrite(&memory, sizeof(memory), 1, fp);
	fclose(fp);
}
