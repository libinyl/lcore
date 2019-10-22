.text
.globl switch_to

# struct context {
#     uint32_t eip;
#     uint32_t esp;
#     uint32_t ebx;
#     uint32_t ecx;
#     uint32_t edx;
#     uint32_t esi;
#     uint32_t edi;
#     uint32_t ebp;
# };

# switch_to(struct context *from, struct context *to);
# 注意 context 在 PCB 中以结构体而非指针类型保存.
# 注意函数栈帧, 进入函数体内之后,从上到下的栈内参数有三: &(next->context), &(prev->context), 返回地址.

switch_to:                      # switch_to(from, to)

    # 在栈上保存 from 的执行现场
    movl 4(%esp), %eax          # eax 指向 from,其实也指向了 from 的 eip
    popl 0(%eax)                # pop 前,esp 指向调用者的 eip,即返回地址,pop 后直接把 eip 赋给了 from 的 eip 变量. 然后 esp 指向了 from.
    movl %esp, 4(%eax)          # 把当前各种寄存器的值保存在 from context 的内存中.
    movl %ebx, 8(%eax)
    movl %ecx, 12(%eax)
    movl %edx, 16(%eax)
    movl %esi, 20(%eax)
    movl %edi, 24(%eax)
    movl %ebp, 28(%eax)

    # 把 to 中的值赋给寄存器
    movl 4(%esp), %eax          # not 8(%esp): popped return address already. to context 的地址赋给 eax.
                                # eax 指向了 to
    movl 28(%eax), %ebp         # 
    movl 24(%eax), %edi
    movl 20(%eax), %esi
    movl 16(%eax), %edx
    movl 12(%eax), %ecx
    movl 8(%eax), %ebx
    movl 4(%eax), %esp

    pushl 0(%eax)               # push eip  当前 eax指向to 的eip.这里把它入栈,这样执行 ret 就会执行到to 的控制流.

    ret

