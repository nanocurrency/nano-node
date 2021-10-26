#include <nano/node/openclwork.hpp>

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
			clGetPlatformIDs = reinterpret_cast<decltype (clGetPlatformIDs)> (dlsym (opencl_library, "clGetPlatformIDs"));
			clGetPlatformInfo = reinterpret_cast<decltype (clGetPlatformInfo)> (dlsym (opencl_library, "clGetPlatformInfo"));
			clGetDeviceIDs = reinterpret_cast<decltype (clGetDeviceIDs)> (dlsym (opencl_library, "clGetDeviceIDs"));
			clGetDeviceInfo = reinterpret_cast<decltype (clGetDeviceInfo)> (dlsym (opencl_library, "clGetDeviceInfo"));
			clCreateContext = reinterpret_cast<decltype (clCreateContext)> (dlsym (opencl_library, "clCreateContext"));
			clCreateCommandQueue = reinterpret_cast<decltype (clCreateCommandQueue)> (dlsym (opencl_library, "clCreateCommandQueue"));
			clCreateBuffer = reinterpret_cast<decltype (clCreateBuffer)> (dlsym (opencl_library, "clCreateBuffer"));
			clCreateProgramWithSource = reinterpret_cast<decltype (clCreateProgramWithSource)> (dlsym (opencl_library, "clCreateProgramWithSource"));
			clBuildProgram = reinterpret_cast<decltype (clBuildProgram)> (dlsym (opencl_library, "clBuildProgram"));
			clGetProgramBuildInfo = reinterpret_cast<decltype (clGetProgramBuildInfo)> (dlsym (opencl_library, "clGetProgramBuildInfo"));
			clCreateKernel = reinterpret_cast<decltype (clCreateKernel)> (dlsym (opencl_library, "clCreateKernel"));
			clSetKernelArg = reinterpret_cast<decltype (clSetKernelArg)> (dlsym (opencl_library, "clSetKernelArg"));
			clReleaseKernel = reinterpret_cast<decltype (clReleaseKernel)> (dlsym (opencl_library, "clReleaseKernel"));
			clReleaseProgram = reinterpret_cast<decltype (clReleaseProgram)> (dlsym (opencl_library, "clReleaseProgram"));
			clReleaseContext = reinterpret_cast<decltype (clReleaseContext)> (dlsym (opencl_library, "clReleaseContext"));
			clEnqueueWriteBuffer = reinterpret_cast<decltype (clEnqueueWriteBuffer)> (dlsym (opencl_library, "clEnqueueWriteBuffer"));
			clEnqueueNDRangeKernel = reinterpret_cast<decltype (clEnqueueNDRangeKernel)> (dlsym (opencl_library, "clEnqueueNDRangeKernel"));
			clEnqueueReadBuffer = reinterpret_cast<decltype (clEnqueueReadBuffer)> (dlsym (opencl_library, "clEnqueueReadBuffer"));
			clFinish = reinterpret_cast<decltype (clFinish)> (dlsym (opencl_library, "clFinish"));
			nano::opencl_loaded = true;
		}
	}
	~opencl_initializer ()
	{
		if (opencl_library != nullptr)
		{
			nano::opencl_loaded = false;
			dlclose (opencl_library);
		}
	}
	void * opencl_library;
	cl_int (*clGetPlatformIDs) (cl_uint, cl_platform_id *, cl_uint *);
	cl_int (*clGetPlatformInfo) (cl_platform_id, cl_platform_info, std::size_t, void *, std::size_t *);
	cl_int (*clGetDeviceIDs) (cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
	cl_int (*clGetDeviceInfo) (cl_device_id, cl_device_info, std::size_t, void *, std::size_t *);
	cl_context (*clCreateContext) (cl_context_properties const *, cl_uint, cl_device_id const *, void (*) (char const *, const void *, std::size_t, void *), void *, cl_int *);
	cl_command_queue (*clCreateCommandQueue) (cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
	cl_mem (*clCreateBuffer) (cl_context, cl_mem_flags, std::size_t, void *, cl_int *);
	cl_program (*clCreateProgramWithSource) (cl_context, cl_uint, char const **, std::size_t const *, cl_int *);
	cl_int (*clBuildProgram) (cl_program, cl_uint, cl_device_id const *, char const *, void (*) (cl_program, void *), void *);
	cl_int (*clGetProgramBuildInfo) (cl_program, cl_device_id, cl_program_build_info, std::size_t, void *, std::size_t *);
	cl_kernel (*clCreateKernel) (cl_program, char const *, cl_int *);
	cl_int (*clSetKernelArg) (cl_kernel, cl_uint, std::size_t, void const *);
	cl_int (*clReleaseKernel) (cl_kernel);
	cl_int (*clReleaseProgram) (cl_program);
	cl_int (*clReleaseContext) (cl_context);
	cl_int (*clEnqueueWriteBuffer) (cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t, void const *, cl_uint, cl_event const *, cl_event *);
	cl_int (*clEnqueueNDRangeKernel) (cl_command_queue, cl_kernel, cl_uint, std::size_t const *, std::size_t const *, std::size_t const *, cl_uint, cl_event const *, cl_event *);
	cl_int (*clEnqueueReadBuffer) (cl_command_queue, cl_mem, cl_bool, std::size_t, std::size_t, void *, cl_uint, cl_event const *, cl_event *);
	cl_int (*clFinish) (cl_command_queue);
	static opencl_initializer initializer;
};
}

