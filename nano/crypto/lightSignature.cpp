#include <crypto/blake2/blake2.h>
#include <stdlib.h>

unsigned char *signMsg(unsigned char *Key, unsigned char *msg, int msgLen, unsigned long long msgHeight) {

    // Set Message Height in msg.
    msg[msgLen] = msgHeight & 0xff;
    msg[msgLen + 1] = (msgHeight >> 8) & 0xff;
    msg[msgLen + 2] = (msgHeight >> 16) & 0xff;
    msg[msgLen + 3] = (msgHeight >> 24) & 0xff;
    msg[msgLen + 4] = (msgHeight >> 32) & 0xff;
    msg[msgLen + 5] = (msgHeight >> 40) & 0xff;
    msg[msgLen + 6] = (msgHeight >> 48) & 0xff;
    msg[msgLen + 7] = (msgHeight >> 56) & 0xff;

    unsigned char *Signature = (unsigned char *)malloc(32);

    blake2b_state state;
	blake2b_init_key (&state, 32, Key, 62);
	blake2b_update (&state, msg, msgLen + 8);
	blake2b_final (&state, 32, Signature);

    return Signature;
}