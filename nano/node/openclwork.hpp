#pragma once

#include <nano/lib/work.hpp>
#include <nano/node/openclconfig.hpp>
#include <nano/node/xorshift.hpp>

#include <boost/optional.hpp>

#include <atomic>
#include <mutex>
#include <vector>

#ifdef __APPLE__
#define CL_SILENCE_DEPRECATION
#include <OpenCL/opencl.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#endif

namespace nano
{
extern bool opencl_loaded;
class logger;
class opencl_platform
{
public:
	cl_platform_id platform;
	std::vector<cl_device_id> devices;
};
class opencl_environment
{
public:
	opencl_environment (bool &);
	void dump (std::ostream & stream);
	std::vector<nano::opencl_platform> platforms;
};
class root;
class work_pool;
class opencl_work
{
public:
	opencl_work (bool &, nano::opencl_config const &, nano::opencl_environment &, nano::logger &, nano::work_thresholds & work);
	~opencl_work ();
	boost::optional<uint64_t> generate_work (nano::work_version const, nano::root const &, uint64_t const);
	boost::optional<uint64_t> generate_work (nano::work_version const, nano::root const &, uint64_t const, std::atomic<int> &);
	static std::unique_ptr<opencl_work> create (bool, nano::opencl_config const &, nano::logger &, nano::work_thresholds & work);
	nano::opencl_config const & config;
	nano::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_mem difficulty_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	nano::xorshift1024star rand;
	nano::logger & logger;
	nano::work_thresholds & work;
};
}
