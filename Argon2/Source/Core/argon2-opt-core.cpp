/*
 * Argon2 source code package
 * 
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 * 
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */


#include <stdint.h>


#if !defined(_MSC_VER)
#include <x86intrin.h>
#else 
#include <emmintrin.h>
#include <smmintrin.h>
#include <intrin.h>
#endif


#include "argon2.h"
#include "argon2-core.h"
#include "kat.h"


#include "blake2-round-mka.h"
#include "blake2-impl.h"
#include "blake2.h"


#if defined(KAT) || defined(KAT_INTERNAL)
/* The KAT file name */
const char* KAT_FILENAME = "kat-argon2-opt.log";
#endif


const __m128i r16 = _mm_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9);
const __m128i r24 = _mm_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10);

__m128i t0, t1;


/*
* Function fills a new memory block
* @param state Pointer to the just produced block. Content will be updated(!)
* @param ref_block Pointer to the reference block
* @param next_block Pointer to the block to be constructed
* @param Sbox Pointer to the Sbox (used in Argon2_ds only)
* @pre all block pointers must be valid
*/
void FillBlock(__m128i* state, const uint8_t *ref_block, uint8_t *next_block, const uint64_t* Sbox) {
    __m128i block_XY[QWORDS_IN_BLOCK];
    //__m128i state[64];



    for (uint8_t i = 0; i < QWORDS_IN_BLOCK; i++) {
        block_XY[i] = _mm_load_si128((__m128i *) ref_block);
        ref_block += 16;
    }
    for (uint8_t i = 0; i < QWORDS_IN_BLOCK; i++) {
        block_XY[i] = state[i] = _mm_xor_si128(state[i], block_XY[i]);
    }

    uint64_t x = 0;
    if (Sbox != NULL) {
		x = _mm_extract_epi64(block_XY[0], 0) ^ _mm_extract_epi64(block_XY[QWORDS_IN_BLOCK - 1],1);
        for (int i = 0; i < 6 * 16; ++i) {
            uint32_t x1 = x >> 32;
            uint32_t x2 = x & 0xFFFFFFFF;
            uint64_t y = Sbox[x1 & SBOX_MASK];
            uint64_t z = Sbox[x2 & SBOX_MASK + SBOX_SIZE / 2];
            x = (uint64_t) x1 * (uint64_t) x2;
            x += y;
            x ^= z;
        }
    }

    BLAKE2_ROUND(state[0], state[1], state[2], state[3],
            state[4], state[5], state[6], state[7]);

    BLAKE2_ROUND(state[8], state[9], state[10], state[11],
            state[12], state[13], state[14], state[15]);

    BLAKE2_ROUND(state[16], state[17], state[18], state[19],
            state[20], state[21], state[22], state[23]);

    BLAKE2_ROUND(state[24], state[25], state[26], state[27],
            state[28], state[29], state[30], state[31]);

    BLAKE2_ROUND(state[32], state[33], state[34], state[35],
            state[36], state[37], state[38], state[39]);

    BLAKE2_ROUND(state[40], state[41], state[42], state[43],
            state[44], state[45], state[46], state[47]);

    BLAKE2_ROUND(state[48], state[49], state[50], state[51],
            state[52], state[53], state[54], state[55]);

    BLAKE2_ROUND(state[56], state[57], state[58], state[59],
            state[60], state[61], state[62], state[63]);


    BLAKE2_ROUND(state[0], state[8], state[16], state[24],
            state[32], state[40], state[48], state[56]);

    BLAKE2_ROUND(state[1], state[9], state[17], state[25],
            state[33], state[41], state[49], state[57]);

    BLAKE2_ROUND(state[2], state[10], state[18], state[26],
            state[34], state[42], state[50], state[58])

            BLAKE2_ROUND(state[3], state[11], state[19], state[27],
            state[35], state[43], state[51], state[59]);

    BLAKE2_ROUND(state[4], state[12], state[20], state[28],
            state[36], state[44], state[52], state[60]);

    BLAKE2_ROUND(state[5], state[13], state[21], state[29],
            state[37], state[45], state[53], state[61]);

    BLAKE2_ROUND(state[6], state[14], state[22], state[30],
            state[38], state[46], state[54], state[62]);

    BLAKE2_ROUND(state[7], state[15], state[23], state[31],
            state[39], state[47], state[55], state[63]);

	for (uint8_t i = 0; i < QWORDS_IN_BLOCK; i++) {
		// Feedback
		state[i] = _mm_xor_si128(state[i], block_XY[i]);
	}
    state[0] = _mm_add_epi64(state[0], _mm_set_epi64x(0, x));
    state[QWORDS_IN_BLOCK - 1] = _mm_add_epi64(state[QWORDS_IN_BLOCK - 1], _mm_set_epi64x(x, 0));
	for (uint8_t i = 0; i < QWORDS_IN_BLOCK; i++) {
        _mm_store_si128((__m128i *) next_block, state[i]);
        next_block += 16;
    }
}

