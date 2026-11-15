; simd_test.asm —— 验证"中断不会破坏 XMM 寄存器"
;
; cpu.h 里的设计前提：中断处理程序都用 -mno-sse 编译、不碰 XMM 寄存器，
; 所以 SSE 计算被中断打断后，回来时 XMM 寄存器原封不动，不需要保存。
; 这里实测这个前提：往 xmm0 ~ xmm7 装入已知的值，然后 hlt 等若干次中断（时钟每 10 毫秒一次），
; 再把 8 个寄存器存出来，由 C 代码逐个比对。
;
; void xmm_interrupt_test(unsigned int wait_count, float *out, const float *in);
;   in / out 都是 8 × 4 = 32 个 float

BITS 32
section .text
global xmm_interrupt_test

xmm_interrupt_test:
    mov eax, [esp + 12]         ; in
    movups xmm0, [eax + 0]
    movups xmm1, [eax + 16]
    movups xmm2, [eax + 32]
    movups xmm3, [eax + 48]
    movups xmm4, [eax + 64]
    movups xmm5, [eax + 80]
    movups xmm6, [eax + 96]
    movups xmm7, [eax + 112]

    mov ecx, [esp + 4]          ; 等这么多次中断
.wait:
    sti
    hlt                         ; 睡到下一次中断；中断处理完回到这里
    loop .wait

    mov eax, [esp + 8]          ; out
    movups [eax + 0],   xmm0
    movups [eax + 16],  xmm1
    movups [eax + 32],  xmm2
    movups [eax + 48],  xmm3
    movups [eax + 64],  xmm4
    movups [eax + 80],  xmm5
    movups [eax + 96],  xmm6
    movups [eax + 112], xmm7
    ret
