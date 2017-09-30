/*
 * Argon2 source code package
 * 
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 * 
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */


using namespace std;

/*For memory wiping*/
#ifdef _MSC_VER
#include "windows.h"
#include "winbase.h" //For SecureZeroMemory
#endif
#if defined __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif
#define VC_GE_2005( version )		( version >= 1400 )


#include <inttypes.h>
#include <vector>
#include <thread>
#include <cstring>

#include "argon2.h"
#include "argon2-core.h"
#include "kat.h"


#include "blake2.h"
#include "blake2-impl.h"

 #if defined(__clang__)
#if __has_attribute(optnone)
#define NOT_OPTIMIZED __attribute__((optnone))
#endif
#elif defined(__GNUC__)
#define GCC_VERSION (__GNUC__ * 10000 \
                    + __GNUC_MINOR__ * 100 \
                    + __GNUC_PATCHLEVEL__)
#if GCC_VERSION >= 40400
 #define NOT_OPTIMIZED __attribute__((optimize("O0")))
#endif
#else 
#define NOT_OPTIMIZED
#endif




block operator^(const block& l, const block& r) {
    block a = l;
    a ^= r;
    return a;
}

int AllocateMemory(block **memory, uint32_t m_cost) {
    if (memory != NULL) {
        *memory = new block[m_cost];
        if (!*memory) {
            return ARGON2_MEMORY_ALLOCATION_ERROR;
        }
        return ARGON2_OK;
    } else return ARGON2_MEMORY_ALLOCATION_ERROR;
}

/* Function that securely cleans the memory
* @param mem Pointer to the memory
* @param s Memory size in bytes
*/

static inline void NOT_OPTIMIZED secure_wipe_memory( void *v, size_t n )
{
#if defined  (_MSC_VER ) &&  VC_GE_2005( _MSC_VER )
    SecureZeroMemory(v,n);
#elif defined memset_s
    memset_s(v, n);
#elif defined( __OpenBSD__ )
	explicit_bzero( memory, size );
#else
	static void* (*const volatile memset_sec)(void*, int, size_t) = &memset;
    memset_sec(v,0,n);
#endif
} 

void FreeMemory(Argon2_instance_t* instance, bool clear) {
    if (instance->state != NULL) {
        if (clear) {
            if (instance->type == Argon2_ds && instance->Sbox != NULL) {
                secure_wipe_memory(instance->Sbox, SBOX_SIZE * sizeof (uint64_t));
            }
            secure_wipe_memory(instance->state, sizeof (block) * instance->memory_blocks);
        }
        delete[] instance->state;
        if (instance->Sbox != NULL)
            delete[] instance->Sbox;
    }
}
int blake2b_long(uint8_t *out, const void *in, const uint32_t outlen, const uint64_t inlen)
{
	blake2b_state blake_state;
	if (outlen <= BLAKE2B_OUTBYTES)
	{
		blake2b_init(&blake_state, outlen);
		blake2b_update(&blake_state, (const uint8_t*)&outlen, sizeof(uint32_t));
		blake2b_update(&blake_state, (const uint8_t *)in, inlen);
		blake2b_final(&blake_state, out, outlen);
	}
	else
	{
		uint8_t out_buffer[BLAKE2B_OUTBYTES];
		uint8_t in_buffer[BLAKE2B_OUTBYTES];
		blake2b_init(&blake_state, BLAKE2B_OUTBYTES);
		blake2b_update(&blake_state, (const uint8_t*)&outlen, sizeof(uint32_t));
		blake2b_update(&blake_state, (const uint8_t *)in, inlen);
		blake2b_final(&blake_state, out_buffer, BLAKE2B_OUTBYTES);
		memcpy(out, out_buffer, BLAKE2B_OUTBYTES / 2);
		out += BLAKE2B_OUTBYTES / 2;
		uint32_t toproduce = outlen - BLAKE2B_OUTBYTES / 2;
		while (toproduce > BLAKE2B_OUTBYTES)
		{
			memcpy(in_buffer, out_buffer, BLAKE2B_OUTBYTES);
			blake2b_argon(out_buffer, in_buffer, NULL, BLAKE2B_OUTBYTES, BLAKE2B_OUTBYTES, 0);
			memcpy(out, out_buffer, BLAKE2B_OUTBYTES / 2);
			out += BLAKE2B_OUTBYTES / 2;
			toproduce -= BLAKE2B_OUTBYTES / 2;
		}
		memcpy(in_buffer, out_buffer, BLAKE2B_OUTBYTES);
		blake2b_argon(out_buffer, in_buffer, NULL, toproduce, BLAKE2B_OUTBYTES, 0);
		memcpy(out, out_buffer, toproduce);

	}
	return 0;
}

