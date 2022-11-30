/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <elf.h>

#include <algorithm>
#include <iostream>
#include <vector>

// A range of addresses.
struct Range {
  ptraddr_t base; // Base address.
  ptraddr_t top;  // Top address, exclusive (one byte after the last byte in the range).

  // Represents an empty range (which intersects with and contains no range).
  static const Range kEmpty;

  size_t Size() const {
    return top > base ? top - base : 0;
  }

  bool Intersects(const Range& other) const {
    return base < other.top && other.base < top;
  }

  bool Contains(const Range& other) const {
    return base <= other.base && other.top <= top;
  }

  bool Contains(ptraddr_t addr) const {
    return base <= addr && addr < top;
  }

  // Set the range so that it encompasses the old range and other's range.
  void Enlarge(const Range& other) {
    base = std::min(base, other.base);
    top = std::max(top, other.top);
  }
};
std::ostream& operator<<(std::ostream&, const Range&);

// Representation of a static AArch64 ELF executable, for runtime loading.
class StaticElfExecutable {
 public:
  // Create a StaticElfExecutable object that represents the file associated with fd. The object
  // acquires ownership of fd (fd will be closed when the object is destroyed).
  // The object is not in an initialized state until Read() is called and returns true.
  StaticElfExecutable(int fd);
  ~StaticElfExecutable();

  StaticElfExecutable(const StaticElfExecutable&) = delete;

  // Read the ELF file using fd_. Returns false if anything went wrong.
  bool Read();

  // Map all the segments. Existing mappings are not checked and will be overwritten if overlapping!
  bool Map() const;

  // Find a global symbol (corresponding to a global variable or function) in the symbol table, and
  // return its address, or nullptr if there is no match. The address is guaranteed to point into
  // one of the executable's loaded segments (mapped by Map()), with at least the required
  // protection attributes.
  // - name: name of the symbol (mangled if necessary).
  // - size: required size (any if 0).
  // - prot: required protection attribute, mmap format
  //         (e.g. for an R/W variable: PROT_READ | PROT_WRITE).
  void* FindSymbol(const char* name, size_t size, int prot) const;

  const Range& total_range() const { return total_range_; }
  const Range& executable_range() const { return executable_range_; }
  void* entry_point() const { return reinterpret_cast<void*>(ehdr_.e_entry); }

  // Return the appropriate auxiliary value for this executable. Only types that are directly
  // related to the ELF file are supported:
  // - AT_PHDR (if the program headers are not included in a LOAD segment, 0 is returned)
  // - AT_PHENT
  // - AT_PHNUM
  unsigned long GetAuxval(unsigned long type) const;

 private:
  // Everything needed to load a (PT_LOAD) segment.
  struct LoadSegmentInfo {
    Range mem_range;          // Address range of the segment in memory (page-aligned base).
    off_t file_offset;        // Offset of the segment in the file (page-aligned).
    size_t file_mapped_size;  // Size of the file region that should be mapped
                              // (starting at file_offset).
    int prot;                 // Protection attributes (mmap format).
  };

  bool ReadProgramHeaders();
  bool LoadSymbolTable();
  bool CheckEntryPoint() const;

  int fd_;
  bool initialized_ = false;

  std::vector<LoadSegmentInfo> load_segments_;
  // Range encompassing all the loaded segments.
  Range total_range_;
  // Range encompassing all the executable segments.
  Range executable_range_;

  Elf64_Ehdr ehdr_;

  // Pointer to the symbol table (mmap()'ed).
  Elf64_Sym* symtab_;
  // Total number of symbol entries in symtab_.
  Elf64_Word symtab_num_;
  // Index of the first non-local symbol in symtab_.
  Elf64_Word symtab_global_index_;
  // Pointer to the symbol string table (mmap()'ed).
  char* strtab_;
  size_t strtab_size_;

  size_t page_size_;
};
