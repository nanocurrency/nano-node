#pragma once

#include <string>

namespace nano::store::lmdb
{
bool success (int status);
bool not_found (int status);
std::string error_string (int status);
}