void Finalize(const Argon2_Context *context, Argon2_instance_t* instance) {
    if (context != NULL && instance != NULL) {
        block blockhash = instance->state[instance->lane_length - 1];

        // XOR the last blocks
        for (uint8_t l = 1; l < instance->lanes; ++l) {
            uint32_t last_block_in_lane = l * instance->lane_length + (instance->lane_length - 1);
            blockhash ^= instance->state[last_block_in_lane];

        }

        // Hash the result
        blake2b_long(context->out, (uint8_t*) blockhash.v, context->outlen, BLOCK_SIZE);
        secure_wipe_memory(blockhash.v,  BLOCK_SIZE); //clear the blockhash
#ifdef KAT
        PrintTag(context->out, context->outlen);
#endif 

        // Deallocate the memory
        if (NULL != context->free_cbk) {
            context->free_cbk((uint8_t *) instance->state, instance->memory_blocks * sizeof (block));
        } else {
            FreeMemory(instance, context->clear_memory);
        }

    }
}

uint32_t IndexAlpha(const Argon2_instance_t* instance, const Argon2_position_t* position, uint32_t pseudo_rand, bool same_lane) {
    /*
     * Pass 0:
     *      This lane : all already finished segments plus already constructed blocks in this segment
     *      Other lanes : all already finished segments
     * Pass 1+:
     *      This lane : (SYNC_POINTS - 1) last segments plus already constructed blocks in this segment
     *      Other lanes : (SYNC_POINTS - 1) last segments 
     */
    uint32_t reference_area_size;

    if (0 == position->pass) {
        // First pass
        if (0 == position->slice) {
            // First slice
            reference_area_size = position->index - 1; // all but the previous
        } else {
            if (same_lane) {
                // The same lane => add current segment
                reference_area_size = position->slice * instance->segment_length + position->index - 1;
            } else {
                reference_area_size = position->slice * instance->segment_length + ((position->index == 0) ? (-1) : 0);
            }
        }
    } else {
        // Second pass
        if (same_lane) {
            reference_area_size = instance->lane_length - instance->segment_length + position->index - 1;
        } else {
            reference_area_size = instance->lane_length - instance->segment_length + ((position->index == 0) ? (-1) : 0);
        }
    }

    /* 1.2.4. Mapping pseudo_rand to 0..<reference_area_size-1> and produce relative position */
    uint64_t relative_position = pseudo_rand;
    relative_position = relative_position * relative_position >> 32;
    relative_position = reference_area_size - 1 - (reference_area_size * relative_position >> 32);

    /* 1.2.5 Computing starting position */
    uint32_t start_position = 0;
    if (0 != position->pass) {
        start_position = (position->slice == SYNC_POINTS - 1) ? 0 : (position->slice + 1) * instance->segment_length;
    }

    /* 1.2.6. Computing absolute position */
    uint32_t absolute_position = (start_position + relative_position) % instance->lane_length; // absolute position
    return absolute_position;
}

void FillMemoryBlocks(Argon2_instance_t* instance) {
    vector<thread> Threads;
    if (instance != NULL) {
        for (uint8_t r = 0; r < instance->passes; ++r) {
            if (Argon2_ds == instance->type) {
                GenerateSbox(instance);
            }
            for (uint8_t s = 0; s < SYNC_POINTS; ++s) {
                for (uint8_t l = 0; l < instance->lanes; ++l) {
                    Threads.push_back(thread(FillSegment, instance, Argon2_position_t(r, l, s, 0)));
                }

                for (auto& t : Threads) {
                    t.join();
                }
                Threads.clear();
            }

#ifdef KAT_INTERNAL
            InternalKat(instance, r);
#endif

        }
    }
}

