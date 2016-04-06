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
	cl_int (* clGetPlatformInfo) (cl_platform_id, cl_platform_info, size_t, void *, size_t *);
	cl_int (* clGetDeviceIDs) (cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
	cl_int (* clGetDeviceInfo) (cl_device_id, cl_device_info, size_t, void *, size_t *);
	cl_context (* clCreateContext) (cl_context_properties const *, cl_uint, cl_device_id const *, void (*)(const char *, const void *, size_t, void *), void *, cl_int *);
	cl_command_queue (* clCreateCommandQueue) (cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
	cl_mem (* clCreateBuffer) (cl_context, cl_mem_flags, size_t, void *, cl_int *);
	cl_program (* clCreateProgramWithSource) (cl_context, cl_uint, char const **, size_t const *, cl_int *);
	cl_int (* clBuildProgram) (cl_program, cl_uint, cl_device_id const *, char const *, void (*)(cl_program, void *), void *);
	cl_kernel (* clCreateKernel) (cl_program, char const *, cl_int *);
	cl_int (* clSetKernelArg) (cl_kernel, cl_uint, size_t, void const *);
	cl_int (* clReleaseKernel) (cl_kernel);
	cl_int (* clReleaseProgram) (cl_program);
	cl_int (* clReleaseContext) (cl_context);
	cl_int (* clEnqueueWriteBuffer) (cl_command_queue, cl_mem, cl_bool, size_t, size_t, void const *, cl_uint, cl_event const *, cl_event *);
	cl_int (* clEnqueueNDRangeKernel) (cl_command_queue, cl_kernel, cl_uint, size_t const *, size_t const *, size_t const *, cl_uint, cl_event const *, cl_event *);
	cl_int (* clEnqueueReadBuffer) (cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *, cl_uint, cl_event const *, cl_event *);
	cl_int (* clFinish) (cl_command_queue);
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
		*num_platforms = 0;
	}
	return result;
}

cl_int clGetPlatformInfo (cl_platform_id platform, cl_platform_info param_name, size_t param_value_size, void * param_value, size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetPlatformInfo (platform, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_int clGetDeviceIDs (cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id * devices, cl_uint * num_devices)
{
	return opencl_initializer::initializer.clGetDeviceIDs (platform, device_type, num_entries, devices, num_devices);
}

cl_int clGetDeviceInfo (cl_device_id device, cl_device_info param_name, size_t param_value_size, void * param_value, size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetDeviceInfo (device, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_context clCreateContext (cl_context_properties const * properties, cl_uint num_devices, cl_device_id const * devices, void (* pfn_notify)(const char *, const void *, size_t, void *), void * user_data, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateContext (properties, num_devices, devices, pfn_notify, user_data, errcode_ret);
}

cl_command_queue clCreateCommandQueue (cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateCommandQueue (context, device, properties, errcode_ret);
}

cl_mem clCreateBuffer (cl_context context, cl_mem_flags flags, size_t size, void * host_ptr, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateBuffer (context, flags, size, host_ptr, errcode_ret);
}

cl_program clCreateProgramWithSource (cl_context context, cl_uint count, char const ** strings, size_t const * lengths, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateProgramWithSource (context, count, strings, lengths, errcode_ret);
}

cl_int clBuildProgram (cl_program program, cl_uint num_devices, cl_device_id const * device_list, char const * options, void (*pfn_notify) (cl_program , void *), void * user_data)
{
	return opencl_initializer::initializer.clBuildProgram (program, num_devices, device_list, options, pfn_notify, user_data);
}

cl_kernel clCreateKernel (cl_program program, char const * kernel_name, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateKernel (program, kernel_name, errcode_ret);
}

cl_int clSetKernelArg (cl_kernel kernel, cl_uint arg_index, size_t arg_size, void const * arg_value)
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

cl_int clEnqueueWriteBuffer (cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write, size_t offset, size_t size, void const * ptr, cl_uint num_events_in_wait_list, cl_event const * event_wait_list,cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueWriteBuffer (command_queue, buffer, blocking_write, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

cl_int clEnqueueNDRangeKernel (cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim, size_t const * global_work_offset, size_t const * global_work_size, size_t const * local_work_size, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueNDRangeKernel (command_queue, kernel, work_dim, global_work_offset, global_work_size, local_work_size, num_events_in_wait_list, event_wait_list, event);
}

cl_int clEnqueueReadBuffer (cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read, size_t offset, size_t size, void * ptr, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event *event)
{
	return opencl_initializer::initializer.clEnqueueReadBuffer (command_queue, buffer, blocking_read, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

cl_int clFinish (cl_command_queue command_queue)
{
	return opencl_initializer::initializer.clFinish (command_queue);
}
