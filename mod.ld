SECTIONS {
    . = 0x81000000;

    .text : {
        *(.text*)
    }

    .recomp_patch : {
        *(.recomp_patch*)
    }

    .recomp_force_patch : {
        *(.recomp_force_patch*)
    }

    .data : {
        *(.data*)
        *(.rodata*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.MIPS.abiflags)
        *(.reginfo)
        *(.pdr)
        *(.comment)
    }
}
