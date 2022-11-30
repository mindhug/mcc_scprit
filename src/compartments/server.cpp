/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/random.h>

#include <iostream>

#include <archcap.h>

#include "compartment_helpers.h"
#include "protocol.h"

namespace {

KeyPair server_keys;

// This is no cryptographic key generation, just some random data generation!
void GenerateKeyPair(KeyPair& key_pair) {
  for (Key* key : {&key_pair.public_key, &key_pair.private_key}) {
    // Ignore (unlikely) errors to make things simpler.
    (void)getrandom(key->data, sizeof(key->data), 0);
  }
}

}

COMPARTMENT_ENTRY_POINT(RequestType request, KeyPair* __capability client_keys) {
  switch (request) {
    case RequestType::kGetServerPublicKey: {
      // Create a read-only capability to the server public key. The length of the capability is set
      // to the public key's size, and its permissions are restricted to only allow loading data.
      Key* __capability server_pk_cap = archcap_c_ddc_cast(&server_keys.public_key);
      server_pk_cap = archcap_c_perms_set(server_pk_cap, ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_LOAD);
      // Return the capability to the client compartment.
      CompartmentReturn(AsUintcap(server_pk_cap));
    }
    case RequestType::kGenerateClientKey: {
      // Generate a new key pair for the client. We need to use a temporary variable because
      // getrandom() and therefore GenerateKeyPair() only accept 64-bit pointers, not capabilities,
      // so we cannot pass client_keys to it directly.
      KeyPair tmp;
      GenerateKeyPair(tmp);
      std::cout << "[Server] Generated public key: ";
      PrintKey(&tmp.public_key);
      std::cout << "[Server] Generated private key: ";
      PrintKey(&tmp.private_key);

      // Check that the client-provided capability is appropriate for storing the generated key pair
      // to, by inspecting its tag, bounds and permissions. Note that this is not an exhaustive
      // check, and there is no reliable way to ensure that the underlying memory is accessible.
      if (archcap_c_tag_get(client_keys) &&
          (archcap_c_limit_get(client_keys) - archcap_c_address_get(client_keys)) >= sizeof(tmp) &&
          (archcap_c_perms_get(client_keys) & ARCHCAP_PERM_STORE) != 0) {
        // Use memcpy_c() to write via the client capability. We use DDC to construct a source
        // capability.
        memcpy_c(client_keys, archcap_c_ddc_cast(&tmp), sizeof(tmp));
        CompartmentReturn(0);
      } else {
        CompartmentReturn(-1);
      }
    }
    default:
      std::cout << "[Server] Unknown request\n";
      CompartmentReturn(-1);
  }
}

int main(int, char** argv) {
  GenerateKeyPair(server_keys);
  std::cout << "[Server] Public key: ";
  PrintKey(&server_keys.public_key);
  std::cout << "[Server] Private key: ";
  PrintKey(&server_keys.private_key);

  std::cout << "[Server] Compartment @" << argv[0] << " initialized" << std::endl;

  // Return to the compartment manager, letting it know that we have completed our initialization.
  CompartmentReturn();
}