opencl_initializer opencl_initializer::initializer;

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
		if (num_platforms != nullptr)
		{
			*num_platforms = 0;
		}
	}
	return result;
}

cl_int clGetPlatformInfo (cl_platform_id platform, cl_platform_info param_name, std::size_t param_value_size, void * param_value, std::size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetPlatformInfo (platform, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_int clGetDeviceIDs (cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id * devices, cl_uint * num_devices)
{
	return opencl_initializer::initializer.clGetDeviceIDs (platform, device_type, num_entries, devices, num_devices);
}

cl_int clGetDeviceInfo (cl_device_id device, cl_device_info param_name, std::size_t param_value_size, void * param_value, std::size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetDeviceInfo (device, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_context clCreateContext (cl_context_properties const * properties, cl_uint num_devices, cl_device_id const * devices, void (*pfn_notify) (char const *, const void *, std::size_t, void *), void * user_data, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateContext (properties, num_devices, devices, pfn_notify, user_data, errcode_ret);
}

cl_command_queue clCreateCommandQueue (cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateCommandQueue (context, device, properties, errcode_ret);
}

cl_mem clCreateBuffer (cl_context context, cl_mem_flags flags, std::size_t size, void * host_ptr, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateBuffer (context, flags, size, host_ptr, errcode_ret);
}

cl_program clCreateProgramWithSource (cl_context context, cl_uint count, char const ** strings, std::size_t const * lengths, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateProgramWithSource (context, count, strings, lengths, errcode_ret);
}

cl_int clBuildProgram (cl_program program, cl_uint num_devices, cl_device_id const * device_list, char const * options, void (*pfn_notify) (cl_program, void *), void * user_data)
{
	return opencl_initializer::initializer.clBuildProgram (program, num_devices, device_list, options, pfn_notify, user_data);
}

cl_int clGetProgramBuildInfo (cl_program program, cl_device_id device, cl_program_build_info param_name, std::size_t param_value_size, void * param_value, std::size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetProgramBuildInfo (program, device, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_kernel clCreateKernel (cl_program program, char const * kernel_name, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateKernel (program, kernel_name, errcode_ret);
}

cl_int clSetKernelArg (cl_kernel kernel, cl_uint arg_index, std::size_t arg_size, void const * arg_value)
{
	return opencl_initializer::initializer.clSetKernelArg (kernel, arg_index, arg_size, arg_value);
}

cl_int clReleaseKernel (cl_kernel kernel)
{
	return opencl_initializer::initializer.clReleaseKernel (kernel);
}

cl_int clReleaseProgram (cl_program program)
{
	return opencl_initializer::initializer.clReleaseProgram (program);
}

cl_int clReleaseContext (cl_context context)
{
	return opencl_initializer::initializer.clReleaseContext (context);
}

cl_int clEnqueueWriteBuffer (cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write, std::size_t offset, std::size_t size, void const * ptr, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueWriteBuffer (command_queue, buffer, blocking_write, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

cl_int clEnqueueNDRangeKernel (cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim, std::size_t const * global_work_offset, std::size_t const * global_work_size, std::size_t const * local_work_size, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueNDRangeKernel (command_queue, kernel, work_dim, global_work_offset, global_work_size, local_work_size, num_events_in_wait_list, event_wait_list, event);
}

cl_int clEnqueueReadBuffer (cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read, std::size_t offset, std::size_t size, void * ptr, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueReadBuffer (command_queue, buffer, blocking_read, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

cl_int clFinish (cl_command_queue command_queue)
{
	return opencl_initializer::initializer.clFinish (command_queue);
}
