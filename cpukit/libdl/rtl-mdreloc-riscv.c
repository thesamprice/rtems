/* SPDX-License-Identifier: BSD-2-Clause */

/*-
 * Copyright (c) 2019 Hesham Almatary
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <sys/cdefs.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "rtl-elf.h"
#include "rtl-error.h"
#include "rtl-unwind-dw2.h"
#include "rtl-unwind.h"
#include <rtems/rtl/rtl-trace.h>
#include <rtems/rtl/rtl.h>

uint32_t rtems_rtl_elf_section_flags(const rtems_rtl_obj* obj,
                                     const Elf_Shdr* shdr) {
  (void)obj;
  (void)shdr;

  return 0;
}

uint32_t rtems_rtl_elf_arch_parse_section(const rtems_rtl_obj* obj, int section,
                                          const char* name,
                                          const Elf_Shdr* shdr,
                                          const uint32_t flags) {
  (void)obj;
  (void)section;
  (void)name;
  (void)shdr;
  return flags;
}

bool rtems_rtl_elf_arch_section_alloc(const rtems_rtl_obj* obj,
                                      rtems_rtl_obj_sect* sect) {
  (void)obj;
  (void)sect;
  return false;
}

bool rtems_rtl_elf_arch_section_free(const rtems_rtl_obj* obj,
                                     rtems_rtl_obj_sect* sect) {
  (void)obj;
  (void)sect;
  return false;
}

bool rtems_rtl_elf_rel_resolve_sym(Elf_Word type) {
  (void)type;

  return true;
}

uint32_t rtems_rtl_obj_tramp_alignment(const rtems_rtl_obj* obj) {
  (void)obj;
  return sizeof(uint32_t);
}

size_t rtems_rtl_elf_relocate_tramp_max_size(void) {
  /*
   * Disable by returning 0.
   */
  return 0;
}

rtems_rtl_elf_rel_status rtems_rtl_elf_relocate_rel_tramp(
    rtems_rtl_obj* obj, const Elf_Rel* rel, const rtems_rtl_obj_sect* sect,
    const char* symname, const Elf_Byte syminfo, const Elf_Word symvalue) {
  (void)obj;
  (void)rel;
  (void)sect;
  (void)symname;
  (void)syminfo;
  (void)symvalue;
  return rtems_rtl_elf_rel_no_error;
}

// Extract bits V[Begin:End], where range is inclusive, and Begin must be < 63.
static uint32_t extractBits(uint64_t v, uint32_t begin, uint32_t end) {
  return (v & ((1ULL << (begin + 1)) - 1)) >> end;
}

static int64_t SignExtend64(uint64_t val, unsigned bits) {
  return (int64_t)(((int64_t)(val << (64 - bits))) >> (64 - bits));
}

static void write8le(void* loc, uint8_t val) {
  *((uint8_t*)loc) = val;
}

static uint8_t read8le(void* loc) {
  return *((uint8_t*)loc);
}

static void write16le(void* loc, uint16_t val) {
  *((uint16_t*)loc) = val;
}

static void write32le(void* loc, uint32_t val) {
  if ((((uintptr_t)loc) & 3) == 0) {
    *((uint32_t*)loc) = val;
  } else {
    write8le(loc, (uint8_t)val);
    write8le((char*)loc + 1, (uint8_t)(val >> 8));
    write8le((char*)loc + 2, (uint8_t)(val >> 16));
    write8le((char*)loc + 3, (uint8_t)(val >> 24));
  }
}

static void write64le(void* loc, uint64_t val) {
  if ((((uintptr_t)loc) & 7) == 0) {
    *((uint64_t*)loc) = val;
  } else {
    write32le(loc, (uint32_t)val);
    write32le((char*)loc + 4, (uint32_t)(val >> 32));
  }
}

static uint16_t read16le(void* loc) {
  return *((uint16_t*)loc);
}

static uint32_t read32le(void* loc) {
  if ((((uintptr_t)loc) & 3) == 0) {
    return *((uint32_t*)loc);
  }
  return (uint32_t)read8le(loc) | ((uint32_t)read8le((char*)loc + 1) << 8) |
         ((uint32_t)read8le((char*)loc + 2) << 16) |
         ((uint32_t)read8le((char*)loc + 3) << 24);
}

static uint64_t read64le(void* loc) {
  if ((((uintptr_t)loc) & 7) == 0) {
    return *((uint64_t*)loc);
  }
  return (uint64_t)read32le(loc) |
         ((uint64_t)read32le((char*)loc + 4) << 32);
}

