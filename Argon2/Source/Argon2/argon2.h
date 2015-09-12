/*
 * Argon2 source code package
 * 
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 * 
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */


#ifndef __ARGON2_H__
#define __ARGON2_H__

#include <cstddef>


/************************* Constants to enable Known Answer Tests (KAT)  **************************************************/
/* Enable KAT */
//#define KAT
//#define KAT_INTERNAL


#if defined(KAT) || defined(KAT_INTERNAL)
/* The KAT file name */
extern const char* KAT_FILENAME;
#endif 


/*************************Argon2 input parameter restrictions**************************************************/

/* Minimum and maximum number of lanes (degree of parallelism) */
const uint8_t MIN_LANES = 1;
const uint8_t MAX_LANES = 255;

/* Number of synchronization points between lanes per pass */
const uint32_t SYNC_POINTS = 4;

/* Minimum and maximum digest size in bytes */
const uint32_t MIN_OUTLEN = 4;
const uint32_t MAX_OUTLEN = 0xFFFFFFFF;

/* Minimum and maximum number of memory blocks (each of BLOCK_SIZE bytes) */
const uint32_t MIN_MEMORY = 2 * SYNC_POINTS; // 2 blocks per slice
const uint32_t MAX_MEMORY = 0xFFFFFFFF; // 2^32-1 blocks

/* Minimum and maximum number of passes */
const uint32_t MIN_TIME = 1;
const uint32_t MAX_TIME = 0xFFFFFFFF;

/* Minimum and maximum password length in bytes */
const uint32_t MIN_PWD_LENGTH = 0;
const uint32_t MAX_PWD_LENGTH = 0xFFFFFFFF;

/* Minimum and maximum associated data length in bytes */
const uint32_t MIN_AD_LENGTH = 0;
const uint32_t MAX_AD_LENGTH = 0xFFFFFFFF;

/* Minimum and maximum salt length in bytes */
const uint32_t MIN_SALT_LENGTH = 8;
const uint32_t MAX_SALT_LENGTH = 0xFFFFFFFF;

/* Minimum and maximum key length in bytes */
const uint32_t MIN_SECRET = 0;
const uint32_t MAX_SECRET = 0xFFFFFFFF;

/************************* Error codes *********************************************************************************/
enum Argon2_ErrorCodes {
    ARGON2_OK = 0,

    ARGON2_OUTPUT_PTR_NULL = 1,

    ARGON2_OUTPUT_TOO_SHORT = 2,
    ARGON2_OUTPUT_TOO_LONG = 3,

    ARGON2_PWD_TOO_SHORT = 4,
    ARGON2_PWD_TOO_LONG = 5,

    ARGON2_SALT_TOO_SHORT = 6,
    ARGON2_SALT_TOO_LONG = 7,

    ARGON2_AD_TOO_SHORT = 8,
    ARGON2_AD_TOO_LONG = 9,

    ARGON2_SECRET_TOO_SHORT = 10,
    ARGON2_SECRET_TOO_LONG = 11,

    ARGON2_TIME_TOO_SMALL = 12,
    ARGON2_TIME_TOO_LARGE = 13,

    ARGON2_MEMORY_TOO_LITTLE = 14,
    ARGON2_MEMORY_TOO_MUCH = 15,

    ARGON2_LANES_TOO_FEW = 16,
    ARGON2_LANES_TOO_MANY = 17,

    ARGON2_PWD_PTR_MISMATCH = 18, //NULL ptr with non-zero length
    ARGON2_SALT_PTR_MISMATCH = 19, //NULL ptr with non-zero length
    ARGON2_SECRET_PTR_MISMATCH = 20, //NULL ptr with non-zero length
    ARGON2_AD_PTR_MISMATCH = 21, //NULL ptr with non-zero length

    ARGON2_MEMORY_ALLOCATION_ERROR = 22,

    ARGON2_FREE_MEMORY_CBK_NULL = 23,
    ARGON2_ALLOCATE_MEMORY_CBK_NULL = 24,

    ARGON2_INCORRECT_PARAMETER = 25,
    ARGON2_INCORRECT_TYPE = 26,

    ARGON2_OUT_PTR_MISMATCH = 27,

    ARGON2_ERROR_CODES_LENGTH /* Do NOT remove; Do NOT add error codes after this error code */
};



/********************************************* Memory allocator types --- for external allocation *************************************************************/
typedef int (*AllocateMemoryCallback)(uint8_t **memory, size_t bytes_to_allocate);
typedef void(*FreeMemoryCallback)(uint8_t *memory, size_t bytes_to_allocate);

/********************************************* Argon2 external data structures*************************************************************/

