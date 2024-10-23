; All Arm Thumb Assembly Functions
; Deep Shinglot

;-----------------------------------------------------------------------------
; Hardware Target
;-----------------------------------------------------------------------------

; Target Platform: EK-TM4C123GXL
; Target uC:       TM4C123GH6PM
; System Clock:    40 MHz

; Hardware configuration:
; 16 MHz external crystal oscillator

;-----------------------------------------------------------------------------
; Device includes, defines, and assembler directives
;-----------------------------------------------------------------------------

   .def setPsp
   .def goThreadMode
   .def getPsp
   .def getMsp
   .def getStackDump
   .def goUserMode
   .def runFn
   .def restoreRegs
   .def storeRegs
   .def setExecpLr

;-----------------------------------------------------------------------------
; Register values and large immediate values
;-----------------------------------------------------------------------------
execResultValue:
	.word 0xFFFFFFFD

thumbBitInPsr:
	.word  0x1000000

.thumb
.const

setPsp:
	MSR PSP, R0		; an address is passed in R0, and PSP is set to it
	BX LR

goThreadMode:
	MRS R0, CONTROL		; get CONTROL reg value, this way keep previous data
	ORR R0, #2			; assign value to r0 respect with to CONTROL reg as 1 to ASP bit
	MSR CONTROL, R0		; moves the r0 to CONTROL special reg
	ISB					; instruction synchronization barrier
	BX LR

getPsp:
	MRS R0, PSP		; gets address of process stack pointer
	BX LR

getMsp:
	MRS R0, MSP		; gets address of main stack pointer
	BX LR

; Done manually to lable data read
getStackDump:
    MOV R3, #0        		; R3 used as an index for SP
    LDR R2, [R1, R3]  		; Load value of R0 from stack
    STR R2, [R0, R3]  		; Store R0 value at regData[0]
    ADD R3, #4	      		; Increment index, 4 bytes in 32-bit(1 reg/arr-index)
    LDR R2, [R1, R3]  		; Load value of R1 from stack
    STR R2, [R0, R3]  		; Store R1 value at regData[1]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, R3] 		; Load value of R2 from stack
    STR R2, [R0, R3]  		; Store R2 value at regData[2]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, R3] 		; Load value of R3 from stack
    STR R2, [R0, R3]  		; Store R3 value at regData[3]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, R3] 		; Load value of R12 from stack
    STR R2, [R0, R3]  		; Store R12 value at regData[4]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, R3] 		; Load value of LR from stack
    STR R2, [R0, R3]  		; Store LR value at regData[5]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, R3] 		; Load value of PC from stack
    STR R2, [R0, R3]  		; Store PC value at regData[6]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, R3] 		; Load value of xPSR from stack
    STR R2, [R0, R3]  		; Store xPSR value at regData[7]
    BX LR

goUserMode:
	MRS R0, CONTROL			; special register load
	ORR R0, #1				; sets TMPL bit, switches to unprivilege
	MSR CONTROL, R0			; updating CONTROL reg wit TMPL
	BX LR

; writing to addrs in order of faulted PSR stack
runFn:
	PUSH {R2}						; save raw values as they be stored later (not needed but so what)
	MRS R2, EPSR					; special register read to R5 which alway returns 0 (good practice)
	LDR R2, thumbBitInPsr			; sets thumb assembly bit in PSR
	STR R2, [R0, #-4]!				; PSR gets store 4 bytes below addr in R0 and addr updates in R0
	POP {R2}						; restore values of regs
	STR R1, [R0, #-4]!				; PC place (value in R1) gets store 4 bytes below addr in R0 and addr updates in R0
	STMDB R0, {LR,R12,R0-R3}		; stores reglist to R0 address, decrements first, cannnot write-back due to RO being address base and in reglist
	SUB R0, #24						; decrement address, for the stored register, 6 regs in list so 6*4=24 (4 byte per word)
	PUSH {LR}						; store way-back address
	LDR LR, execResultValue			; load LR with execute_result of thread PSP value
	STMDB R0!, {LR,R4-R11}			; stores reglist to R0 address, decrements first and writes back the updated address
	POP {LR}						; restore LR value
	BX LR
; not needed stack address in stack because sp variable in tcb struct
;	SUB R1, R0, #4					; expand stack to push R0 value while preserving R0 value
;	STR R0, [R1]					; store address (R0) points to last R11 pushed

restoreRegs:
	PUSH {LR}				; need this function to return updated sp, so can not BX 0xFFFFFFFD
	MRS R0, PSP				; gets stack continous address
	LDM R0!, {LR,R4-R11}	; load reglist to R0 address, increment after and writes back the updated address
	MSR PSP, R0				; update the stack after POP (LDR)
	POP {LR}				; restore LR value
	BX LR

storeRegs:
	PUSH {LR}						; store way-back address
	MRS R0, PSP						; gets stack continous address
	LDR LR, execResultValue			; load LR with execute_result of thread PSP value
	STMDB R0!, {LR,R4-R11}			; stores reglist to R0 address, decrements first and writes back the updated address
	MSR PSP, R0						; sets stack continous address
	POP {LR}						; restore LR value
	BX LR

setExecpLr:
	LDR LR, execResultValue			; load LR with execute_result of thread PSP valu/e
	BX LR
