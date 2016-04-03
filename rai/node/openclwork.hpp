#pragma once

#include <rai/node/xorshift.hpp>

#include <map>
#include <mutex>
#include <vector>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>	
#endif

namespace rai
{
class opencl_environment
{
public:
	opencl_environment (bool &);
	void dump ();
	std::map <cl_platform_id, std::vector <cl_device_id>> devices;
};
union uint256_union;
class work_pool;
class opencl_work
{
public:
	opencl_work (bool &, rai::opencl_environment &, rai::work_pool &);
	~opencl_work ();
	uint64_t generate_work (rai::uint256_union const &);
	std::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	rai::xorshift1024star rand;
	rai::work_pool & pool;
};
}