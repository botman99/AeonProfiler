;;
;; Copyright (c) 2015-2018 Jeffrey "botman" Broome
;;

IF _M_IX86

	.586
	.model flat

	.code

	extern	?ProfilerEnter@@YAX_JPAX@Z:near
	extern	?ProfilerExit@@YAX_JPAX@Z:near

;	http://msdn.microsoft.com/en-us/library/984x0h58.aspx

	_Profiler_enter PROC
		PUSH	EAX
		PUSH	EBX
		PUSH	ECX
		PUSH	EDX

		MOV		ECX,[ESP + 10h]
		PUSH	ECX  ; push the callers address

		XOR		EAX, EAX
		XOR		ECX, ECX
		CPUID  ; slower but more accurate across multiple threads running on different cores
		RDTSC
		PUSH	EDX  ; push the counter
		PUSH	EAX

		CALL	?ProfilerEnter@@YAX_JPAX@Z
		ADD		ESP,0Ch  ; "pop" off the counter and caller address

		POP		EDX
		POP		ECX
		POP		EBX
		POP		EAX

		RET
	_Profiler_enter ENDP

	_Profiler_exit PROC
		PUSH	EAX
		PUSH	EBX
		PUSH	ECX
		PUSH	EDX

		MOV		ECX,[ESP + 10h]
		PUSH	ECX  ; push the callers address

		XOR		EAX, EAX
		XOR		ECX, ECX
		CPUID  ; slower but more accurate across multiple threads running on different cores
		RDTSC
		PUSH	EDX  ; push the counter
		PUSH	EAX

		CALL	?ProfilerExit@@YAX_JPAX@Z
		ADD		ESP,0Ch  ; "pop" off the counter and caller address

		POP		EDX
		POP		ECX
		POP		EBX
		POP		EAX

		RET
	_Profiler_exit ENDP

ELSEIF _M_X64

	.code

	extern	?ProfilerEnter@@YAX_JPEAX@Z:near
	extern	?ProfilerExit@@YAX_JPEAX@Z:near

