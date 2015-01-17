#include <rai/working.hpp>

#include <Foundation/Foundation.h>

namespace rai
{
boost::filesystem::path working_path ()
{
	NSString * dir_string = NSHomeDirectory ();
	char const * dir_chars = [dir_string UTF8String];
	boost::filesystem::path result (dir_chars);
	[dir_string release];
	result /= "RaiBlocks";
	return result;
}
}