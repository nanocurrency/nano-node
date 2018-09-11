#include <rai/lib/utility.hpp>

#include <sys/types.h>
#include <sys/stat.h>

void rai::set_umask ()
{
	umask (077);
}
