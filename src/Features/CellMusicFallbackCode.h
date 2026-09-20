#pragma once

#include <cstdint>
#include <xbyak/xbyak.h>

class CellMusicFallbackCode : public Xbyak::CodeGenerator
{
public:
	CellMusicFallbackCode(std::uintptr_t singletonSlot, std::uintptr_t continuation)
		: Xbyak::CodeGenerator(64)
	{
		Xbyak::Label missingRegion, done, resume;
		mov(rax, singletonSlot);
		mov(rax, ptr[rax]);
		test(rax, rax);
		jz(missingRegion);
		mov(rdi, ptr[rax + 0x50]);
		jmp(done);
		L(missingRegion);
		xor_(edi, edi);
		L(done);
		jmp(ptr[rip + resume]);
		L(resume);
		dq(continuation);
		ready();
	}
};