void GenerateAddresses(const Argon2_instance_t* instance, const Argon2_position_t* position, uint64_t* pseudo_rands) {
    block  input_block(0), address_block(0);
    if (instance != NULL && position != NULL) {
        input_block.v[0] = position->pass;
        input_block.v[1] = position->lane;
        input_block.v[2] = position->slice;
        input_block.v[3] = instance->memory_blocks;
        input_block.v[4] = instance->passes;
        input_block.v[5] = instance->type;

        for (uint32_t i = 0; i < instance->segment_length; ++i) {
            if (i % ADDRESSES_IN_BLOCK == 0) {
                input_block.v[6]++;
				block zero_block(0), zero2_block(0);
				FillBlock((__m128i *) & zero_block.v, (uint8_t *)& input_block.v, (uint8_t *)& address_block.v, NULL);
				FillBlock((__m128i *) & zero2_block.v, (uint8_t *)& address_block.v, (uint8_t *)& address_block.v, NULL);
            }
            pseudo_rands[i] = address_block[i % ADDRESSES_IN_BLOCK];
        }
    }
}



/*
* Function that fills the segment using previous segments also from other threads. Identical to the reference code except that it calls optimized FillBlock()
* @param instance Pointer to the current instance
* @param position Current position
* @pre all block pointers must be valid
*/
void FillSegment(const Argon2_instance_t* instance, Argon2_position_t position) {
    uint64_t pseudo_rand, ref_index, ref_lane;
    uint32_t prev_offset, curr_offset;
    __m128i state[64];
	bool data_independent_addressing = (instance->type == Argon2_i) || (instance->type == Argon2_id && (position.pass == 0) && (position.slice < SYNC_POINTS / 2));
    if (instance != NULL) {
        // Pseudo-random values that determine the reference block position
        uint64_t *pseudo_rands = new uint64_t[instance->segment_length];
        if (pseudo_rands != NULL) {
			if (data_independent_addressing) {
                GenerateAddresses(instance, &position, pseudo_rands);
            }

            uint32_t starting_index = 0;
            if ((0 == position.pass) && (0 == position.slice)) {
                starting_index = 2; // we have already generated the first two blocks
            }

            // Offset of the current block
            curr_offset = position.lane * instance->lane_length + position.slice * instance->segment_length + starting_index;
            if (0 == curr_offset % instance->lane_length) {
                // Last block in this lane
                prev_offset = curr_offset + instance->lane_length - 1;
            } else {
                // Previous block
                prev_offset = curr_offset - 1;
            }
            memcpy(state, (uint8_t *) (instance->state + prev_offset), BLOCK_SIZE);
            for (uint32_t i = starting_index; i < instance->segment_length; ++i, ++curr_offset, ++prev_offset) {
                /*1.1 Rotating prev_offset if needed */
                if (curr_offset % instance->lane_length == 1) {
                    prev_offset = curr_offset - 1;
                }

                /* 1.2 Computing the index of the reference block */
                /* 1.2.1 Taking pseudo-random value from the previous block */
				if (data_independent_addressing) {
                    pseudo_rand = pseudo_rands[i];
                } else {
                    pseudo_rand = instance->state[prev_offset][0];
                }

                /* 1.2.2 Computing the lane of the reference block */
                ref_lane = ((pseudo_rand >> 32)) % instance->lanes;
                if ((position.pass == 0) && (position.slice == 0)) {
                    // Can not reference other lanes yet
                    ref_lane = position.lane;
                }

                /* 1.2.3 Computing the number of possible reference block within the lane. */
                position.index = i;
                ref_index = IndexAlpha(instance, &position, pseudo_rand & 0xFFFFFFFF, ref_lane == position.lane);

                /* 2 Creating a new block */
                block* ref_block = instance->state + instance->lane_length * ref_lane + ref_index;
                block* curr_block = instance->state + curr_offset;
                FillBlock(state, (uint8_t *) ref_block->v, (uint8_t *) curr_block->v, instance->Sbox);
            }

            delete[] pseudo_rands;
        }
    }
}

void GenerateSbox(Argon2_instance_t* instance) {
    
	block start_block(instance->state[0]), out_block(0), zero_block(0);
    if (instance == NULL)
        return;
    if (instance->Sbox == NULL)
        instance->Sbox = new uint64_t[SBOX_SIZE];

    for (uint32_t i = 0; i < SBOX_SIZE / WORDS_IN_BLOCK; ++i) {
		block zero_block(0), zero2_block(0);
		FillBlock((__m128i*)zero_block.v, (uint8_t*)start_block.v, (uint8_t*)out_block.v, NULL);
		FillBlock((__m128i*) zero2_block.v, (uint8_t*)out_block.v, (uint8_t*)start_block.v,NULL);
        memcpy(instance->Sbox + i*WORDS_IN_BLOCK, start_block.v, BLOCK_SIZE);
    }
}
