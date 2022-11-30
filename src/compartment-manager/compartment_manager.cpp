/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compartment_manager.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/random.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

#include <archcap.h>

#include "compartment_manager_asm.h"
#include "compartment_config.h"
#include "utils/elf_util.h"

// This is accessed from assembly.
extern "C" {
  Compartment cm_compartments[MAX_COMPARTMENTS];
}

namespace {

// Helper class to build capabilities, starting from a root capability (passed to the
// constructor). The bounds and permissions can then be restricted, and the address set.
class Capability {
 public:
  Capability(void* __capability c)
    : cap_(c) {}

  Capability(uintcap_t c)
    : cap_(reinterpret_cast<void* __capability>(c)) {}

  template <typename T>
  Capability& SetAddress(T* addr) {
    cap_ = archcap_c_address_set(cap_, addr);
    return *this;
  }

  // This sets the base and length of the capability, but also sets the address to the base,
  // because the original address may be out of the new bounds.
  Capability& SetBounds(void* base, size_t length) {
    cap_ = archcap_c_address_set(cap_, base);
    cap_ = archcap_c_bounds_set(cap_, length);
    return *this;
  }

  Capability& SetBounds(ptraddr_t base, size_t length) {
    return SetBounds(reinterpret_cast<void*>(base), length);
  }

  Capability& SetPerms(archcap_perms_t perms) {
    cap_ = archcap_c_perms_set(cap_, perms);
    return *this;
  }

  operator void* __capability() const {
    return cap_;
  }

