#include <banano/node/working.hpp>

#include <Foundation/Foundation.h>

namespace rai
{
boost::filesystem::path app_path ()
{
	NSString * dir_string = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) lastObject];
	char const * dir_chars = [dir_string UTF8String];
	boost::filesystem::path result (dir_chars);
	[dir_string release];
	return result;
}
}