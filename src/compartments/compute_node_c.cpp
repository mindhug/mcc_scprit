
#include <sys/random.h>
#include <iostream>
#include <archcap.h>
#include "compartment_helpers.h"
#include "protocol.h"


void blkcpy(uint8_t * dest, uint8_t * src, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		dest[i] = src[i];
}

uint32_t le32dec(const void * pp)
{
	const uint8_t * p = (uint8_t const *)pp;

	return ((uint32_t)(p[0]) | ((uint32_t)(p[1]) << 8) |
	    ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24));
}

void le32enc(void * pp, uint32_t x)
{
	uint8_t * p = (uint8_t *)pp;

	p[0] = x & 0xff;
	p[1] = (x >> 8) & 0xff;
	p[2] = (x >> 16) & 0xff;
	p[3] = (x >> 24) & 0xff;
}

void salsa20_8(uint8_t B[64])
{
	uint32_t B32[16];
	uint32_t x[16];
	size_t i;

	/* Convert little-endian values in. */
	for (i = 0; i < 16; i++)
		B32[i] = le32dec(&B[i * 4]);

	/* Compute x = doubleround^4(B32). */
	for (i = 0; i < 16; i++)
		x[i] = B32[i];
	for (i = 0; i < 8; i += 2) {
        #define R(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
		/* Operate on columns. */
		x[ 4] ^= R(x[ 0]+x[12], 7);  x[ 8] ^= R(x[ 4]+x[ 0], 9);
		x[12] ^= R(x[ 8]+x[ 4],13);  x[ 0] ^= R(x[12]+x[ 8],18);

		x[ 9] ^= R(x[ 5]+x[ 1], 7);  x[13] ^= R(x[ 9]+x[ 5], 9);
		x[ 1] ^= R(x[13]+x[ 9],13);  x[ 5] ^= R(x[ 1]+x[13],18);

		x[14] ^= R(x[10]+x[ 6], 7);  x[ 2] ^= R(x[14]+x[10], 9);
		x[ 6] ^= R(x[ 2]+x[14],13);  x[10] ^= R(x[ 6]+x[ 2],18);

		x[ 3] ^= R(x[15]+x[11], 7);  x[ 7] ^= R(x[ 3]+x[15], 9);
		x[11] ^= R(x[ 7]+x[ 3],13);  x[15] ^= R(x[11]+x[ 7],18);

		/* Operate on rows. */
		x[ 1] ^= R(x[ 0]+x[ 3], 7);  x[ 2] ^= R(x[ 1]+x[ 0], 9);
		x[ 3] ^= R(x[ 2]+x[ 1],13);  x[ 0] ^= R(x[ 3]+x[ 2],18);

		x[ 6] ^= R(x[ 5]+x[ 4], 7);  x[ 7] ^= R(x[ 6]+x[ 5], 9);
		x[ 4] ^= R(x[ 7]+x[ 6],13);  x[ 5] ^= R(x[ 4]+x[ 7],18);

		x[11] ^= R(x[10]+x[ 9], 7);  x[ 8] ^= R(x[11]+x[10], 9);
		x[ 9] ^= R(x[ 8]+x[11],13);  x[10] ^= R(x[ 9]+x[ 8],18);

		x[12] ^= R(x[15]+x[14], 7);  x[13] ^= R(x[12]+x[15], 9);
		x[14] ^= R(x[13]+x[12],13);  x[15] ^= R(x[14]+x[13],18);
        #undef R
	}

	/* Compute B32 = B32 + x. */
	for (i = 0; i < 16; i++)
		B32[i] += x[i];

	/* Convert little-endian values out. */
	for (i = 0; i < 16; i++)
		le32enc(&B[4 * i], B32[i]);
}


COMPARTMENT_ENTRY_POINT(uint8_t* __capability core_hash_output) {
    uint8_t octetX[64];

    if (archcap_c_tag_get(core_hash_output) && (archcap_c_limit_get(core_hash_output) - archcap_c_address_get(core_hash_output)) >= sizeof(octetX) && 
    (archcap_c_perms_get(core_hash_output) & ARCHCAP_PERM_STORE) != 0 && (archcap_c_perms_get(core_hash_output) & ARCHCAP_PERM_LOAD) != 0) {
        
        blkcpy(octetX, &core_hash_output, 64);
        salsa20_8(octetX)
        std::cout << "Generated a basic PseudoRandom salsa stream output"

        // Use memcpy_c() to write via the client capability. We use DDC to construct a source
        // capability.
        memcpy_c(core_hash_output, archcap_c_ddc_cast(&octetX), sizeof(octetX));
        CompartmentReturn(0);
    } 
    else {
        CompartmentReturn(-1);
    }

}

int main(int, char** argv) {

  std::cout << "[Salsa Core - Node C] Compartment @" << argv[0] << " initialized" << std::endl;

  // Return to the compartment manager, letting it know that we have completed our initialization.
  CompartmentReturn();
}