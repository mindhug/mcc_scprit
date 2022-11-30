
#include <sys/random.h>
#include <iostream>
#include <archcap.h>
#include "compartment_helpers.h"
#include "protocol.h"

#define RNDr(S, W, i, ii)			\
	RND(S[(64 - i) % 8], S[(65 - i) % 8],	\
	    S[(66 - i) % 8], S[(67 - i) % 8],	\
	    S[(68 - i) % 8], S[(69 - i) % 8],	\
	    S[(70 - i) % 8], S[(71 - i) % 8],	\
	    W[i + ii] + Krnd[i + ii])

/* Message schedule computation */
#define MSCH(W, ii, i)				\
	W[i + ii + 16] = s1(W[i + ii + 14]) + W[i + ii + 9] + s0(W[i + ii + 1]) + W[i + ii]

// Known only to Node A 
const int PARALLELIZATION_FACTOR = 2;

typedef struct {
	uint32_t state[8];
	uint64_t count;
	uint8_t buf[64];
} SHA256_CTX;

typedef struct {
	SHA256_CTX ictx;
	SHA256_CTX octx;
} HMAC_SHA256_CTX;

static const uint32_t initial_state[8] = {
	0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

static inline void be32enc(void * pp, uint32_t x)
{
	uint8_t * p = (uint8_t *)pp;

	p[3] = x & 0xff;
	p[2] = (x >> 8) & 0xff;
	p[1] = (x >> 16) & 0xff;
	p[0] = (x >> 24) & 0xff;
}

static void be32enc_vect(uint8_t * dst, const uint32_t * src, size_t len)
{
	size_t i;
	/* Sanity-check. */
	assert(len % 4 == 0);

	/* Encode vector, one word at a time. */
	for (i = 0; i < len / 4; i++)
		be32enc(dst + i * 4, src[i]);
}

static inline void be32enc(void * pp, uint32_t x)
{
	uint8_t * p = (uint8_t *)pp;

	p[3] = x & 0xff;
	p[2] = (x >> 8) & 0xff;
	p[1] = (x >> 16) & 0xff;
	p[0] = (x >> 24) & 0xff;
}

static inline uint32_t be32dec(const void * pp)
{
	const uint8_t * p = (uint8_t const *)pp;

	return ((uint32_t)(p[3]) | ((uint32_t)(p[2]) << 8) |
	    ((uint32_t)(p[1]) << 16) | ((uint32_t)(p[0]) << 24));
}

static void be32dec_vect(uint32_t * dst, const uint8_t * src, size_t len)
{
	size_t i;
	/* Sanity-check. */
	assert(len % 4 == 0);
	/* Decode vector, one word at a time. */
	for (i = 0; i < len / 4; i++)
		dst[i] = be32dec(src + i * 4);
}

static void SHA256_Transform(uint32_t state[static restrict 8], const uint8_t block[static restrict 64], uint32_t W[static restrict 64], uint32_t S[static restrict 8])
{
	int i;
	/* 1. Prepare the first part of the message schedule W. */
	be32dec_vect(W, block, 64);

	/* 2. Initialize working variables. */
	memcpy(S, state, 32);

	/* 3. Mix. */
	for (i = 0; i < 64; i += 16) {
		RNDr(S, W, 0, i);
		RNDr(S, W, 1, i);
		RNDr(S, W, 2, i);
		RNDr(S, W, 3, i);
		RNDr(S, W, 4, i);
		RNDr(S, W, 5, i);
		RNDr(S, W, 6, i);
		RNDr(S, W, 7, i);
		RNDr(S, W, 8, i);
		RNDr(S, W, 9, i);
		RNDr(S, W, 10, i);
		RNDr(S, W, 11, i);
		RNDr(S, W, 12, i);
		RNDr(S, W, 13, i);
		RNDr(S, W, 14, i);
		RNDr(S, W, 15, i);

		if (i == 48)
			break;
		MSCH(W, 0, i);
		MSCH(W, 1, i);
		MSCH(W, 2, i);
		MSCH(W, 3, i);
		MSCH(W, 4, i);
		MSCH(W, 5, i);
		MSCH(W, 6, i);
		MSCH(W, 7, i);
		MSCH(W, 8, i);
		MSCH(W, 9, i);
		MSCH(W, 10, i);
		MSCH(W, 11, i);
		MSCH(W, 12, i);
		MSCH(W, 13, i);
		MSCH(W, 14, i);
		MSCH(W, 15, i);
	}

	/* 4. Mix local working variables into global state. */
	for (i = 0; i < 8; i++)
		state[i] += S[i];
}


static void SHA256_Init(SHA256_CTX * ctx)
{
	/* Zero bits processed so far. */
	ctx->count = 0;

	/* Initialize state. */
	memcpy(ctx->state, initial_state, sizeof(initial_state));

}

static void SHA256_Update(SHA256_CTX * ctx, const void * in, size_t len, uint32_t tmp32[static restrict 72])
{
	uint32_t r;
	const uint8_t * src = in;

	/* Return immediately if we have nothing to do. */
	if (len == 0)
		return;

	/* Number of bytes left in the buffer from previous updates. */
	r = (ctx->count >> 3) & 0x3f;

	/* Update number of bits. */
	ctx->count += (uint64_t)(len) << 3;

	/* Handle the case where we don't need to perform any transforms. */
	if (len < 64 - r) {
		memcpy(&ctx->buf[r], src, len);
		return;
	}

	/* Finish the current block. */
	memcpy(&ctx->buf[r], src, 64 - r);
	SHA256_Transform(ctx->state, ctx->buf, &tmp32[0], &tmp32[64]);
	src += 64 - r;
	len -= 64 - r;

	/* Perform complete blocks. */
	while (len >= 64) {
		SHA256_Transform(ctx->state, src, &tmp32[0], &tmp32[64]);
		src += 64;
		len -= 64;
	}

	/* Copy left over data into buffer. */
	memcpy(ctx->buf, src, len);
}

static void init(HMAC_SHA256_CTX* ctx, const void * _K, size_t Klen, uint32_t tmp32[static restrict 72], uint8_t pad[static restrict 64],
    uint8_t khash[static restrict 32]){

    const uint8_t * K = _K;
	size_t i;

    SHA256_Init(&ctx->ictx);
    memset(pad, 0x36, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
    
    SHA256_Update(&ctx->ictx, pad, 64, tmp32);

    SHA256_Init(&ctx->octx);
	memset(pad, 0x5c, 64);
	for (i = 0; i < Klen; i++)
		pad[i] ^= K[i];
    
    SHA256_Update(&ctx->octx, pad, 64, tmp32);

}

void key_derivation_function(const uint8_t * passwd, size_t passwdlen, const uint8_t * salt,
    size_t saltlen, uint64_t c, uint8_t * buf, size_t dkLen){

    HMAC_SHA256_CTX Phctx, PShctx, hctx;
    uint8_t ivec[4];
    uint32_t tmp32[72];
	uint8_t tmp8[96];
	size_t i;
    uint8_t U[32];
	uint8_t T[32];
	uint64_t j;
	int k;
	size_t clen;

    if(dkLen <= 32 * (size_t)(UINT32_MAX)){
        
        init(&Phctx, passwd, passwdlen, tmp32, &tmp8[0], &tmp8[64]);

        memcpy(&PShctx, &Phctx, sizeof(HMAC_SHA256_CTX));
	    SHA256_Update(&PShctx->ictx, salt, saltlen, tmp32);

        /* Iterate through the blocks. */
	    for (i = 0; i * 32 < dkLen; i++) {
		    /* Generate INT(i + 1). */
		    be32enc(ivec, (uint32_t)(i + 1));

		    /* Compute U_1 = PRF(P, S || INT(i)). */
		    memcpy(&hctx, &PShctx, sizeof(HMAC_SHA256_CTX));
		    SHA256_Update(&hctx->ictx, ivec, 4, tmp32);
            be32enc_vect(U, hctx->state, 32);

		    /* T_i = U_1 ... */
		    memcpy(T, U, 32);

		    /* Copy as many bytes as necessary into buf. */
		    clen = dkLen - i * 32;
		    if (clen > 32)
			    clen = 32;
		    memcpy(&buf[i * 32], T, clen);
	    }

    }

}

bool sanityChecks(size_t buflen){

    #if SIZE_MAX > UINT32_MAX
	if (buflen > (((uint64_t)(1) << 32) - 1) * 32) {
		return false;
	}
    #endif

    if ((uint64_t)(r) * (uint64_t)(PARALLELIZATION_FACTOR) >= (1 << 30)) {
		return false;
	}

    if (r > SIZE_MAX / 128 / PARALLELIZATION_FACTOR) {
// #if SIZE_MAX / 256 <= UINT32_MAX
// 	    (r > SIZE_MAX / 256) ||
// #endif) {
		return false;
	}

    return true;

}

COMPARTMENT_ENTRY_POINT(KDF_Inputs* __capability input, Secret* __capability client_derived_secret) {
    uint8_t* blocks;
	// uint8_t* V;
	// uint8_t* XY;
	uint32_t i;
    Secret secret;
    
    bool checks = sanityChecks(OUTPUT_BUFLEN)

    if(!checks){
        CompartmentReturn(-1);
    }
    else {
        /* Allocate memory. */
	    if ((blocks = malloc(128 * blockSize * PARALLELIZATION_FACTOR)) == NULL)
		    CompartmentReturn(-1);

        //do conditional check for the password capability of that is still correct?

        key_derivation_function(&input.passwd, strlen(&input.passwd), &input.salt, strlen(&input.salt), 1, blocks, PARALLELIZATION_FACTOR * 128 * blockSize);

        /* 2: for i = 0 to p - 1 do */
	    for (i = 0; i < PARALLELIZATION_FACTOR; i++) {
		    /* 3: B_i <-- MF(B_i, N) */
            //ensure the blocks data is getting set ele re type cast to temp
            uint8_t* __capability blocks_segment_cap = archcap_c_ddc_cast(&blocks[i * 128 * blockSize]);
            blocks_segment_cap = archcap_c_perms_set(blocks_segment_cap, ARCHCAP_PERM_GLOBAL | ARCHCAP_PERM_STORE | ARCHCAP_PERM_LOAD);
		    // romix(&blocks[i * 128 * r], r, N, V, XY);
            uintcap_t ret = CompartmentCall(kComputeNodeBCompartmentId, AsUintcap(blocks_segment_cap));
            if (ret == 0) {
                std::cout << "[Node A] Returned Block: ";
                PrintKey(&blocks[i * 128 * blockSize]);
            } else {
                std::cout << "[Node A] Node B failed to send block\n";
            }
	    }

        key_derivation_function(&input.passwd, strlen(&input.passwd), blocks, PARALLELIZATION_FACTOR * 128 * blockSize, 1, &secret->output, OUTPUT_BUFLEN);

        
        if (archcap_c_tag_get(client_derived_secret) && (archcap_c_limit_get(client_derived_secret) - archcap_c_address_get(client_derived_secret)) >= sizeof(secret) &&
          (archcap_c_perms_get(client_derived_secret) & ARCHCAP_PERM_STORE) != 0) {
            // Use memcpy_c() to write via the client capability. We use DDC to construct a source
            // capability.
            memcpy_c(client_derived_secret, archcap_c_ddc_cast(&secret), sizeof(secret));
            CompartmentReturn(0);
        } else {
            CompartmentReturn(-1);
        }
    }

}

int main(int, char** argv) {

  std::cout << "[Node A] Parallelization Factor Compartment @" << argv[0] << " initialized" << std::endl;

  // Return to the compartment manager, letting it know that we have completed our initialization.
  CompartmentReturn();
}