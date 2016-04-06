#include <rai/node/openclwork.hpp>

#include <dlfcn.h>

namespace
{
class opencl_initializer
{
public:
	opencl_initializer ()
	{
		opencl_library = dlopen ("libOpenCL.so", RTLD_NOW);
		if (opencl_library != nullptr)
		{
			clGetPlatformIDs = reinterpret_cast <decltype (clGetPlatformIDs)> (dlsym (opencl_library, "clGetPlatformIDs"));
		}
	}
	~opencl_initializer ()
	{
		dlclose (opencl_library);
	}
	void * opencl_library;
	cl_int (* clGetPlatformIDs) (cl_uint, cl_platform_id *, cl_uint *);
	cl_int (* clGetDeviceIDs) (cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
	static opencl_initializer initializer;
};
}

cl_int clGetPlatformIDs (cl_uint num_entries, cl_platform_id * platforms, cl_uint * num_platforms)
{
	cl_int result;
	if (opencl_initializer::initializer.opencl_library != nullptr)
	{
		result = opencl_initializer::initializer.clGetPlatformIDs (num_entries, platforms, num_platforms);
	}
	else
	{
		result = CL_SUCCESS;
		*num_platforms = 0;
	}
	return result;
}

cl_int clGetDeviceIDs (cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id * devices, cl_uint * num_devices)
{
	return clGetDeviceIDs (platform, device_type, num_entries, devices, num_devices);
}