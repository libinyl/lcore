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

#                   <- bp
# ...
# 
# &(next->context)  //to
# &(prev->context)  //from
# ret addr          <- sp
# 
# 

switch_to:                      # switch_to(from, to)

    # 在栈上保存 from 的一堆寄存器
    movl 4(%esp), %eax          # eax points to from
    popl 0(%eax)                # save eip !popl
    movl %esp, 4(%eax)
    movl %ebx, 8(%eax)
    movl %ecx, 12(%eax)
    movl %edx, 16(%eax)
    movl %esi, 20(%eax)
    movl %edi, 24(%eax)
    movl %ebp, 28(%eax)

    # 把参数中的 to 赋给寄存器
    movl 4(%esp), %eax          # not 8(%esp): popped return address already
                                # eax now points to to
    movl 28(%eax), %ebp
    movl 24(%eax), %edi
    movl 20(%eax), %esi
    movl 16(%eax), %edx
    movl 12(%eax), %ecx
    movl 8(%eax), %ebx
    movl 4(%eax), %esp

    pushl 0(%eax)               # push eip

    ret

