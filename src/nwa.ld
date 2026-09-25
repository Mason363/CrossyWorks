/* Merge input sections for the relocatable .nwa output (fewer section headers). */
SECTIONS {
  .text : { *(.text .text.*) }
  .rodata.eadk_app_name : { KEEP(*(.rodata.eadk_app_name)) }
  .rodata.eadk_api_level : { KEEP(*(.rodata.eadk_api_level)) }
  .rodata.eadk_app_icon : { KEEP(*(.rodata.eadk_app_icon)) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
  /DISCARD/ : { *(.comment) *(.ARM.attributes) *(.note*) }
}
