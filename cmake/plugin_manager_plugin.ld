/*
 * Constrained Cortex-M plugin image layout.
 * The application supplies MEMORY origins and sizes through the linker.
 * This image has no vector table, reset handler, or Zephyr kernel globals.
 */
SECTIONS
{
    .text : ALIGN(4)
    {
        __pm_text_start = .;
        KEEP(*(.text.pm_plugin_get_descriptor))
        *(.text*)
        __pm_text_end = .;
    } > PM_PLUGIN_TEXT

    .rodata : ALIGN(4)
    {
        __pm_rodata_start = .;
        KEEP(*(.rodata.pm_descriptor))
        *(.rodata*)
        __pm_rodata_end = .;
    } > PM_PLUGIN_RODATA

    .data : ALIGN(4)
    {
        __pm_data_start = .;
        *(.data*)
        __pm_data_end = .;
    } > PM_PLUGIN_DATA AT> PM_PLUGIN_TEXT

    .bss (NOLOAD) : ALIGN(4)
    {
        __pm_bss_start = .;
        *(.bss*) *(COMMON)
        __pm_bss_end = .;
    } > PM_PLUGIN_DATA

    /DISCARD/ :
    {
        *(.isr_vector) *(.init) *(.fini) *(.ARM.exidx*) *(.eh_frame*)
    }
}
