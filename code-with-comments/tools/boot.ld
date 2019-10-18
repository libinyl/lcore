OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)

/**          .data
 *           .startup
 * 0x7C00    .text
 */

SECTIONS {
    . = 0x7C00;

    .startup : {
        *bootasm.o(.text)
    }

    .text : { *(.text) }
    .data : { *(.data .rodata) }
    
    /DISCARD/ : { *(.eh_*) }
}
