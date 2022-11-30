/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "elf_util.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#include <limits>

#include <archcap.h>

#include "utils/align.h"

namespace {

template <typename T>
bool ReadHeader(int fd, off_t offset, T* header) {
  ssize_t res = pread(fd, header, sizeof(*header), offset);

  if (res != sizeof(*header)) {
    if (res == -1) {
      perror("pread() failed");
    } else {
      std::cerr << "Got unexpected size while reading header (got "
                << res << ", expected " << sizeof(*header) << ")\n";
    }
    return false;
  }

  return true;
}

// Map a section read-only for inspection purposes (no fixed address).
void* MapSection(int fd, const Elf64_Shdr& shdr, size_t page_size) {
  // We don't care about the mapped address, but we still need to start at a page-aligned offset.
  size_t page_offset = (shdr.sh_offset & (page_size - 1));
  void* res = mmap(nullptr, shdr.sh_size + page_offset, PROT_READ, MAP_PRIVATE,
                   fd, shdr.sh_offset - page_offset);

  if (res == MAP_FAILED) {
    perror("mmap() failed");
  } else {
    res = reinterpret_cast<char*>(res) + page_offset;
  }
  return res;
}

void UnmapUnaligned(void* addr, size_t size, size_t page_size) {
  size_t page_offset = archcap_address_get_bits(addr, page_size - 1);
  if (munmap(reinterpret_cast<char*>(addr) - page_offset, size + page_offset) == -1) {
    perror("munmap() failed");
  }
}

} // namespace

const Range Range::kEmpty = {std::numeric_limits<ptraddr_t>::max(), 0};

std::ostream& operator<<(std::ostream& o, const Range& r) {
  auto f{o.flags()};
  o << std::hex << std::showbase << "[" << r.base << ", " << r.top << "]";
  o.flags(f);
  return o;
}

bool StaticElfExecutable::ReadProgramHeaders() {
  total_range_ = Range::kEmpty;
  executable_range_ = total_range_;

  assert(ehdr_.e_phentsize == sizeof(Elf64_Phdr));

  for (Elf64_Half i = 0; i < ehdr_.e_phnum; ++i) {
    Elf64_Phdr phdr;
    if (!ReadHeader(fd_, ehdr_.e_phoff + i * sizeof(phdr), &phdr))
      return false;

    // We only care about segments that need to be loaded in memory.
    if (phdr.p_type != PT_LOAD) {
      if (phdr.p_type == PT_DYNAMIC || phdr.p_type == PT_INTERP) {
        std::cerr << "Unexpected dynamic segment, only static executables are supported\n";
        return false;
      }
      continue;
    }

    if (phdr.p_filesz > phdr.p_memsz) {
        std::cerr << "Invalid segment " << i << ": p_filesz > p_memsz\n";
        return false;
    } else if (phdr.p_filesz < phdr.p_memsz && !(phdr.p_flags & PF_W)) {
        std::cerr << "Invalid segment " << i << ": requires zero-fill, but is not writeable\n";
        return false;
    }

    LoadSegmentInfo info;
    info.mem_range.base = align_down(phdr.p_vaddr, page_size_);
    info.mem_range.top = phdr.p_vaddr + phdr.p_memsz;
    info.file_offset = align_down(phdr.p_offset, page_size_);
    info.file_mapped_size = phdr.p_filesz + (phdr.p_offset & (page_size_ - 1));
    info.prot = (phdr.p_flags & PF_R ? PROT_READ : 0) |
                (phdr.p_flags & PF_W ? PROT_WRITE : 0) |
                (phdr.p_flags & PF_X ? PROT_EXEC : 0);

    // The ELF spec mandates that PT_LOAD segments are sorted in ascending order
    // of p_vaddr, so the segment cannot fit in a "hole" in the total range
    // built so far.
    if (total_range_.Intersects(info.mem_range)) {
      std::cerr << "Invalid segment " << i << ": overlaps another segment\n";
      return false;
    }
    total_range_.Enlarge(info.mem_range);
    if (phdr.p_flags & PF_X)
      executable_range_.Enlarge(info.mem_range);

    load_segments_.push_back(info);
  }

  return true;
}

