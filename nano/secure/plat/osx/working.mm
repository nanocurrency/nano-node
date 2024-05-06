#include <nano/secure/working.hpp>

#include <Foundation/Foundation.h>

namespace nano
{
std::filesystem::path app_path_impl ()
{
	NSString * dir_string = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) lastObject];
	char const * dir_chars = [dir_string UTF8String];
	std::filesystem::path result (dir_chars);
	[dir_string release];
	return result;
}
}