 private:
  void* __capability cap_;
};

// The compartment manager always uses DDC for memory accesses, so Load permissions are unnecessary.
constexpr archcap_perms_t kCompartmentManagerEntryPointPerms =
    ARCHCAP_PERM_EXECUTE |
    ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_SYSTEM | ARCHCAP_PERM_MORELLO_EXECUTIVE;

// Base address of the lowest mapping, as of when the compartment manager is initialised (before any
// compartment is mapped).
ptraddr_t cm_lowest_address;

// Assumption used during the stack size calculation.
static_assert(sizeof(Elf64_auxv_t) == 16, "");

// Returns a pointer to the full name=value string for the environment variable
// named name, or nullptr if it doesn't exist.
const char* GetFullEnvString(const char* name) {
  size_t name_len = strlen(name);
  for (char** env = environ; env != nullptr; ++env) {
    const char* str = *env;
    if (strncmp(str, name, name_len) == 0 && str[name_len] == '=')
      return str;
  }
  return nullptr;
}

// Returns true if the range is considered available to be reserved for a compartment.
// These checks are not bullet-proof, rather they are considered good enough for this use-case.
// In particular, there are several assumptions being made:
// * The compartment manager has not mapped anything below cm_lowest_address since it was
//   initialised. This is a strong assumption; in practice it is a reasonable one in most cases as
//   mmap() will not allocate mappings below the program segments unless it has been explicitly
//   asked to do so.
// * Everything above cm_lowest_address is reserved to the compartment manager. This is very
//   defensive in the general case (wasting a lot of address space), but it makes things a lot
//   easier.
//
// Another note: while we could check that no mapping is present in the range using mincore() or by
// parsing /proc/self/maps, this is not necessary for this demo and is still not bullet-proof in the
// general case, as there may be other threads running and the check cannot be atomic with respect
// to other mmap() operations.
bool IsRangeFree(Range range) {
  if (range.top > cm_lowest_address) {
    std::cerr << "Range " << range << " lies partially or completely above the ambient limit\n";
    return false;
  }

  for (const Compartment& comp : cm_compartments) {
    // Ignore this compartment if it hasn't been initialised.
    if (!archcap_c_tag_get(comp.entry_point))
      continue;

    // Infer the compartment's range from its DDC.
    Range comp_range{archcap_c_base_get(comp.ddc), archcap_c_limit_get(comp.ddc)};

    if (comp_range.Intersects(range)) {
      std::cerr << "Range " << range << " clashes with already allocated compartment's range "
                << comp_range << "\n";
      return false;
    }
  }
  return true;
}

bool SetupMappings(const StaticElfExecutable& elf, size_t memory_range_length, size_t stack_size,
                   void** stack_top, Range* mmap_range) {
  long page_size = sysconf(_SC_PAGESIZE);

  // Check that the reserved range does not clash with another compartment's range.
  if (!IsRangeFree({elf.total_range().base, elf.total_range().base + memory_range_length}))
    return false;

  // Check that the reserved range is big enough for mapping the ELF segments and the stack
  // (including 2 guard pages).
  size_t required_range_length = elf.total_range().Size() + kCompartmentStackSize + 2 * page_size;
  if (required_range_length > memory_range_length) {
    std::cerr << "Insufficient memory range (required " << required_range_length
              << " B, total " << memory_range_length << " B)\n";
    return false;
  }

  // Map the whole range with PROT_NONE to reserve it for this compartment.
  if (mmap(reinterpret_cast<void*>(elf.total_range().base), memory_range_length, PROT_NONE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
    perror("mmap() failed");
    return false;
  }

  // Map the ELF segments.
  if (!elf.Map())
    return false;

  // Allocate the stack at the top of the range, leaving one guard page before and after (everything
  // is already mapped as PROT_NONE so no need to mprotect() them explicitly).
  ptraddr_t stack_base = elf.total_range().base + memory_range_length - stack_size - page_size;
  if (mmap(reinterpret_cast<void*>(stack_base), stack_size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_FIXED, -1, 0) == MAP_FAILED) {
    perror("mmap() failed");
    return false;
  }

  *stack_top = reinterpret_cast<void*>(stack_base + stack_size);

  // The remaining range is what is available for the compartment's mmap().
  mmap_range->base = elf.total_range().top;
  mmap_range->top = stack_base - page_size;

  return true;
}

// Sets up a very basic compartment stack, with only argv[0] (compartment name), PATH in envp and
// the auxiliary values required by libc.
bool SetupCompartmentStack(void** stack_top, size_t stack_size, const std::vector<std::string>& args,
                           const StaticElfExecutable& exec) {
  char* sp = reinterpret_cast<char*>(*stack_top);
  // Make sure we leave some stack space for the compartment code.
  const char* lower_limit = sp - stack_size / 2;
  bool overflow = false;

  auto push = [&](const void* src, size_t size) {
    if (sp - size < lower_limit) {
      overflow = true;
      return;
    }

    sp -= size;
    memcpy(sp, src, size);
  };

  // First copy the strings that are going to be referred to in argv, envp and auxv.
  std::vector<const char*> argv_str;
  std::vector<const char*> envp_str;
  argv_str.reserve(args.size());

  for (const std::string& arg: args) {
    push(arg.c_str(), arg.size() + 1);
    argv_str.push_back(sp);
  }

  for (const char* env_name : kCompartmentPropagatedEnv) {
    const char* env = GetFullEnvString(env_name);
    if (env != nullptr) {
      push(env, strlen(env) + 1);
      envp_str.push_back(sp);
    }
  }

  // Generate 16 random bytes for AT_RANDOM using getrandom(), like the kernel's ELF loader.
  char buf[16];
  if (getrandom(buf, sizeof(buf), 0) != sizeof(buf)) {
    perror("getrandom() failed");
    exit(1);
  }
  push(buf, sizeof(buf));
  const char* at_random = sp;

  // We need to align the final SP value on a 16-byte boundary to comply with the AAPCS64.
  // Since the argc/argv/envp/auxv layout is fixed and cannot be padded (i.e. its lowest address,
  // which is also the address of argc, must be the final SP value), the only way to do that is to
  // figure out the total size in advance. However, since the auxv struct is 16 bytes long, we don't
  // need to take auxiliary values into account for alignment purposes.
  // One pointer per envp/argv string + nullptr for envp + nullptr for argv + argc itself.
  size_t args_env_size = (envp_str.size() + 1 + argv_str.size() + 1 + 1) * sizeof(uint64_t);
  // Adjust sp so that the final SP value is 16-byte aligned.
  sp -= archcap_address_get_bits(sp - args_env_size, 0xf);

  // Setup the auxiliary vector.
  auto set_auxval = [&](uint64_t type, uint64_t val) {
    Elf64_auxv_t auxval{type, {val}};
    push(&auxval, sizeof(auxval));
  };

  set_auxval(AT_NULL, 0);
  set_auxval(AT_RANDOM, reinterpret_cast<uint64_t>(at_random));
  set_auxval(AT_SECURE, 0);
  set_auxval(AT_PHNUM, exec.GetAuxval(AT_PHNUM));
  set_auxval(AT_PHENT, exec.GetAuxval(AT_PHENT));
  set_auxval(AT_PHDR, exec.GetAuxval(AT_PHDR));
  set_auxval(AT_PAGESZ, getauxval(AT_PAGESZ));

  // Setup envp.
  const void* null = nullptr;

  push(&null, sizeof(null));
  for (const char* env : envp_str) {
    push(&env, sizeof(env));
  }

  // Setup argv and argc.
  push(&null, sizeof(null));
  // The order of arguments is significant, unlike environment variables. Since we are pushing
  // the arguments in reverse order (decreasing addresses), we need to iterate argv_str in reverse.
  std::for_each(argv_str.rbegin(), argv_str.rend(), [&](const char* arg) {
    push(&arg, sizeof(arg));
  });
  uint64_t argc = args.size();
  push(&argc, sizeof(argc));

  *stack_top = sp;
  return !overflow;
}

void CheckSpWithinStackBounds(ptraddr_t sp, ptraddr_t stack_top, size_t stack_size) {
  Range stack_range{stack_top - stack_size, stack_top};

  if (!stack_range.Contains(sp)) {
    std::cerr << std::hex << "Invalid SP returned by compartment initialization (SP = " << sp
              << ", stack = " << stack_range << ")\n";
    exit(1);
  }
}

template <typename T>
T* GetElfDataSymbol(const StaticElfExecutable& elf, const char* name) {
  void* sym = elf.FindSymbol(name, sizeof(T), PROT_READ | PROT_WRITE);
  if (sym == nullptr) {
    std::cerr << "Missing or invalid data symbol: \"" << name << "\"\n";
    exit(1);
  }

  return reinterpret_cast<T*>(sym);
}

void* GetElfFunctionSymbol(const StaticElfExecutable& elf, const char* name) {
  void* sym = elf.FindSymbol(name, 0, PROT_EXEC);
  if (sym == nullptr) {
    std::cerr << "Missing or invalid function symbol: \"" << name << "\"\n";
    exit(1);
  }

  return reinterpret_cast<void*>(sym);
}

} // namespace


void CompartmentManagerInit() {
  // Read /proc/self/maps to find the lowest mapped address.
  std::ifstream maps{"/proc/self/maps"};

  // The mapping at the lowest address is the first line in the file, and the line starts with
  // the <start>-<end> range, so we just need to read the first integer in the file to get the
  // lowest mapped address.
  maps >> std::hex >> cm_lowest_address;
  if (maps.bad()) {
    std::cerr << "Failed to read /proc/self/maps\n";
    exit(1);
  }
}

void CompartmentAdd(CompartmentId id, const std::string& path, const std::vector<std::string>& args,
                    size_t memory_range_length) {
  // Note: a proper implementation would need to check that the compartment ID isn't already
  // allocated, or even better allocate it itself and return it to the caller.
  assert(id < MAX_COMPARTMENTS);

  // Step 1: process the compartment's ELF file.
  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    perror("open() failed");
    exit(1);
  }

