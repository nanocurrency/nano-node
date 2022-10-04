#pragma once

#include <nano/node/ssl/ssl_classes.hpp>

namespace nano::ssl
{
bool is_ca_public_key_valid (const nano::ssl::BufferView & public_key);

}
