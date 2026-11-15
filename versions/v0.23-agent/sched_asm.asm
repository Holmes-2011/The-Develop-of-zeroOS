; sched_asm.asm —— 上下文切换（v0.17）

BITS 32
global switch_to

; void switch_to(unsigned int *save_esp, unsigned int new_esp)
; 保存 callee-saved 寄存器，把当前 esp 写进 *save_esp，换到 new_esp，
; 再"返回"——第一次进新任务时，新栈是我们伪造的帧（见 sched.c）
switch_to:
    push ebp
    push ebx
    push esi
    push edi
    mov eax, [esp + 20]
    mov edx, [esp + 24]
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
