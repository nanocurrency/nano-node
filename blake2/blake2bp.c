/*
   BLAKE2 reference source code package - optimized C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along with
   this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if defined(_OPENMP)
#include <omp.h>
#endif

#include "blake2.h"
#include "blake2-impl.h"

#define PARALLELISM_DEGREE 4

static inline int blake2bp_init_leaf( blake2b_state *S, uint8_t outlen, uint8_t keylen, uint64_t offset )
{
  blake2b_param P[1];
  P->digest_length = outlen;
  P->key_length = keylen;
  P->fanout = PARALLELISM_DEGREE;
  P->depth = 2;
  P->leaf_length = 0;
  P->node_offset = offset;
  P->node_depth = 0;
  P->inner_length = BLAKE2B_OUTBYTES;
  memset( P->reserved, 0, sizeof( P->reserved ) );
  memset( P->salt, 0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  return blake2b_init_param( S, P );
}

static inline int blake2bp_init_root( blake2b_state *S, uint8_t outlen, uint8_t keylen )
{
  blake2b_param P[1];
  P->digest_length = outlen;
  P->key_length = keylen;
  P->fanout = PARALLELISM_DEGREE;
  P->depth = 2;
  P->leaf_length = 0;
  P->node_offset = 0;
  P->node_depth = 1;
  P->inner_length = BLAKE2B_OUTBYTES;
  memset( P->reserved, 0, sizeof( P->reserved ) );
  memset( P->salt, 0, sizeof( P->salt ) );
  memset( P->personal, 0, sizeof( P->personal ) );
  return blake2b_init_param( S, P );
}


int blake2bp_init( blake2bp_state *S, const uint8_t outlen )
{
  if( !outlen || outlen > BLAKE2B_OUTBYTES ) return -1;

  memset( S->buf, 0, sizeof( S->buf ) );
  S->buflen = 0;

  if( blake2bp_init_root( S->R, outlen, 0 ) < 0 )
    return -1;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2bp_init_leaf( S->S[i], outlen, 0, i ) < 0 ) return -1;

  S->R->last_node = 1;
  S->S[PARALLELISM_DEGREE - 1]->last_node = 1;
  return 0;
}

int blake2bp_init_key( blake2bp_state *S, const uint8_t outlen, const void *key, const uint8_t keylen )
{
  if( !outlen || outlen > BLAKE2B_OUTBYTES ) return -1;

  if( !key || !keylen || keylen > BLAKE2B_KEYBYTES ) return -1;

  memset( S->buf, 0, sizeof( S->buf ) );
  S->buflen = 0;

  if( blake2bp_init_root( S->R, outlen, keylen ) < 0 )
    return -1;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2bp_init_leaf( S->S[i], outlen, keylen, i ) < 0 ) return -1;

  S->R->last_node = 1;
  S->S[PARALLELISM_DEGREE - 1]->last_node = 1;
  {
    uint8_t block[BLAKE2B_BLOCKBYTES];
    memset( block, 0, BLAKE2B_BLOCKBYTES );
    memcpy( block, key, keylen );

    for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2b_update( S->S[i], block, BLAKE2B_BLOCKBYTES );

    secure_zero_memory( block, BLAKE2B_BLOCKBYTES ); /* Burn the key from stack */
  }
  return 0;
}


int blake2bp_update( blake2bp_state *S, const uint8_t *in, uint64_t inlen )
{
  size_t left = S->buflen;
  size_t fill = sizeof( S->buf ) - left;

  if( left && inlen >= fill )
  {
    memcpy( S->buf + left, in, fill );

    for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2b_update( S->S[i], S->buf + i * BLAKE2B_BLOCKBYTES, BLAKE2B_BLOCKBYTES );

    in += fill;
    inlen -= fill;
    left = 0;
  }

#if defined(_OPENMP)
  #pragma omp parallel shared(S), num_threads(PARALLELISM_DEGREE)
#else

  for( size_t id__ = 0; id__ < PARALLELISM_DEGREE; ++id__ )
#endif
  {
#if defined(_OPENMP)
    size_t      id__ = omp_get_thread_num();
#endif
    uint64_t inlen__ = inlen;
    const uint8_t *in__ = ( const uint8_t * )in;
    in__ += id__ * BLAKE2B_BLOCKBYTES;

    while( inlen__ >= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES )
    {
      blake2b_update( S->S[id__], in__, BLAKE2B_BLOCKBYTES );
      in__ += PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
      inlen__ -= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
    }
  }

  in += inlen - inlen % ( PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES );
  inlen %= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;

  if( inlen > 0 )
    memcpy( S->buf + left, in, inlen );

  S->buflen = left + inlen;
  return 0;
}



