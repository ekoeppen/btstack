SECTIONS
{
/* Put sections we need to copy first to reduce total file size */
  .text 0x00000 : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data.rel.ro : { *(.data.*) }
  .plt : { *(.plt*) }
  .got : { *(.got*) }
/* Other sections */
  .dynamic : { *(.dynamic*) }
  .hash : { *(.hash) }
  .rel.dyn : { *(.rel.dyn) }
  .dynstr : { *(.dynstr) }
  .dynsym : { *(.dynsym) }
  .gnu.version : { *(.gnu.version) }
  .gnu.version_d : { *(.gnu.version_d) }
  .symtab : { *(.symtab) }
  .strtab : { *(.strtab) }
  .shstrtab : { *(.shstrtab) }
}
