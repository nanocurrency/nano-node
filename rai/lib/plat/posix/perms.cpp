#include <rai/lib/utility.hpp>

#include <sys/stat.h>
#include <sys/types.h>

void rai::set_umask ()
{
	umask (077);
}
