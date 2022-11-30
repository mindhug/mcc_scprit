
#include <iostream>
#include <archcap.h>
#include "compartment_helpers.h"
#include "protocol.h"


// Known only to Node B 
const int MEMORY_COST_PARAMETER = 8; //actually needs to be as high as 16384;

bool sanityChecks(){
    if (((MEMORY_COST_PARAMETER & (MEMORY_COST_PARAMETER - 1)) != 0) || (MEMORY_COST_PARAMETER == 0)) {
		return false;
	}

    if ((MEMORY_COST_PARAMETER > SIZE_MAX / 128 / blockSize)) {
		return false;
	}

    return true;
}

void blkcpy(uint8_t * dest, uint8_t * src, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		dest[i] = src[i];
}

void blkxor(uint8_t * dest, uint8_t * src, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		dest[i] ^= src[i];
}

inline uint64_t le64dec(const void * pp)
{
	const uint8_t * p = (uint8_t const *)pp;

	return ((uint64_t)(p[0]) | ((uint64_t)(p[1]) << 8) |
	    ((uint64_t)(p[2]) << 16) | ((uint64_t)(p[3]) << 24) |
	    ((uint64_t)(p[4]) << 32) | ((uint64_t)(p[5]) << 40) |
	    ((uint64_t)(p[6]) << 48) | ((uint64_t)(p[7]) << 56));
}

/**
 * integerify(B, blockSize):
 * Return the result of parsing B_{2r-1} as a little-endian integer.
 */
uint64_t integerify(uint8_t * B, size_t r)
{
	uint8_t * X = &B[(2 * r - 1) * 64];
	return (le64dec(X));
}

/**
 * blockmix_salsa8(B, Y, r):
 * Compute B = BlockMix_{salsa20/8, r}(B).  The input B must be 128r bytes in
 * length; the temporary space Y must also be the same size.
 */
void blockmix_salsa8(uint8_t* B, uint8_t* Y, size_t blockSize)
{
	uint8_t X[64];
	size_t i;

    bool checks = sanityChecks()

    if(!checks){
        CompartmentReturn(-1);
    }

	/* 1: X <-- B_{2r - 1} */
	blkcpy(X, &B[(2 * blockSize - 1) * 64], 64);

	/* 2: for i = 0 to 2r - 1 do */
	for (i = 0; i < 2 * blockSize; i++) {
		/* 3: X <-- H(X \xor B_i) */
		blkxor(X, &B[i * 64], 64);
        
        uint8_t* __capability block_mixed_hash_cap = archcap_c_ddc_cast(&X);
        block_mixed_hash_cap = archcap_c_perms_set(block_mixed_hash_cap, ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_STORE | ARCHCAP_PERM_LOAD);
        uintcap_t ret = CompartmentCall(kComputeNodeCCompartmentId, AsUintcap(block_mixed_hash_cap));
		// salsa20_8(X);

        if (ret == 0) {
            std::cout << "[Node B] Returned Salsa Core: ";
            PrintKey(&X);
        } else {
            std::cout << "[Node B] Node C failed to return salsa core\n";
        }

		/* 4: Y_i <-- X */
		blkcpy(&Y[i * 64], X, 64);
	}

	/* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
	for (i = 0; i < blockSize; i++)
		blkcpy(&B[i * 64], &Y[(i * 2) * 64], 64);
	for (i = 0; i < blockSize; i++)
		blkcpy(&B[(i + blockSize) * 64], &Y[(i * 2 + 1) * 64], 64);
}

COMPARTMENT_ENTRY_POINT(uint8_t* __capability input_chunk) {
    uint8_t* V;
	uint8_t* XY;

    if ((XY = malloc(256 * blockSize)) == NULL)
		CompartmentReturn(-1);
	if ((V = malloc(128 * blockSize * MEMORY_COST_PARAMETER)) == NULL)
		CompartmentReturn(-1);
    
    uint8_t* X = XY;
	uint8_t* Y = &XY[128 * blockSize];
	uint64_t i;
	uint64_t j;

    // Needs investigation does store provide the load perm by default?
    if (archcap_c_tag_get(input_chunk) && (archcap_c_limit_get(input_chunk) - archcap_c_address_get(input_chunk)) >= sizeof(X) &&
          (archcap_c_perms_get(input_chunk) & ARCHCAP_PERM_LOAD) != 0 && (archcap_c_perms_get(input_chunk) & ARCHCAP_PERM_STORE) != 0) {
              
        //check if the capability has any issues doing the blkcpy, else need to do memcpy
        blkcpy(X, input_chunk, 128 * blockSize);

        for (i = 0; i < MEMORY_COST_PARAMETER; i++) {
		    /* 3: V_i <-- X */
		    blkcpy(&V[i * (128 * blockSize)], X, 128 * blockSize);
		    /* 4: X <-- H(X) */
		    blockmix_salsa8(X, Y, blockSize);
	    }

        /* 6: for i = 0 to N - 1 do */
	    for (i = 0; i < MEMORY_COST_PARAMETER; i++) {
		    /* 7: j <-- Integerify(X) mod N */
		    j = integerify(X, blockSize) & (MEMORY_COST_PARAMETER - 1);
            std::cout << j;

		    /* 8: X <-- H(X \xor V_j) */
		    blkxor(X, &V[j * (128 * blockSize)], 128 * blockSize);
		    blockmix_salsa8(X, Y, blockSize);
	    }

        //investigate the ptr X, does it copy or needs the address & to copy contents
        memcpy_c(input_chunk, archcap_c_ddc_cast(X), 128 * blockSize);
        // blkcpy(input_chunk, X, 128 * blockSize);
        CompartmentReturn(0);

    } else {
        CompartmentReturn(-1);
    }

}

int main(int, char** argv) {

  std::cout << "[Node B] MemCost Factor Compartment @" << argv[0] << " initialized" << std::endl;

  // Return to the compartment manager, letting it know that we have completed our initialization.
  CompartmentReturn();
}