bool StaticElfExecutable::CheckEntryPoint() const {
  // Require at least one instruction inside the segment.
  Range entry_point_range{ehdr_.e_entry, ehdr_.e_entry + 4};

  // Check that the entry point is in an executable segment.
  for (const auto& segment : load_segments_) {
    if (!segment.mem_range.Contains(entry_point_range))
      continue;

    if (segment.prot & PROT_EXEC) {
      return true;
    } else {
      // Segments don't overlap, bail out.
      break;
    }
  }

  std::cerr << "Invalid entry point\n";
  return false;
}

bool StaticElfExecutable::LoadSymbolTable() {
  assert(ehdr_.e_shentsize == sizeof(Elf64_Shdr));

  // Find the symbol table section. We assume there is only one.
  Elf64_Shdr symtabhdr;
  Elf64_Half i;
  for (i = 0; i < ehdr_.e_shnum; ++i) {
    // Iterate in reverse order, as the linker tends to put the symbol table towards the end.
    if (!ReadHeader(fd_, ehdr_.e_shoff + (ehdr_.e_shnum - 1 - i) * sizeof(symtabhdr), &symtabhdr))
      return false;

    if (symtabhdr.sh_type == SHT_SYMTAB)
      break;
  }

  if (i == ehdr_.e_shnum) {
    std::cerr << "No symbol table section found, make sure the binary is not stripped\n";
    return false;
  }

  assert(symtabhdr.sh_entsize == sizeof(Elf64_Sym));

  // The SysV and ELF64 specs specify that sh_link is equal to the string table section number and
  // sh_info is equal to the number of local symbols for SHT_SYMTAB sections.
  if (symtabhdr.sh_link == SHN_UNDEF || symtabhdr.sh_link >= ehdr_.e_shnum ||
      symtabhdr.sh_info * sizeof(Elf64_Sym) >= symtabhdr.sh_size ||
      symtabhdr.sh_size == 0 || symtabhdr.sh_size % sizeof(Elf64_Sym) != 0) {
    std::cerr << "Invalid symbol table section\n";
    return false;
  }

  Elf64_Shdr strtabhdr;
  if (!ReadHeader(fd_, ehdr_.e_shoff + symtabhdr.sh_link * sizeof(strtabhdr), &strtabhdr))
    return false;

  if (strtabhdr.sh_type != SHT_STRTAB) {
    std::cerr << "Invalid string table section\n";
    return false;
  }

  // Map both sections.
  void* map;
  if ((map = MapSection(fd_, symtabhdr, page_size_)) == MAP_FAILED)
    return false;
  symtab_ = reinterpret_cast<Elf64_Sym*>(map);
  symtab_num_ = symtabhdr.sh_size / sizeof(Elf64_Sym);
  symtab_global_index_ = symtabhdr.sh_info;

  if ((map = MapSection(fd_, strtabhdr, page_size_)) == MAP_FAILED) {
    UnmapUnaligned(symtab_, symtabhdr.sh_size, page_size_);
    return false;
  }
  strtab_ = reinterpret_cast<char*>(map);
  strtab_size_ = strtabhdr.sh_size;

  return true;
}

bool StaticElfExecutable::Read() {
  if (initialized_) {
    std::cerr << "StaticElfExecutable::Read(): already initialized\n";
    return false;
  }

  // Read the ELF header.
  if (!ReadHeader(fd_, 0, &ehdr_))
    return false;

  // Some sanity checks.
  if (!(strncmp(reinterpret_cast<char*>(&ehdr_.e_ident[EI_MAG0]), "\x7f""ELF", 4) == 0 &&
        ehdr_.e_ident[EI_CLASS] == ELFCLASS64 &&
        // We can only load static executables (meaning not PIE).
        ehdr_.e_type == ET_EXEC &&
        ehdr_.e_machine == EM_AARCH64)) {
    std::cerr << "Unexpected file (must be an AArch64 static ELF executable)\n";
    return false;
  }

  // Read the program header table and build a table of mapping ranges.
  if (!ReadProgramHeaders())
    return false;

  if (executable_range_.Size() == 0) {
    std::cerr << "No executable segment found\n";
    return false;
  }
  assert(total_range_.Size() > 0);

  // Check the entry point is sensible.
  if (!CheckEntryPoint())
    return false;

  // Load the symbol and symbol string tables.
  if (!LoadSymbolTable())
    return false;

  initialized_ = true;
  return true;
}

