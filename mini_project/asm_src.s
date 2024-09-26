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
   .def setControlReg
   .def getPsp
   .def getMsp
   .def getStackDump

;-----------------------------------------------------------------------------
; Register values and large immediate values
;-----------------------------------------------------------------------------

.thumb
.const

setPsp:
	MSR PSP, R0		; an address is passed in R0, and PSP is set to it
	BX LR

setControlReg:
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

getStackDump:
    MOV R3, #0        		; R3 used as an index for SP
    LDR R2, [R1, #0]  		; Load value of R0 from stack
    STR R2, [R0, R3]  		; Store R0 value at regData[0]
    ADD R3, #4	      		; Increment index, 4 bytes in 32-bit(1 reg/arr-index)
    LDR R2, [R1, #4]  		; Load value of R1 from stack
    STR R2, [R0, R3]  		; Store R1 value at regData[1]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, #8] 		; Load value of R2 from stack
    STR R2, [R0, R3]  		; Store R2 value at regData[2]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, #12] 		; Load value of R3 from stack
    STR R2, [R0, R3]  		; Store R3 value at regData[3]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, #16] 		; Load value of R12 from stack
    STR R2, [R0, R3]  		; Store R12 value at regData[4]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, #20] 		; Load value of LR from stack
    STR R2, [R0, R3]  		; Store LR value at regData[5]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, #24] 		; Load value of PC from stack
    STR R2, [R0, R3]  		; Store PC value at regData[6]
    ADD R3, #4	      		; Increment index
    LDR R2, [R1, #28] 		; Load value of xPSR from stack
    STR R2, [R0, R3]  		; Store xPSR value at regData[7]
    BX LR
