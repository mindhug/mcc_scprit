/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <iostream>

#include <archcap.h>

#include "compartment_helpers.h"
#include "compartment_interface.h"
#include "protocol.h"



#if defined(COMPARTMENT_CLIENT_GENERATE_KEYS)

COMPARTMENT_ENTRY_POINT(void) {
  // Request a new key pair for the client. We construct a write-only capability to that effect,
  // and pass it to the server.
  KeyPair client_keys;
  KeyPair* __capability client_keys_cap = archcap_c_ddc_cast(&client_keys);
  client_keys_cap = archcap_c_perms_set(client_keys_cap, ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_STORE);

  uintcap_t ret = CompartmentCall(kServerCompartmentId, AsUintcap(RequestType::kGenerateClientKey),
                                  AsUintcap(client_keys_cap));
  if (ret == 0) {
    std::cout << "[Client] Generated public key: ";
    PrintKey(&client_keys.public_key);
    std::cout << "[Client] Generated private key: ";
    PrintKey(&client_keys.private_key);
  } else {
    std::cout << "[Client] Server failed to generate keys\n";
  }

  CompartmentReturn();
}

#elif defined(COMPARTMENT_CLIENT_DERIVE_SECRET_KEY)

COMPARTMENT_ENTRY_POINT(void) {
  // Derive a client secret based on MCC based key derivation function. We construct a write-only capability to that effect,
  // and pass it to the server.
  // const char* passwd = "dsbd_cheri";
  // const char* salt = "$123fvp_morello123$";
  KDF_Inputs input;
  &input->passwd = "dsbd_cheri";
  &input->salt = "$123fvp_morello123$";
  KDF_Inputs* __capability input_cap = archcap_c_ddc_cast(&input);
  input_cap = archcap_c_perms_set(input_cap, ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_LOAD);
  Secret client_derived_secret;
  Secret* __capability client_derived_secret_cap = archcap_c_ddc_cast(&client_derived_secret);
  client_derived_secret_cap = archcap_c_perms_set(client_derived_secret_cap, ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_STORE);

  uintcap_t ret = CompartmentCall(kComputeNodeACompartmentId, AsUintcap(input_cap), AsUintcap(client_derived_secret_cap));
  if (ret == 0) {
    std::cout << "[Client] Derived Secret: ";
    PrintKey(&client_derived_secret.output);
  } else {
    std::cout << "[Client] Nodes failed to derive secret\n";
  }

  CompartmentReturn();
}

#elif defined(COMPARTMENT_CLIENT_GET_SERVER_KEY)

COMPARTMENT_ENTRY_POINT(void) {
  // Request a capability to the server's public key.
  uintcap_t ret = CompartmentCall(kServerCompartmentId, AsUintcap(RequestType::kGetServerPublicKey));
  auto server_pk = reinterpret_cast<const Key* __capability>(ret);
  std::cout << "[Client] Server public key: ";
  PrintKey(server_pk);

  CompartmentReturn();
}

#elif defined(COMPARTMENT_CLIENT_GET_SERVER_KEY_ROGUE)

COMPARTMENT_ENTRY_POINT(void) {
  // Request a capability to the server's public key as above.
  uintcap_t ret = CompartmentCall(kServerCompartmentId, AsUintcap(RequestType::kGetServerPublicKey));
  // Try to access the server's private key through type obfuscation.
  auto server_key_pair = reinterpret_cast<const KeyPair* __capability>(ret);
  std::cout << "[Client] Server private key: " << std::endl;
  // This will cause a capability fault, as we are trying to access data beyond the bounds of the
  // capability the server gave us!
  PrintKey(&server_key_pair->private_key);

  CompartmentReturn();
}

#else
#error "No client implementation chosen"
#endif

int main(int, char** argv) {
  std::cout << "[Client] Compartment @" << argv[0] << " initialized" << std::endl;

  CompartmentReturn();
}