  StaticElfExecutable elf{fd};
  if (!elf.Read())
    exit(1);

  // Find the symbols we need.
  void* entry_point_sym = GetElfFunctionSymbol(elf, ___STRING(COMPARTMENT_ENTRY_SYMBOL));

  void* __capability* cm_call_cap_sym = GetElfDataSymbol<void* __capability>(elf,
      ___STRING(COMPARTMENT_MANAGER_CALL_CAPABILITY_SYMBOL));
  void* __capability* cm_return_cap_sym = GetElfDataSymbol<void* __capability>(elf,
      ___STRING(COMPARTMENT_MANAGER_RETURN_CAPABILITY_SYMBOL));

  ptraddr_t* mmap_range_base_sym = GetElfDataSymbol<ptraddr_t>(elf,
      ___STRING(COMPARTMENT_MMAP_RANGE_BASE_SYMBOL));
  ptraddr_t* mmap_range_top_sym = GetElfDataSymbol<ptraddr_t>(elf,
      ___STRING(COMPARTMENT_MMAP_RANGE_TOP_SYMBOL));

  // Step 2: setup the compartment's memory mappings.
  void* stack_top;
  Range mmap_range;
  if (!SetupMappings(elf, memory_range_length, kCompartmentStackSize, &stack_top, &mmap_range))
    exit(1);