/*
 * Decode the ULEB128 value at loc.  Returns the number of bytes the encoded
 * value occupies, or 0 if it is not terminated within max_len bytes.
 */
static size_t read_uleb128(void* loc, size_t max_len, uint64_t* val) {
  uint64_t v = 0;
  size_t i;
  for (i = 0; i < max_len; ++i) {
    uint8_t byte = read8le((char*)loc + i);
    v |= ((uint64_t)(byte & 0x7f)) << (7 * i);
    if ((byte & 0x80) == 0) {
      *val = v;
      return i + 1;
    }
  }
  return 0;
}

/*
 * Encode val as ULEB128 into exactly len bytes at loc, padding with
 * continuation bytes.  Returns false if the value does not fit.
 */
static bool write_uleb128(void* loc, size_t len, uint64_t val) {
  size_t i;
  for (i = 0; i < len; ++i) {
    uint8_t byte = val & 0x7f;
    val >>= 7;
    if (i + 1 < len) {
      byte |= 0x80;
    }
    write8le((char*)loc + i, byte);
  }
  return val == 0;
}

static rtems_rtl_elf_rel_status
rtems_rtl_elf_reloc_rela(rtems_rtl_obj* obj, const Elf_Rela* rela,
                         const rtems_rtl_obj_sect* sect, const char* symname,
                         const Elf_Byte syminfo, const Elf_Word symvalue,
                         const bool parsing) {
  (void)symname;

  Elf_Addr* where;

  char bits = (sizeof(Elf_Word) * 8);
  where = (Elf_Addr*)(sect->base + rela->r_offset);

  // Symbol value with the addend applied
  Elf_Word target = symvalue + rela->r_addend;

  // Final PCREL value
  Elf_Word pcrel_val = target - ((Elf_Word)(uintptr_t)where);

  if (syminfo == STT_SECTION) {
    return rtems_rtl_elf_rel_no_error;
  }

  if (parsing) {
    return rtems_rtl_elf_rel_no_error;
  }

  switch (ELF_R_TYPE(rela->r_info)) {
  case R_TYPE(NONE):
    break;

  case R_TYPE(RVC_BRANCH): {
    uint16_t insn = ((*where) & 0xFFFF) & 0xE383;
    uint16_t imm8 = extractBits(pcrel_val, 8, 8) << 12;
    uint16_t imm4_3 = extractBits(pcrel_val, 4, 3) << 10;
    uint16_t imm7_6 = extractBits(pcrel_val, 7, 6) << 5;
    uint16_t imm2_1 = extractBits(pcrel_val, 2, 1) << 3;
    uint16_t imm5 = extractBits(pcrel_val, 5, 5) << 2;
    insn |= imm8 | imm4_3 | imm7_6 | imm2_1 | imm5;

    write16le(where, insn);
  } break;

  case R_TYPE(RVC_JUMP): {
    uint16_t insn = ((*where) & 0xFFFF) & 0xE003;
    uint16_t imm11 = extractBits(pcrel_val, 11, 11) << 12;
    uint16_t imm4 = extractBits(pcrel_val, 4, 4) << 11;
    uint16_t imm9_8 = extractBits(pcrel_val, 9, 8) << 9;
    uint16_t imm10 = extractBits(pcrel_val, 10, 10) << 8;
    uint16_t imm6 = extractBits(pcrel_val, 6, 6) << 7;
    uint16_t imm7 = extractBits(pcrel_val, 7, 7) << 6;
    uint16_t imm3_1 = extractBits(pcrel_val, 3, 1) << 3;
    uint16_t imm5 = extractBits(pcrel_val, 5, 5) << 2;
    insn |= imm11 | imm4 | imm9_8 | imm10 | imm6 | imm7 | imm3_1 | imm5;

    write16le(where, insn);
  } break;

  case R_TYPE(RVC_LUI): {
    int64_t imm = SignExtend64(target + 0x800, bits) >> 12;
    if (imm == 0) { // `c.lui rd, 0` is illegal, convert to `c.li rd, 0`
      write16le(where, (read16le(where) & 0x0F83) | 0x4000);
    } else {
      uint16_t imm17 = extractBits(target + 0x800, 17, 17) << 12;
      uint16_t imm16_12 = extractBits(target + 0x800, 16, 12) << 2;
      write16le(where, (read16le(where) & 0xEF83) | imm17 | imm16_12);
    }
  } break;

  case R_TYPE(JAL): {
    uint32_t insn = read32le(where) & 0xFFF;
    uint32_t imm20 = extractBits(pcrel_val, 20, 20) << 31;
    uint32_t imm10_1 = extractBits(pcrel_val, 10, 1) << 21;
    uint32_t imm11 = extractBits(pcrel_val, 11, 11) << 20;
    uint32_t imm19_12 = extractBits(pcrel_val, 19, 12) << 12;
    insn |= imm20 | imm10_1 | imm11 | imm19_12;

    write32le(where, insn);
  } break;

  case R_TYPE(BRANCH): {

    uint32_t insn = read32le(where) & 0x1FFF07F;
    uint32_t imm12 = extractBits(pcrel_val, 12, 12) << 31;
    uint32_t imm10_5 = extractBits(pcrel_val, 10, 5) << 25;
    uint32_t imm4_1 = extractBits(pcrel_val, 4, 1) << 8;
    uint32_t imm11 = extractBits(pcrel_val, 11, 11) << 7;
    insn |= imm12 | imm10_5 | imm4_1 | imm11;

    write32le(where, insn);
  } break;

  case R_TYPE(64):
    write64le(where, target);
    break;
  case R_TYPE(32):
    write32le(where, target);
    break;

  case R_TYPE(SET6):
    write8le(where, (read8le(where) & 0xc0) | (target & 0x3f));
    break;
  case R_TYPE(SET8):
    write8le(where, target);
    break;
  case R_TYPE(SET16):
    write16le(where, target);
    break;
  case R_TYPE(SET32):
    write32le(where, target);
    break;

  case R_TYPE(ADD8):
    write8le(where, read8le(where) + target);
    break;
  case R_TYPE(ADD16):
    write16le(where, read16le(where) + target);
    break;
  case R_TYPE(ADD32):
    write32le(where, read32le(where) + target);
    break;
  case R_TYPE(ADD64):
    write64le(where, read64le(where) + target);
    break;

  case R_TYPE(SUB6):
    write8le(where, (read8le(where) & 0xc0) |
                        (((read8le(where) & 0x3f) - target) & 0x3f));
    break;
  case R_TYPE(SUB8):
    write8le(where, read8le(where) - target);
    break;
  case R_TYPE(SUB16):
    write16le(where, read16le(where) - target);
    break;
  case R_TYPE(SUB32):
    write32le(where, read32le(where) - target);
    break;
  case R_TYPE(SUB64):
    write64le(where, read64le(where) - target);
    break;

  case R_TYPE(32_PCREL): {
    write32le(where, pcrel_val);

    if (rtems_rtl_trace(RTEMS_RTL_TRACE_RELOC)) {
      printf("rtl: R_RISCV_32_PCREL %p @ %p in %s\n", (void*)*(where), where,
             rtems_rtl_obj_oname(obj));
    }

  } break;

  case R_TYPE(PCREL_HI20): {
    int64_t hi = SignExtend64(pcrel_val + 0x800, bits); // pcrel_val + 0x800;
    write32le(where, (read32le(where) & 0xFFF) | (hi & 0xFFFFF000));

  } break;

  case R_TYPE(GOT_HI20):
  case R_TYPE(HI20): {

    uint64_t hi = target + 0x800;
    write32le(where, (read32le(where) & 0xFFF) | (hi & 0xFFFFF000));
  } break;

  case R_TYPE(PCREL_LO12_I): {
    uint64_t hi = (pcrel_val + 0x800) >> 12;
    uint64_t lo = pcrel_val - (hi << 12);
    write32le(where, (read32le(where) & 0xFFFFF) | ((lo & 0xFFF) << 20));
  } break;

  case R_TYPE(LO12_I): {

    uint64_t hi = (target + 0x800) >> 12;
    uint64_t lo = target - (hi << 12);
    write32le(where, (read32le(where) & 0xFFFFF) | ((lo & 0xFFF) << 20));

  } break;

  case R_TYPE(PCREL_LO12_S): {
    uint64_t hi = (pcrel_val + 0x800) >> 12;
    uint64_t lo = pcrel_val - (hi << 12);
    uint32_t imm11_5 = extractBits(lo, 11, 5) << 25;
    uint32_t imm4_0 = extractBits(lo, 4, 0) << 7;
    write32le(where, (read32le(where) & 0x1FFF07F) | imm11_5 | imm4_0);

  } break;

  case R_TYPE(LO12_S): {
    uint64_t hi = (target + 0x800) >> 12;
    uint64_t lo = target - (hi << 12);
    uint32_t imm11_5 = extractBits(lo, 11, 5) << 25;
    uint32_t imm4_0 = extractBits(lo, 4, 0) << 7;
    write32le(where, (read32le(where) & 0x1FFF07F) | imm11_5 | imm4_0);
  } break;

  case R_TYPE(SET_ULEB128):
  case R_TYPE(SUB_ULEB128): {
    /*
     * These appear in pairs at the same offset, for example for label
     * differences in .gcc_except_table:  SET_ULEB128 sets the field to S + A
     * and SUB_ULEB128 subtracts S + A from it.  The encoded field length is
     * fixed by the assembler and must be preserved.
     */
    size_t max_len;
    uint64_t val;
    size_t len;

    max_len = sect->size - rela->r_offset;
    if (max_len > 10) {
      max_len = 10;
    }

    len = read_uleb128(where, max_len, &val);
    if (len == 0) {
      rtems_rtl_set_error(EINVAL, "%s: unterminated ULEB128 field",
                          sect->name);
      return rtems_rtl_elf_rel_failure;
    }

    if (ELF_R_TYPE(rela->r_info) == R_TYPE(SET_ULEB128)) {
      val = target;
    } else {
      val -= target;
    }

    /*
     * SET_ULEB128 and SUB_ULEB128 come in pairs at the same offset.  The
     * intermediate value written by SET_ULEB128 may exceed the encoded field
     * width; the arithmetic is carried out modulo the field capacity and
     * only the final difference has to fit, which the compiler guarantees.
     */
    if (len < 10) {
      val &= (UINT64_C(1) << (7 * len)) - 1;
    }
    (void)write_uleb128(where, len, val);
  } break;

  case R_TYPE(ALIGN):
  case R_TYPE(RELAX):
    /*
     * Linker relaxation markers.  The runtime loader performs no linker
     * relaxation, so the instructions remain in their expanded form and the
     * alignment padding remains in place:  nothing to do.
     */
    break;

  case R_TYPE(CALL_PLT):
  case R_TYPE(CALL): {
    int64_t hi = SignExtend64(pcrel_val + 0x800, bits);
    write32le(where, (read32le(where) & 0xFFF) | (hi & 0xFFFFF000));
    int64_t hi20 = SignExtend64(pcrel_val + 0x800, bits);
    int64_t lo = pcrel_val - (hi20 << 12);
    write32le(((char*)where) + 4,
              (read32le(((char*)where) + 4) & 0xFFFFF) | ((lo & 0xFFF) << 20));
  } break;

  default:
    rtems_rtl_set_error(EINVAL,
                        "%s: Unsupported relocation type %u "
                        "in non-PLT relocations",
                        sect->name, (uint32_t)ELF_R_TYPE(rela->r_info));
    return rtems_rtl_elf_rel_failure;
  }

  return rtems_rtl_elf_rel_no_error;
}

