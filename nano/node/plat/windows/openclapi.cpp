#include <nano/node/openclwork.hpp>

#include <Windows.h>

namespace
{
class opencl_initializer
{
public:
	opencl_initializer ()
	{
		opencl_library = LoadLibrary ("OpenCL.dll");
		if (opencl_library != nullptr)
		{
			clGetPlatformIDs = reinterpret_cast<decltype (clGetPlatformIDs)> (GetProcAddress (opencl_library, "clGetPlatformIDs"));
			clGetPlatformInfo = reinterpret_cast<decltype (clGetPlatformInfo)> (GetProcAddress (opencl_library, "clGetPlatformInfo"));
			clGetDeviceIDs = reinterpret_cast<decltype (clGetDeviceIDs)> (GetProcAddress (opencl_library, "clGetDeviceIDs"));
			clGetDeviceInfo = reinterpret_cast<decltype (clGetDeviceInfo)> (GetProcAddress (opencl_library, "clGetDeviceInfo"));
			clCreateContext = reinterpret_cast<decltype (clCreateContext)> (GetProcAddress (opencl_library, "clCreateContext"));
			clCreateCommandQueue = reinterpret_cast<decltype (clCreateCommandQueue)> (GetProcAddress (opencl_library, "clCreateCommandQueue"));
			clCreateBuffer = reinterpret_cast<decltype (clCreateBuffer)> (GetProcAddress (opencl_library, "clCreateBuffer"));
			clCreateProgramWithSource = reinterpret_cast<decltype (clCreateProgramWithSource)> (GetProcAddress (opencl_library, "clCreateProgramWithSource"));
			clBuildProgram = reinterpret_cast<decltype (clBuildProgram)> (GetProcAddress (opencl_library, "clBuildProgram"));
			clGetProgramBuildInfo = reinterpret_cast<decltype (clGetProgramBuildInfo)> (GetProcAddress (opencl_library, "clGetProgramBuildInfo"));
			clCreateKernel = reinterpret_cast<decltype (clCreateKernel)> (GetProcAddress (opencl_library, "clCreateKernel"));
			clSetKernelArg = reinterpret_cast<decltype (clSetKernelArg)> (GetProcAddress (opencl_library, "clSetKernelArg"));
			clReleaseKernel = reinterpret_cast<decltype (clReleaseKernel)> (GetProcAddress (opencl_library, "clReleaseKernel"));
			clReleaseProgram = reinterpret_cast<decltype (clReleaseProgram)> (GetProcAddress (opencl_library, "clReleaseProgram"));
			clReleaseContext = reinterpret_cast<decltype (clReleaseContext)> (GetProcAddress (opencl_library, "clReleaseContext"));
			clEnqueueWriteBuffer = reinterpret_cast<decltype (clEnqueueWriteBuffer)> (GetProcAddress (opencl_library, "clEnqueueWriteBuffer"));
			clEnqueueNDRangeKernel = reinterpret_cast<decltype (clEnqueueNDRangeKernel)> (GetProcAddress (opencl_library, "clEnqueueNDRangeKernel"));
			clEnqueueReadBuffer = reinterpret_cast<decltype (clEnqueueReadBuffer)> (GetProcAddress (opencl_library, "clEnqueueReadBuffer"));
			clFinish = reinterpret_cast<decltype (clFinish)> (GetProcAddress (opencl_library, "clFinish"));
			nano::opencl_loaded = true;
		}
	}
	~opencl_initializer ()
	{
		if (opencl_library != nullptr)
		{
			nano::opencl_loaded = false;
			FreeLibrary (opencl_library);
		}
	}
	HMODULE opencl_library;
	cl_int (CL_API_CALL * clGetPlatformIDs) (cl_uint, cl_platform_id *, cl_uint *);
	cl_int (CL_API_CALL * clGetPlatformInfo) (cl_platform_id, cl_platform_info, size_t, void *, size_t *);
	cl_int (CL_API_CALL * clGetDeviceIDs) (cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
	cl_int (CL_API_CALL * clGetDeviceInfo) (cl_device_id, cl_device_info, size_t, void *, size_t *);
	cl_context (CL_API_CALL * clCreateContext) (cl_context_properties const *, cl_uint, cl_device_id const *, void (CL_CALLBACK *) (const char *, const void *, size_t, void *), void *, cl_int *);
	cl_command_queue (CL_API_CALL * clCreateCommandQueue) (cl_context, cl_device_id, cl_command_queue_properties, cl_int *);
	cl_mem (CL_API_CALL * clCreateBuffer) (cl_context, cl_mem_flags, size_t, void *, cl_int *);
	cl_program (CL_API_CALL * clCreateProgramWithSource) (cl_context, cl_uint, char const **, size_t const *, cl_int *);
	cl_int (CL_API_CALL * clBuildProgram) (cl_program, cl_uint, cl_device_id const *, char const *, void (CL_CALLBACK *) (cl_program, void *), void *);
	cl_int (CL_API_CALL * clGetProgramBuildInfo) (cl_program, cl_device_id, cl_program_build_info, size_t, void *, size_t *);
	cl_kernel (CL_API_CALL * clCreateKernel) (cl_program, char const *, cl_int *);
	cl_int (CL_API_CALL * clSetKernelArg) (cl_kernel, cl_uint, size_t, void const *);
	cl_int (CL_API_CALL * clReleaseKernel) (cl_kernel);
	cl_int (CL_API_CALL * clReleaseProgram) (cl_program);
	cl_int (CL_API_CALL * clReleaseContext) (cl_context);
	cl_int (CL_API_CALL * clEnqueueWriteBuffer) (cl_command_queue, cl_mem, cl_bool, size_t, size_t, void const *, cl_uint, cl_event const *, cl_event *);
	cl_int (CL_API_CALL * clEnqueueNDRangeKernel) (cl_command_queue, cl_kernel, cl_uint, size_t const *, size_t const *, size_t const *, cl_uint, cl_event const *, cl_event *);
	cl_int (CL_API_CALL * clEnqueueReadBuffer) (cl_command_queue, cl_mem, cl_bool, size_t, size_t, void *, cl_uint, cl_event const *, cl_event *);
	cl_int (CL_API_CALL * clFinish) (cl_command_queue);
	static opencl_initializer initializer;
};
}

opencl_initializer opencl_initializer::initializer;

cl_int CL_API_CALL clGetPlatformIDs (cl_uint num_entries, cl_platform_id * platforms, cl_uint * num_platforms)
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

cl_int CL_API_CALL clGetPlatformInfo (cl_platform_id platform, cl_platform_info param_name, size_t param_value_size, void * param_value, size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetPlatformInfo (platform, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_int CL_API_CALL clGetDeviceIDs (cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id * devices, cl_uint * num_devices)
{
	return opencl_initializer::initializer.clGetDeviceIDs (platform, device_type, num_entries, devices, num_devices);
}

cl_int CL_API_CALL clGetDeviceInfo (cl_device_id device, cl_device_info param_name, size_t param_value_size, void * param_value, size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetDeviceInfo (device, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_context CL_API_CALL clCreateContext (cl_context_properties const * properties, cl_uint num_devices, cl_device_id const * devices, void (CL_CALLBACK * pfn_notify) (const char *, const void *, size_t, void *), void * user_data, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateContext (properties, num_devices, devices, pfn_notify, user_data, errcode_ret);
}

cl_command_queue CL_API_CALL clCreateCommandQueue (cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateCommandQueue (context, device, properties, errcode_ret);
}

cl_mem CL_API_CALL clCreateBuffer (cl_context context, cl_mem_flags flags, size_t size, void * host_ptr, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateBuffer (context, flags, size, host_ptr, errcode_ret);
}

cl_program CL_API_CALL clCreateProgramWithSource (cl_context context, cl_uint count, char const ** strings, size_t const * lengths, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateProgramWithSource (context, count, strings, lengths, errcode_ret);
}

cl_int CL_API_CALL clBuildProgram (cl_program program, cl_uint num_devices, cl_device_id const * device_list, char const * options, void (CL_CALLBACK * pfn_notify) (cl_program, void *), void * user_data)
{
	return opencl_initializer::initializer.clBuildProgram (program, num_devices, device_list, options, pfn_notify, user_data);
}

cl_int CL_API_CALL clGetProgramBuildInfo (cl_program program, cl_device_id device, cl_program_build_info param_name, size_t param_value_size, void * param_value, size_t * param_value_size_ret)
{
	return opencl_initializer::initializer.clGetProgramBuildInfo (program, device, param_name, param_value_size, param_value, param_value_size_ret);
}

cl_kernel CL_API_CALL clCreateKernel (cl_program program, char const * kernel_name, cl_int * errcode_ret)
{
	return opencl_initializer::initializer.clCreateKernel (program, kernel_name, errcode_ret);
}

cl_int CL_API_CALL clSetKernelArg (cl_kernel kernel, cl_uint arg_index, size_t arg_size, void const * arg_value)
{
	return opencl_initializer::initializer.clSetKernelArg (kernel, arg_index, arg_size, arg_value);
}

cl_int CL_API_CALL clReleaseKernel (cl_kernel kernel)
{
	return opencl_initializer::initializer.clReleaseKernel (kernel);
}

cl_int CL_API_CALL clReleaseProgram (cl_program program)
{
	return opencl_initializer::initializer.clReleaseProgram (program);
}

cl_int CL_API_CALL clReleaseContext (cl_context context)
{
	return opencl_initializer::initializer.clReleaseContext (context);
}

cl_int CL_API_CALL clEnqueueWriteBuffer (cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write, size_t offset, size_t size, void const * ptr, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueWriteBuffer (command_queue, buffer, blocking_write, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

cl_int CL_API_CALL clEnqueueNDRangeKernel (cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim, size_t const * global_work_offset, size_t const * global_work_size, size_t const * local_work_size, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueNDRangeKernel (command_queue, kernel, work_dim, global_work_offset, global_work_size, local_work_size, num_events_in_wait_list, event_wait_list, event);
}

cl_int CL_API_CALL clEnqueueReadBuffer (cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read, size_t offset, size_t size, void * ptr, cl_uint num_events_in_wait_list, cl_event const * event_wait_list, cl_event * event)
{
	return opencl_initializer::initializer.clEnqueueReadBuffer (command_queue, buffer, blocking_read, offset, size, ptr, num_events_in_wait_list, event_wait_list, event);
}

cl_int CL_API_CALL clFinish (cl_command_queue command_queue)
{
	return opencl_initializer::initializer.clFinish (command_queue);
}
