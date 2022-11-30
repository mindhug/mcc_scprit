/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <iostream>

// Data types used in the client-server communication.
#define blockSize 16
#define OUTPUT_BUFLEN 16


enum class RequestType {
  kGetServerPublicKey,
  kGenerateClientKey,
};

struct Key {
  char data[64];
};

struct KeyPair {
  Key public_key;
  Key private_key;
};

struct Secret {
  uint8_t output[OUTPUT_BUFLEN];
};

struct KDF_Inputs {
  char passwd[10];
  char salt[19];
};

// Use a template to allow pasing both a pointer and a capability to the key.
template <typename Kp>
static inline void PrintKey(Kp key_ptr) {
  for (size_t i = 0; i < sizeof(key_ptr->data); ++i) {
    unsigned c = static_cast<unsigned>(key_ptr->data[i]);
    std::cout << std::hex << (c >> 4) << (c & 0xf);
  }
  std::cout << std::endl;
}
