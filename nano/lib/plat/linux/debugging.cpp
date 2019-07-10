#include <nano/lib/utility.hpp>

#include <link.h>
namespace
{
// This creates a file for the load address of an executable or shared library.
// Useful for debugging should the virtual addresses be randomized.
int create_load_memory_address_file (dl_phdr_info * info, size_t, void *)
{
	static int counter = 0;
	std::ostringstream ss;
	ss << "nano_node_crash_load_address_dump_" << counter << ".txt";
	std::ofstream file (ss.str ());
	file << "Name: " << info->dlpi_name << "\n";

	for (auto i = 0; i < info->dlpi_phnum; ++i)
	{
		// Only care about the first load address
		if (info->dlpi_phdr[i].p_type == PT_LOAD)
		{
			file << std::hex << (void *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
			break;
		}
	}
	++counter;
	return 0;
}
}

void nano::create_load_memory_address_files ()
{
	dl_iterate_phdr (create_load_memory_address_file, nullptr);
}