bool StaticElfExecutable::Map() const {
  if (!initialized_) {
    std::cerr << "StaticElfExecutable::Map(): not initialized\n";
    return false;
  }

  for (const auto& segment : load_segments_) {
    if (mmap(reinterpret_cast<void*>(segment.mem_range.base), segment.file_mapped_size,
             segment.prot, MAP_PRIVATE | MAP_FIXED, fd_, segment.file_offset) == MAP_FAILED) {
      perror("mmap() failed");
      return false;
    }

    size_t zero_fill_size = segment.mem_range.Size() - segment.file_mapped_size;
    if (zero_fill_size != 0) {
      // Zero-fill the end of the last page, and map anonymous pages for the remainder of the range.
      // We checked that the segment is writeable previously, so writing to the page is safe.
      ptraddr_t zero_fill_start = segment.mem_range.base + segment.file_mapped_size;
      ptraddr_t zero_pages_start = align_up(zero_fill_start, page_size_);
      size_t memset_size = std::min(zero_pages_start - zero_fill_start, zero_fill_size);

      memset(reinterpret_cast<void*>(zero_fill_start), 0, memset_size);
      zero_fill_size -= memset_size;

      if (zero_fill_size != 0) {
        if (mmap(reinterpret_cast<void*>(zero_pages_start), zero_fill_size,
                 segment.prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED) {
          perror("mmap() failed");
          return false;
        }
      }
    }
  }

  return true;
}

void* StaticElfExecutable::FindSymbol(const char* name, size_t size, int prot) const {
  if (!initialized_) {
    std::cerr << "StaticElfExecutable::FindSymbol(): not initialized\n";
    return nullptr;
  }

  // Ignore local symbols.
  for (auto i = symtab_global_index_; i < symtab_num_; ++i) {
    const Elf64_Sym& sym = symtab_[i];

    // Only consider OBJECT or FUNC symbols, we are only interested in global variables or
    // functions.
    switch (ELF64_ST_TYPE(sym.st_info)) {
      case STT_OBJECT:
      case STT_FUNC:
        break;
      default:
        continue;
    }

    if (sym.st_name >= strtab_size_) {
      std::cerr << "Invalid symbol table entry at index " << i << "\n";
      continue;
    }

    // Match symbol name.
    if (strncmp(&strtab_[sym.st_name], name, strtab_size_ - sym.st_name) != 0)
      continue;

    // We assume there is at most one global symbol with a given name, so from there on bail
    // out if anything doesn't match.
    // Match size.
    if (size == 0 || sym.st_size == size) {
      // Check the address points into one of the segments.
      for (const auto& segment : load_segments_) {
        Range sym_range{sym.st_value, sym.st_value + sym.st_size};

        if (!segment.mem_range.Contains(sym_range))
          continue;

        // Check protection attributes.
        if ((segment.prot & prot) == prot) {
          // Found our symbol!
          return reinterpret_cast<void*>(sym.st_value);
        } else {
          // Segments don't overlap, bail out.
          break;
        }
      }
    }

    break;
  }

  return nullptr;
}

unsigned long StaticElfExecutable::GetAuxval(unsigned long type) const {
  switch (type) {
    case AT_PHDR: {
      off_t phdr_base_off = ehdr_.e_phoff;
      off_t phdr_top_off = phdr_base_off + ehdr_.e_phnum * ehdr_.e_phentsize;

      // Find the segment in which the program headers are loaded.
      for (const auto& segment : load_segments_) {
        off_t segment_top_offset = segment.file_offset + segment.file_mapped_size;

        if (segment.file_offset <= phdr_base_off && phdr_top_off <= segment_top_offset) {
          return segment.mem_range.base + phdr_base_off - segment.file_offset;
        }
      }
      // No segment found.
      return 0;
    }

    case AT_PHENT:
      return ehdr_.e_phentsize;

    case AT_PHNUM:
      return ehdr_.e_phnum;

    default:
      return 0;
  }
}

StaticElfExecutable::StaticElfExecutable(int fd)
    : fd_(fd), initialized_(false), page_size_(sysconf(_SC_PAGESIZE)) {}

StaticElfExecutable::~StaticElfExecutable() {
  if (initialized_) {
    UnmapUnaligned(symtab_, symtab_num_ * sizeof(Elf64_Sym), page_size_);
    UnmapUnaligned(strtab_, strtab_size_, page_size_);
  }
  close(fd_);
}