/*
 *****Context: structure to hold Argon2 inputs: 
 * output array and its length, 
 * password and its length,
 * salt and its length,
 * secret and its length,
 * associated data and its length,
 * number of passes, amount of used memory (in KBytes, can be rounded up a bit)
 * number of parallel threads that will be run.
 * All the parameters above affect the output hash value.
 * Additionally, two function pointers can be provided to allocate and deallocate the memory (if NULL, memory will be allocated internally).
 * Also, three flags indicate whether to erase password, secret as soon as they are pre-hashed (and thus not needed anymore), and the entire memory
 ****************************
 Simplest situation: you have output array out[8], password is stored in pwd[32], salt is stored in salt[16], you do not have keys nor associated data.
 You need to spend 1 GB of RAM and you run 5 passes of Argon2d with 4 parallel lanes.
 You want to erase the password, but you're OK with last pass not being erased.
 You want to use the default memory allocator.
 Then you initialize
 Argon2_Context(out,8,pwd,32,salt,16,NULL,0,NULL,0,5,1<<20,4,NULL,NULL,true,false,false).
 */
struct Argon2_Context {
    uint8_t *out; //output array
    const uint32_t outlen; //digest length

    uint8_t *pwd; //password array
    uint32_t pwdlen; //password length

    const uint8_t *salt; //salt array
    const uint32_t saltlen; //salt length

    uint8_t *secret; //key array
    uint32_t secretlen; //key length

    const uint8_t *ad; //associated data array
    const uint32_t adlen; //associated data length

    const uint32_t t_cost; //number of passes
    const uint32_t m_cost; //amount of memory requested (KB)
    const uint32_t lanes; //number of parallel threads

    AllocateMemoryCallback allocate_cbk; //pointer to memory allocator
    FreeMemoryCallback free_cbk; //pointer to memory deallocator

    const bool clear_password; //whether to clear the password array
    const bool clear_secret; //whether to clear the secret array
    const bool clear_memory; //whether to clear the memory after the run

    Argon2_Context(uint8_t *o, uint32_t olen,
            /*const*/ uint8_t *m, uint32_t mlen,
            /*const*/ uint8_t *n, uint32_t nlen,
            /*const*/ uint8_t *s, uint32_t slen,
            /*const*/ uint8_t *a, uint32_t alen,
            uint32_t t_c, uint32_t m_c, uint32_t l,
            AllocateMemoryCallback a_cbk = NULL, FreeMemoryCallback f_cbk = NULL, bool c_p = true, bool c_s = true, bool c_m = false) : out(o), outlen(olen),
    pwd(m), pwdlen(mlen),
    salt(n), saltlen(nlen),
    secret(s), secretlen(slen),
    ad(a), adlen(alen),
    t_cost(t_c), m_cost(m_c), lanes(l),
    allocate_cbk(a_cbk), free_cbk(f_cbk), clear_password(c_p), clear_secret(c_s), clear_memory(c_m) {
    }
};

/**
 * Function to hash the inputs in the memory-hard fashion
 * @param  out  Pointer to the memory where the hash digest will be written
 * @param  outlen Digest length in bytes
 * @param  in Pointer to the input (password)
 * @param  inlen Input length in bytes
 * @param  salt Pointer to the salt
 * @param  saltlen Salt length in bytes
 * @pre    @a out must have at least @a outlen bytes allocated
 * @pre    @a in must be at least @inlen bytes long
 * @pre    @a saltlen must be at least @saltlen bytes long
 * @return Zero if successful, 1 otherwise.
 */
extern "C" int PHS(void *out, size_t outlen, const void *in, size_t inlen, const void *salt, size_t saltlen,
        unsigned int t_cost, unsigned int m_cost);

/*
 * **************Argon2d: Version of Argon2 that picks memory blocks depending on the password and salt. Only for side-channel-free environment!!***************
 * @param  context  Pointer to current Argon2 context
 * @return  Zero if successful, a non zero error code otherwise
 */
extern int Argon2d(Argon2_Context* context);

/*
 *  * **************Argon2i: Version of Argon2 that picks memory blocks independing on the password and salt. Good for side-channels,
 ******************* but worse w.r.t. tradeoff attacks if
 *******************only one pass is used***************
 * @param  context  Pointer to current Argon2 context
 * @return  Zero if successful, a non zero error code otherwise
 */
extern int Argon2i(Argon2_Context* context);

/*
 *   * **************Argon2di: Reserved name***************
 * @param  context  Pointer to current Argon2 context
 * @return  Zero if successful, a non zero error code otherwise
 */
extern int Argon2di(Argon2_Context* context);

/*
 *   * **************Argon2ds: Argon2d hardened against GPU attacks, 20% slower***************
 * @param  context  Pointer to current Argon2 context
 * @return  Zero if successful, a non zero error code otherwise
 */
extern int Argon2ds(Argon2_Context* context);


/*
 *   * **************Argon2id: First half-pass over memory is password-independent, the rest are password-dependent
 ********************OK against side channels: they reduce to 1/2-pass Argon2i***************
 * @param  context  Pointer to current Argon2 context
 * @return  Zero if successful, a non zero error code otherwise
 */
extern int Argon2id(Argon2_Context* context);

/*
 * Verify if a given password is correct for Argon2d hashing
 * @param  context  Pointer to current Argon2 context
 * @param  hash  The password hash to verify. The length of the hash is specified by the context outlen member
 * @return  Zero if successful, a non zero error code otherwise
 */
extern int VerifyD(Argon2_Context* context, const char *hash);

/*
 * Get the associated error message for given erro code
 * @return  The error message associated with the given error code
 */
const char* ErrorMessage(int error_code);

#endif
