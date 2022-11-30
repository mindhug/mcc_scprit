/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <filesystem>
#include <iostream>

#include "compartment_config.h"
#include "compartment_manager.h"

namespace {

std::string DefaultClientPath(const std::string& dirname) {
  // return dirname + "compartments/client_generate_keys";
  return dirname + "compartments/client_derive_secret_key";
}

std::string DefaultServerPath(const std::string& dirname) {
  return dirname + "compartments/server";
}

std::string DefaultComputeNodeAPath(const std::string& dirname) {
  return dirname + "compartments/compute_node_a";
}

std::string DefaultComputeNodeBPath(const std::string& dirname) {
  return dirname + "compartments/compute_node_b";
}

std::string DefaultComputeNodeCPath(const std::string& dirname) {
  return dirname + "compartments/compute_node_c";
}

void Usage(const std::string& progname, const std::string& dirname) {
  std::cout << "Usage: " << progname << " [client_path [server_path]]\n";
  std::cout << "Default compartment paths (if not specified):\n";
  std::cout << "    client_path: " << DefaultClientPath(dirname) << "\n";
  std::cout << "    server_path: " << DefaultServerPath(dirname) << "\n";
  std::cout << "    compute_node_a_path: " << DefaultComputeNodeAPath(dirname) << "\n";
  std::cout << "    compute_node_b_path: " << DefaultComputeNodeBPath(dirname) << "\n";
  std::cout << "    compute_node_c_path: " << DefaultComputeNodeCPath(dirname) << "\n";
}

}

int main(int argc, char** argv) {
  std::string progname{argv[0]};

  // Get our dirname.
  std::string dirname{progname};
  size_t pos = dirname.find_last_of('/');
  // If there is no '/', then erase all the string (that's good enough for constructing relative
  // paths).
  dirname.erase(pos == std::string::npos ? 0 : pos + 1);

  std::string client_path = DefaultClientPath(dirname);
  std::string server_path = DefaultServerPath(dirname);
  std::string compute_node_a_path = DefaultComputeNodeAPath(dirname);
  std::string compute_node_b_path = DefaultComputeNodeBPath(dirname);
  std::string compute_node_c_path = DefaultComputeNodeCPath(dirname);

  switch (argc) {
    case 3:
      if (!std::filesystem::exists(argv[2])) {
        std::cerr << "Error: " << argv[2] << " does not exist\n";
        return 1;
      }
      server_path = argv[2];

      [[fallthrough]];
    case 2: {
      std::string arg = argv[1];
      if (arg == "-h" || arg == "--help") {
        Usage(progname, dirname);
        return 0;
      }

      if (!std::filesystem::exists(argv[1])) {
        std::cerr << "Error: " << argv[1] << " does not exist\n";
        return 1;
      }
      client_path = argv[1];

      break;
    }
    case 1:
      break;
    default:
      Usage(progname, dirname);
      return 1;
  }

  CompartmentManagerInit();
  CompartmentAdd(kClientCompartmentId, client_path, {}, kCompartmentMemoryRangeLength);
  CompartmentAdd(kServerCompartmentId, server_path, {}, kCompartmentMemoryRangeLength);
  CompartmentAdd(kComputeNodeACompartmentId, compute_node_a_path, {}, kCompartmentMemoryRangeLength);
  CompartmentAdd(kComputeNodeBCompartmentId, compute_node_b_path, {}, kCompartmentMemoryRangeLength);
  CompartmentAdd(kComputeNodeCCompartmentId, compute_node_c_path, {}, kCompartmentMemoryRangeLength);

  // Start the client compartment and wait until it's done.
  CompartmentCall(kClientCompartmentId);

  std::cout << "compartment demo completed\n";

  return 0;
}
