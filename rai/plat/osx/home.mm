#include <rai/home.hpp>

#include <Foundation/Foundation.h>

namespace rai
{
boost::filesystem::path home_path ()
{
	NSString * dir_string = NSHomeDirectory ();
	char const * dir_chars = [dir_string UTF8String];
	boost::filesystem::path result (dir_chars);
	[dir_string release];
	return result;
}
}