  std::vector<std::string> main_args = {path};
  main_args.reserve(args.size() + 1);
  main_args.insert(main_args.end(), args.begin(), args.end());
  if (!SetupCompartmentStack(&stack_top, kCompartmentStackSize, main_args, elf)) {
    std::cerr << "Insufficient stack space\n";
    exit(1);
  }

  // Set the compartment's mmap range.
  *mmap_range_base_sym = mmap_range.base;
  *mmap_range_top_sym = mmap_range.top;

  // Step 3: compute compartment capabilities.
  uintcap_t cm_ddc = archcap_c_ddc_get();

  // DDC encompasses the whole memory range allocated to this compartment (that is
  // elf.total_range(), where the ELF code and data are mapped, plus the remainder of
  // memory_range_length where the stack and mmap()'d pages live).
  void* __capability ddc = Capability(cm_ddc)
      .SetBounds(elf.total_range().base, memory_range_length)
      .SetPerms(kCompartmentDataPerms);

  // Compartment entry point. PCC only encompasses the executable range.
  void* __capability c_entry_point = Capability(cm_ddc)
      .SetBounds(elf.executable_range().base, elf.executable_range().Size())
      .SetAddress(entry_point_sym)
      .SetPerms(kCompartmentExecPerms);

  // Init entry point. Same permissions and bounds as the compartment entry point.
  void* __capability init_entry_point = Capability(c_entry_point)
      .SetAddress(elf.entry_point());

  // We only use hybrid code, so we only need to give the compartment a valid SP, not a valid
  // CSP. A null capability with the pointer set to SP is what we need here.
  void* __capability csp = nullptr;
  csp = archcap_c_address_set(csp, stack_top);

  // Step 4: initialize the compartment.

  // Set the return entry point to allow the compartment to return once it's initialized.
  // Don't set the call entry point yet, we don't want to allow compartment calls while the
  // compartment is initializing.
  // TODO: use a type 1 sealed capability to prevent the compartment from jumping to an arbitrary
  // location in the compartment manager.
  *cm_return_cap_sym = Capability(cm_ddc)
      .SetAddress(&CompartmentSwitchReturn)
      .SetPerms(kCompartmentManagerEntryPointPerms);

  // Setup the compartment descriptor for CompartmentCall().
  Compartment& desc = cm_compartments[id];
  desc.csp = csp;
  desc.ddc = ddc;
  // During execve(), the kernel sets TPIDR to 0, so let's do the same.
  desc.ctpidr = nullptr;
  desc.entry_point = init_entry_point;
  // Ask CompartmentSwitch to update the ambient capabilities when the compartment returns, so that
  // the new SP and TPIDR values are saved for the next time the compartment is called.
  desc.update_on_return = true;

  // Call into the compartment to let it initialize itself.
  CompartmentCall(id);

  // Make sure the compartment's new SP value is sane.
  CheckSpWithinStackBounds(archcap_c_address_get(desc.csp), reinterpret_cast<ptraddr_t>(stack_top),
                           kCompartmentStackSize);

  // Step 5: finalize compartment configuration

  // Update the compartment's entry point: set it up to call the function defined by the compartment
  // as its entry point (COMPARTMENT_ENTRY_SYMBOL).
  desc.entry_point = c_entry_point;
  // Further calls to the compartment do not preserve its ambient capabilities when it returns.
  desc.update_on_return = false;

  // Set the call entry point to allow the compartment to call the compartment manager.
  // TODO: same as for cm_return_cap_sym
  *cm_call_cap_sym = Capability(cm_ddc)
      .SetAddress(&CompartmentSwitch)
      .SetPerms(kCompartmentManagerEntryPointPerms);
}
