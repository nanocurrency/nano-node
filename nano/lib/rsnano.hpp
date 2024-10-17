#ifndef rs_nano_bindings_hpp
#define rs_nano_bindings_hpp

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>

namespace rsnano {

extern "C" {

int32_t rsn_sign_message(const uint8_t *priv_key,
                         const uint8_t *pub_key,
                         const uint8_t *message,
                         uintptr_t len,
                         uint8_t *signature);

bool rsn_validate_message(const uint8_t (*pub_key)[32],
                          const uint8_t *message,
                          uintptr_t len,
                          const uint8_t (*signature)[64]);

} // extern "C"

} // namespace rsnano

#endif // rs_nano_bindings_hpp