rtems_rtl_elf_rel_status
rtems_rtl_elf_relocate_rela(rtems_rtl_obj* obj, const Elf_Rela* rela,
                            const rtems_rtl_obj_sect* sect, const char* symname,
                            const Elf_Byte syminfo, const Elf_Word symvalue) {
  return rtems_rtl_elf_reloc_rela(obj, rela, sect, symname, syminfo, symvalue,
                                  false);
}

rtems_rtl_elf_rel_status rtems_rtl_elf_relocate_rela_tramp(
    rtems_rtl_obj* obj, const Elf_Rela* rela, const rtems_rtl_obj_sect* sect,
    const char* symname, const Elf_Byte syminfo, const Elf_Word symvalue) {
  return rtems_rtl_elf_reloc_rela(obj, rela, sect, symname, syminfo, symvalue,
                                  true);
}

rtems_rtl_elf_rel_status
rtems_rtl_elf_relocate_rel(rtems_rtl_obj* obj, const Elf_Rel* rel,
                           const rtems_rtl_obj_sect* sect, const char* symname,
                           const Elf_Byte syminfo, const Elf_Word symvalue) {
  (void)obj;
  (void)rel;
  (void)sect;
  (void)symname;
  (void)syminfo;
  (void)symvalue;

  rtems_rtl_set_error(EINVAL, "rel type record not supported");
  return rtems_rtl_elf_rel_failure;
}

bool rtems_rtl_elf_unwind_parse(const rtems_rtl_obj* obj, const char* name,
                                uint32_t flags) {
  return rtems_rtl_elf_unwind_dw2_parse(obj, name, flags);
}

bool rtems_rtl_elf_unwind_register(rtems_rtl_obj* obj) {
  return rtems_rtl_elf_unwind_dw2_register(obj);
}

bool rtems_rtl_elf_unwind_deregister(rtems_rtl_obj* obj) {
  return rtems_rtl_elf_unwind_dw2_deregister(obj);
}