; See https://software.intel.com/en-us/articles/introduction-to-x64-assembly for a good introduction to x64 architecture and calling conventions.

	Profiler_enter PROC

		PUSH	RAX		; save RAX before clobbering it with the flags
		LAHF			; get the flags
		PUSH	RAX		; save the flags

		; we save 14 64-bit registers (14 * 8 bytes) + 6 128-bit registers (6 * 16 bytes) and we offset 24 bytes for the overhead of the ProfileEnter function (return address + RCX + RDX)
		; (14 * 8) + (6 * 16) + 24 = 232 bytes
		SUB		RSP, 232

		; "push" the volatile registers onto the stack (see http://msdn.microsoft.com/en-us/library/ms235286.aspx)
		; RBX, RCX, RDX, RBP, RDI, RSI, R8, R9, R10, R11, R12, R13, R14, R15, XXM0 (16), XXM1 (16), XXM2 (16), XXM3 (16), XXM4 (16), XXM5 (16)

		MOV		[RSP+24], RBX
		MOV		[RSP+32], RCX
		MOV		[RSP+40], RDX
		MOV		[RSP+48], RBP
		MOV		[RSP+56], RDI
		MOV		[RSP+64], RSI
		MOV		[RSP+72], R8
		MOV		[RSP+80], R9
		MOV		[RSP+88], R10
		MOV		[RSP+96], R11
		MOV		[RSP+104], R12
		MOV		[RSP+112], R13
		MOV		[RSP+120], R14
		MOV		[RSP+128], R15

		MOVDQU	OWORD PTR [RSP+136], XMM0  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+152], XMM1  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+168], XMM2  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+184], XMM3  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+200], XMM4  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+216], XMM5  ; use unaligned move (slower but easier)

		XOR		EAX, EAX
		XOR		ECX, ECX
		CPUID  ; slower but more accurate across multiple threads running on different cores
		RDTSC
		SHL		RDX, 20h
		OR		RDX, RAX
		MOV		RCX, RDX  ; store the counter (in first argument)

		MOV		RDX, QWORD PTR [RSP + 248]  ; get the return address off the stack (offset = 232 + (2 * 8) bytes that we pushed) in the second argument

		CALL	?ProfilerEnter@@YAX_JPEAX@Z

		; "pop" the registers back off the stack
		MOVDQU	XMM5, OWORD PTR [RSP+216]  ; use unaligned move (slower but easier)
		MOVDQU	XMM4, OWORD PTR [RSP+200]  ; use unaligned move (slower but easier)
		MOVDQU	XMM3, OWORD PTR [RSP+184]  ; use unaligned move (slower but easier)
		MOVDQU	XMM2, OWORD PTR [RSP+168]  ; use unaligned move (slower but easier)
		MOVDQU	XMM1, OWORD PTR [RSP+152]  ; use unaligned move (slower but easier)
		MOVDQU	XMM0, OWORD PTR [RSP+136]  ; use unaligned move (slower but easier)

		MOV		R15, [RSP+128]
		MOV		R14, [RSP+120]
		MOV		R13, [RSP+112]
		MOV		R12, [RSP+104]
		MOV		R11, [RSP+96]
		MOV		R10, [RSP+88]
		MOV		R9, [RSP+80]
		MOV		R8, [RSP+72]
		MOV		RSI, [RSP+64]
		MOV		RDI, [RSP+56]
		MOV		RBP, [RSP+48]
		MOV		RDX, [RSP+40]
		MOV		RCX, [RSP+32]
		MOV		RBX, [RSP+24]

		ADD		RSP, 232

		POP		RAX		; pop the flags off the stack
		SAHF			; restore the flags
		POP		RAX		; pop original RAX

		RET
	Profiler_enter ENDP

	Profiler_exit PROC

		PUSH	RAX		; save RAX before clobbering it with the flags
		LAHF			; get the flags
		PUSH	RAX		; save the flags

		; we save 14 64-bit registers (14 * 8 bytes) + 6 128-bit registers (6 * 16 bytes) and we offset 24 bytes for the overhead of the ProfileEnter function (return address + RCX + RDX)
		; (14 * 8) + (6 * 16) + 24 = 232 bytes
		SUB		RSP, 232

		; "push" the volatile registers onto the stack (see http://msdn.microsoft.com/en-us/library/ms235286.aspx)
		; RBX, RCX, RDX, RBP, RDI, RSI, R8, R9, R10, R11, R12, R13, R14, R15, XXM0 (16), XXM1 (16), XXM2 (16), XXM3 (16), XXM4 (16), XXM5 (16)

		MOV		[RSP+24], RBX
		MOV		[RSP+32], RCX
		MOV		[RSP+40], RDX
		MOV		[RSP+48], RBP
		MOV		[RSP+56], RDI
		MOV		[RSP+64], RSI
		MOV		[RSP+72], R8
		MOV		[RSP+80], R9
		MOV		[RSP+88], R10
		MOV		[RSP+96], R11
		MOV		[RSP+104], R12
		MOV		[RSP+112], R13
		MOV		[RSP+120], R14
		MOV		[RSP+128], R15

		MOVDQU	OWORD PTR [RSP+136], XMM0  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+152], XMM1  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+168], XMM2  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+184], XMM3  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+200], XMM4  ; use unaligned move (slower but easier)
		MOVDQU	OWORD PTR [RSP+216], XMM5  ; use unaligned move (slower but easier)

		XOR		EAX, EAX
		XOR		ECX, ECX
		CPUID  ; slower but more accurate across multiple threads running on different cores
		RDTSC
		SHL		RDX, 20h
		OR		RDX, RAX
		MOV		RCX, RDX  ; store the counter (in first argument)

		MOV		RDX, QWORD PTR [RSP + 248]  ; get the return address off the stack (offset = 232 + (2 * 8) bytes that we pushed) in the second argument

		CALL	?ProfilerExit@@YAX_JPEAX@Z

		; "pop" the registers back off the stack
		MOVDQU	XMM5, OWORD PTR [RSP+216]  ; use unaligned move (slower but easier)
		MOVDQU	XMM4, OWORD PTR [RSP+200]  ; use unaligned move (slower but easier)
		MOVDQU	XMM3, OWORD PTR [RSP+184]  ; use unaligned move (slower but easier)
		MOVDQU	XMM2, OWORD PTR [RSP+168]  ; use unaligned move (slower but easier)
		MOVDQU	XMM1, OWORD PTR [RSP+152]  ; use unaligned move (slower but easier)
		MOVDQU	XMM0, OWORD PTR [RSP+136]  ; use unaligned move (slower but easier)

		MOV		R15, [RSP+128]
		MOV		R14, [RSP+120]
		MOV		R13, [RSP+112]
		MOV		R12, [RSP+104]
		MOV		R11, [RSP+96]
		MOV		R10, [RSP+88]
		MOV		R9, [RSP+80]
		MOV		R8, [RSP+72]
		MOV		RSI, [RSP+64]
		MOV		RDI, [RSP+56]
		MOV		RBP, [RSP+48]
		MOV		RDX, [RSP+40]
		MOV		RCX, [RSP+32]
		MOV		RBX, [RSP+24]

		ADD		RSP, 232

		POP		RAX		; pop the flags off the stack
		SAHF			; restore the flags
		POP		RAX		; pop original RAX

		RET
	Profiler_exit ENDP

ENDIF

END
