#include <nano/lib/utility.hpp>

#include <fcntl.h>
#include <link.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
// This creates a file for the load address of an executable or shared library.
// Useful for debugging should the virtual addresses be randomized.
int create_load_memory_address_file (dl_phdr_info * info, size_t, void *)
{
	static int counter = 0;
	assert (counter <= 99);
	// Create filename
	const char file_prefix[] = "nano_node_crash_load_address_dump_";
	// Holds the filename prefix, a unique (max 2 digits) number and extension (null terminator is included in file_prefix size)
	char filename[sizeof (file_prefix) + 2 + 4];
	snprintf (filename, sizeof (filename), "%s%d.txt", file_prefix, counter);

	// Open file
	const auto file_descriptor = ::open (filename, O_CREAT | O_WRONLY | O_TRUNC,
#if defined(S_IWRITE) && defined(S_IREAD)
	S_IWRITE | S_IREAD
#else
	0
#endif
	);

	// Write the name of shared library
	::write (file_descriptor, "Name: ", 6);
	::write (file_descriptor, info->dlpi_name, strlen (info->dlpi_name));
	::write (file_descriptor, "\n", 1);

	// Write the first load address found
	for (auto i = 0; i < info->dlpi_phnum; ++i)
	{
		if (info->dlpi_phdr[i].p_type == PT_LOAD)
		{
			auto load_address = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;

			// Each byte of the pointer is two hexadecimal characters, plus the 0x prefix and null terminator
			char load_address_as_hex_str[sizeof (load_address) * 2 + 2 + 1];
			snprintf (load_address_as_hex_str, sizeof (load_address_as_hex_str), "%p", (void *)load_address);
			::write (file_descriptor, load_address_as_hex_str, strlen (load_address_as_hex_str));
			break;
		}
	}

	::close (file_descriptor);
	++counter;
	return 0;
}
}

void nano::create_load_memory_address_files ()
{
	dl_iterate_phdr (create_load_memory_address_file, nullptr);
}