int blake2bp_final( blake2bp_state *S, uint8_t *out, const uint8_t outlen )
{
  uint8_t hash[PARALLELISM_DEGREE][BLAKE2B_OUTBYTES];

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
  {
    if( S->buflen > i * BLAKE2B_BLOCKBYTES )
    {
      size_t left = S->buflen - i * BLAKE2B_BLOCKBYTES;

      if( left > BLAKE2B_BLOCKBYTES ) left = BLAKE2B_BLOCKBYTES;

      blake2b_update( S->S[i], S->buf + i * BLAKE2B_BLOCKBYTES, left );
    }

    blake2b_final( S->S[i], hash[i], BLAKE2B_OUTBYTES );
  }

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    blake2b_update( S->R, hash[i], BLAKE2B_OUTBYTES );

  blake2b_final( S->R, out, outlen );
  return 0;
}

int blake2bp( uint8_t *out, const void *in, const void *key, uint8_t outlen, uint64_t inlen, uint8_t keylen )
{
  uint8_t hash[PARALLELISM_DEGREE][BLAKE2B_OUTBYTES];
  blake2b_state S[PARALLELISM_DEGREE][1];
  blake2b_state FS[1];

  /* Verify parameters */
  if ( NULL == in ) return -1;

  if ( NULL == out ) return -1;

  if ( NULL == key ) keylen = 0;

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    if( blake2bp_init_leaf( S[i], outlen, keylen, i ) < 0 ) return -1;

  S[PARALLELISM_DEGREE - 1]->last_node = 1; // mark last node

  if( keylen > 0 )
  {
    uint8_t block[BLAKE2B_BLOCKBYTES];
    memset( block, 0, BLAKE2B_BLOCKBYTES );
    memcpy( block, key, keylen );

    for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
      blake2b_update( S[i], block, BLAKE2B_BLOCKBYTES );

    secure_zero_memory( block, BLAKE2B_BLOCKBYTES ); /* Burn the key from stack */
  }

#if defined(_OPENMP)
  #pragma omp parallel shared(S,hash), num_threads(PARALLELISM_DEGREE)
#else

  for( size_t id__ = 0; id__ < PARALLELISM_DEGREE; ++id__ )
#endif
  {
#if defined(_OPENMP)
    size_t      id__ = omp_get_thread_num();
#endif
    uint64_t inlen__ = inlen;
    const uint8_t *in__ = ( const uint8_t * )in;
    in__ += id__ * BLAKE2B_BLOCKBYTES;

    while( inlen__ >= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES )
    {
      blake2b_update( S[id__], in__, BLAKE2B_BLOCKBYTES );
      in__ += PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
      inlen__ -= PARALLELISM_DEGREE * BLAKE2B_BLOCKBYTES;
    }

    if( inlen__ > id__ * BLAKE2B_BLOCKBYTES )
    {
      const size_t left = inlen__ - id__ * BLAKE2B_BLOCKBYTES;
      const size_t len = left <= BLAKE2B_BLOCKBYTES ? left : BLAKE2B_BLOCKBYTES;
      blake2b_update( S[id__], in__, len );
    }

    blake2b_final( S[id__], hash[id__], BLAKE2B_OUTBYTES );
  }

  if( blake2bp_init_root( FS, outlen, keylen ) < 0 )
    return -1;

  FS->last_node = 1; // Mark as last node

  for( size_t i = 0; i < PARALLELISM_DEGREE; ++i )
    blake2b_update( FS, hash[i], BLAKE2B_OUTBYTES );

  blake2b_final( FS, out, outlen );
  return 0;
}


#if defined(BLAKE2BP_SELFTEST)
#include <string.h>
#include "blake2-kat.h"
int main( int argc, char **argv )
{
  uint8_t key[BLAKE2B_KEYBYTES];
  uint8_t buf[KAT_LENGTH];

  for( size_t i = 0; i < BLAKE2B_KEYBYTES; ++i )
    key[i] = ( uint8_t )i;

  for( size_t i = 0; i < KAT_LENGTH; ++i )
    buf[i] = ( uint8_t )i;

  for( size_t i = 0; i < KAT_LENGTH; ++i )
  {
    uint8_t hash[BLAKE2B_OUTBYTES];
    //blake2bp( hash, buf, key, BLAKE2B_OUTBYTES, i, BLAKE2B_KEYBYTES );
    blake2bp_state S[1];
    blake2bp_init_key( S, BLAKE2B_OUTBYTES, key, BLAKE2B_KEYBYTES );
    blake2bp_update( S, buf, i );
    blake2bp_final( S, hash, BLAKE2B_OUTBYTES );

    if( 0 != memcmp( hash, blake2bp_keyed_kat[i], BLAKE2B_OUTBYTES ) )
    {
      puts( "error" );
      return -1;
    }
  }

  puts( "ok" );
  return 0;
}
#endif

