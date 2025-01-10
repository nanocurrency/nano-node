#pragma once

#include <celerix/lib/config.hpp>
#include <celerix/lib/constants.hpp>
#include <celerix/node/openclconfig.hpp>
#include <celerix/node/xorshift.hpp>

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

namespace celerix
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
	std::vector<celerix::opencl_platform> platforms;
};

class root;
class work_pool;

class opencl_work
{
public:
	opencl_work (bool &, celerix::opencl_config const &, celerix::opencl_environment &, celerix::logger &, celerix::work_thresholds & work);
	~opencl_work ();
	boost::optional<uint64_t> generate_work (celerix::work_version const, celerix::root const &, uint64_t const);
	boost::optional<uint64_t> generate_work (celerix::work_version const, celerix::root const &, uint64_t const, std::atomic<int> &);
	static std::unique_ptr<opencl_work> create (bool, celerix::opencl_config const &, celerix::logger &, celerix::work_thresholds & work);
	celerix::opencl_config const & config;
	celerix::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_mem difficulty_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	celerix::xorshift1024star rand;
	celerix::logger & logger;
	celerix::work_thresholds & work;
};
}