int ValidateInputs(const Argon2_Context* context) {
    if (NULL == context) {
        return ARGON2_INCORRECT_PARAMETER;
    }

    if (NULL == context->out) {
        return ARGON2_OUTPUT_PTR_NULL;
    }

    /* Validate output length */
    if (MIN_OUTLEN > context->outlen) {
        return ARGON2_OUTPUT_TOO_SHORT;
    }
    if (MAX_OUTLEN < context->outlen) {
        return ARGON2_OUTPUT_TOO_LONG;
    }

    /* Validate password length */
    if (NULL == context->pwd) {
        if (0 != context->pwdlen) {
            return ARGON2_PWD_PTR_MISMATCH;
        }
    } else {
        if (MIN_PWD_LENGTH > context->pwdlen) {
            return ARGON2_PWD_TOO_SHORT;
        }
        if (MAX_PWD_LENGTH < context->pwdlen) {
            return ARGON2_PWD_TOO_LONG;
        }
    }

    /* Validate salt length */
    if (NULL == context->salt) {
        if (0 != context->saltlen) {
            return ARGON2_SALT_PTR_MISMATCH;
        }
    } else {
        if (MIN_SALT_LENGTH > context->saltlen) {
            return ARGON2_SALT_TOO_SHORT;
        }
        if (MAX_SALT_LENGTH < context->saltlen) {
            return ARGON2_SALT_TOO_LONG;
        }
    }

    /* Validate secret length */
    if (NULL == context->secret) {
        if (0 != context->secretlen) {
            return ARGON2_SECRET_PTR_MISMATCH;
        }
    } else {
        if (MIN_SECRET > context->secretlen) {
            return ARGON2_SECRET_TOO_SHORT;
        }
        if (MAX_SECRET < context->secretlen) {
            return ARGON2_SECRET_TOO_LONG;
        }
    }

    /* Validate associated data */
    if (NULL == context->ad) {
        if (0 != context->adlen) {
            return ARGON2_AD_PTR_MISMATCH;
        }
    } else {
        if (MIN_AD_LENGTH > context->adlen) {
            return ARGON2_AD_TOO_SHORT;
        }
        if (MAX_AD_LENGTH < context->adlen) {
            return ARGON2_AD_TOO_LONG;
        }
    }

    /* Validate memory cost */
    if (MIN_MEMORY > context->m_cost) {
        return ARGON2_MEMORY_TOO_LITTLE;
    }
    if (MAX_MEMORY < context->m_cost) {
        return ARGON2_MEMORY_TOO_MUCH;
    }

    /* Validate time cost */
    if (MIN_TIME > context->t_cost) {
        return ARGON2_TIME_TOO_SMALL;
    }
    if (MAX_TIME < context->t_cost) {
        return ARGON2_TIME_TOO_LARGE;
    }

    /* Validate lanes */
    if (MIN_LANES > context->lanes) {
        return ARGON2_LANES_TOO_FEW;
    }
    if (MAX_LANES < context->lanes) {
        return ARGON2_LANES_TOO_MANY;
    }

    if (NULL != context->allocate_cbk && NULL == context->free_cbk) {
        return ARGON2_FREE_MEMORY_CBK_NULL;
    }

    if (NULL == context->allocate_cbk && NULL != context->free_cbk) {
        return ARGON2_ALLOCATE_MEMORY_CBK_NULL;
    }

    return ARGON2_OK;
}

void FillFirstBlocks(uint8_t* blockhash, const Argon2_instance_t* instance) {
    // Make the first and second block in each lane as G(H0||i||0) or G(H0||i||1)
    for (uint8_t l = 0; l < instance->lanes; ++l) {
        blockhash[PREHASH_DIGEST_LENGTH + 4] = l;
        blockhash[PREHASH_DIGEST_LENGTH] = 0;
        blake2b_long((uint8_t*) (instance->state[l * instance->lane_length].v), blockhash, BLOCK_SIZE, PREHASH_SEED_LENGTH);

        blockhash[PREHASH_DIGEST_LENGTH] = 1;
        blake2b_long((uint8_t*) (instance->state[l * instance->lane_length + 1].v), blockhash, BLOCK_SIZE, PREHASH_SEED_LENGTH);
    }
}

