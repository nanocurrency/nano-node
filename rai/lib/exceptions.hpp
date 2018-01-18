#pragma once

#include <stdexcept>
#include <string>

namespace rai
{
/**
 * Thrown if there's a problem with parsing the configuration file.
 * At a minimum, this exception should be caught in user facing
 * code, such as the CLI, with proper error reporting to the user.
 */
class config_error : public std::runtime_error
{
public:
	config_error (std::string msg) :
	std::runtime_error (msg)
	{
	}
};
}
