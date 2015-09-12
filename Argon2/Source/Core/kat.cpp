/*
 * Argon2 source code package
 * 
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 * 
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */


#include <cstdio>
#include <inttypes.h>

#include "argon2.h"
#include "argon2-core.h"



#ifdef KAT

void InitialKat(const uint8_t* blockhash, const Argon2_Context* context, Argon2_type type) {
    FILE* fp = fopen(KAT_FILENAME, "a+");

    if (fp && blockhash != NULL && context != NULL) {
        fprintf(fp, "=======================================");

        switch (type) {
            case Argon2_d:
                fprintf(fp, "Argon2d\n");
                break;
            case Argon2_i:
                fprintf(fp, "Argon2i\n");
                break;
            case Argon2_di:
                fprintf(fp, "Argon2di\n");
                break;
            case Argon2_id:
                fprintf(fp, "Argon2id\n");
                break;
            case Argon2_ds:
                fprintf(fp, "Argon2ds\n");
                break;
        }

        fprintf(fp, "Iterations: %d, Memory: %d KBytes, Parallelism: %d lanes, Tag length: %d bytes\n",
                context->t_cost, context->m_cost, context->lanes, context->outlen);


        fprintf(fp, "Password[%d]: ", context->pwdlen);
        if (context->clear_password) {
            fprintf(fp, "CLEARED\n");
        } else {
            for (unsigned i = 0; i < context->pwdlen; ++i) {
                fprintf(fp, "%2.2x ", ((unsigned char*) context->pwd)[i]);
            }
            fprintf(fp, "\n");
        }


        fprintf(fp, "Salt[%d]: ", context->saltlen);
        for (unsigned i = 0; i < context->saltlen; ++i) {
            fprintf(fp, "%2.2x ", ((unsigned char*) context->salt)[i]);
        }
        fprintf(fp, "\n");

        fprintf(fp, "Secret[%d]: ", context->secretlen);

        if (context->clear_secret) {
            fprintf(fp, "CLEARED\n");
        } else {
            for (unsigned i = 0; i < context->secretlen; ++i) {
                fprintf(fp, "%2.2x ", ((unsigned char*) context->secret)[i]);
            }
            fprintf(fp, "\n");
        }

        fprintf(fp, "Associated data[%d]: ", context->adlen);
        for (unsigned i = 0; i < context->adlen; ++i) {
            fprintf(fp, "%2.2x ", ((unsigned char*) context->ad)[i]);
        }
        fprintf(fp, "\n");



        fprintf(fp, "Pre-hashing digest: ");
        for (unsigned i = 0; i < PREHASH_DIGEST_LENGTH; ++i) {
            fprintf(fp, "%2.2x ", ((unsigned char*) blockhash)[i]);
        }
        fprintf(fp, "\n");

        fclose(fp);
    }
}

void PrintTag(const void* out, uint32_t outlen) {
    FILE* fp = fopen(KAT_FILENAME, "a+");

    if (fp && out != NULL) {
        fprintf(fp, "Tag: ");
        for (unsigned i = 0; i < outlen; ++i) {
            fprintf(fp, "%2.2x ", ((uint8_t*) out)[i]);
        }
        fprintf(fp, "\n");

        fclose(fp);
    }
}
#endif


#ifdef KAT_INTERNAL

void InternalKat(const Argon2_instance_t* instance, uint32_t pass) {
    FILE* fp = fopen(KAT_FILENAME, "a+");
    if (fp && instance != NULL) {
        fprintf(fp, "\n After pass %d:\n", pass);
        for (uint32_t i = 0; i < instance->memory_blocks; ++i) {
            uint32_t how_many_words = (instance->memory_blocks > WORDS_IN_BLOCK) ? 1 : WORDS_IN_BLOCK;
            for (uint32_t j = 0; j < how_many_words; ++j)
                fprintf(fp, "Block %.4d [%3d]: %016" PRIx64 "\n", i, j, instance->state[i][j]);
        }

        fclose(fp);
    }
}
#endif