void InitialHash(uint8_t* blockhash, Argon2_Context* context, Argon2_type type) {
    blake2b_state BlakeHash;
    uint8_t value[sizeof (uint32_t)];

    if (NULL == context || NULL == blockhash) {
        return;
    }

    blake2b_init(&BlakeHash, PREHASH_DIGEST_LENGTH);

    store32(&value, context->lanes);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));

    store32(&value, context->outlen);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));

    store32(&value, context->m_cost);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));

    store32(&value, context->t_cost);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));

    store32(&value, VERSION_NUMBER);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));

    store32(&value, (uint32_t) type);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));

    store32(&value, context->pwdlen);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));
    if (context->pwd != NULL) {
        blake2b_update(&BlakeHash, (const uint8_t*) context->pwd, context->pwdlen);
        if (context->clear_password) {
            secure_wipe_memory(context->pwd,  context->pwdlen);
            context->pwdlen = 0;
        }
    }

    store32(&value, context->saltlen);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));
    if (context->salt != NULL) {
        blake2b_update(&BlakeHash, (const uint8_t*) context->salt, context->saltlen);
    }

    store32(&value, context->secretlen);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));
    if (context->secret != NULL) {
        blake2b_update(&BlakeHash, (const uint8_t*) context->secret, context->secretlen);
        if (context->clear_secret) {
            secure_wipe_memory(context->secret, context->secretlen);
            context->secretlen = 0;
        }
    }

    store32(&value, context->adlen);
    blake2b_update(&BlakeHash, (const uint8_t*) &value, sizeof (value));
    if (context->ad != NULL) {
        blake2b_update(&BlakeHash, (const uint8_t*) context->ad, context->adlen);
    }
    blake2b_final(&BlakeHash, blockhash, PREHASH_DIGEST_LENGTH);
}

int Initialize(Argon2_instance_t* instance, Argon2_Context* context) {
    if (instance == NULL || context == NULL)
        return ARGON2_INCORRECT_PARAMETER;
    // 1. Memory allocation
    int result = ARGON2_OK;
    if (NULL != context->allocate_cbk) {
        result = context->allocate_cbk((uint8_t **)&(instance->state), instance->memory_blocks * BLOCK_SIZE);
    } else {
        result = AllocateMemory(&(instance->state), instance->memory_blocks);
    }

    if (ARGON2_OK != result) {
        return result;
    }

    // 2. Initial hashing
    // H_0 + 8 extra bytes to produce the first blocks
    uint8_t blockhash[PREHASH_SEED_LENGTH];
    // Hashing all inputs
    InitialHash(blockhash, context, instance->type);
    // Zeroing 8 extra bytes
    secure_wipe_memory(blockhash + PREHASH_DIGEST_LENGTH, PREHASH_SEED_LENGTH - PREHASH_DIGEST_LENGTH);

#ifdef KAT
    InitialKat(blockhash, context, instance->type);
#endif

    // 3. Creating first blocks, we always have at least two blocks in a slice
    FillFirstBlocks(blockhash, instance);
    // Clearing the hash
    secure_wipe_memory(blockhash,  PREHASH_SEED_LENGTH);

    return ARGON2_OK;
}

int Argon2Core(Argon2_Context* context, Argon2_type type) {
    /* 1. Validate all inputs */
    int result = ValidateInputs(context);
    if (ARGON2_OK != result) {
        return result;
    }
    if (type > MAX_ARGON2_TYPE)
        return ARGON2_INCORRECT_TYPE;

    /* 2. Align memory size */
    // Minimum memory_blocks = 8L blocks, where L is the number of lanes
    uint32_t memory_blocks = context->m_cost;
    if (memory_blocks < 2 * SYNC_POINTS * context->lanes) {
        memory_blocks = 2 * SYNC_POINTS * context->lanes;
    }
    uint32_t segment_length = memory_blocks / (context->lanes * SYNC_POINTS);
    // Ensure that all segments have equal length
    memory_blocks = segment_length * (context->lanes * SYNC_POINTS);
    Argon2_instance_t instance(NULL, type, context->t_cost, memory_blocks, context->lanes);

    /* 3. Initialization: Hashing inputs, allocating memory, filling first blocks */
    result = Initialize(&instance, context);
    if (ARGON2_OK != result) {
        return result;
    }

    /* 4. Filling memory */
    FillMemoryBlocks(&instance);

    /* 5. Finalization */
    Finalize(context, &instance);

    return ARGON2_OK;
}
