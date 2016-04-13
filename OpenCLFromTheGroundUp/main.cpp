#include <iostream>
#include <Windows.h>
#include <vector>
#include <algorithm>>
#include <string>
#include "CL/cl.h"
#include <malloc.h>
#include <stdio.h>

std::string GetDeviceName(cl_device_id id)
{
    size_t size = 0;
    clGetDeviceInfo(id, CL_DEVICE_NAME, 0, nullptr, &size);

    std::string result;
    result.resize(size);
    clGetDeviceInfo(id, CL_DEVICE_NAME, size,
        const_cast<char*> (result.data()), nullptr);

    return result;
}

int ReadSourceFromFile(const char* fileName, char** source, size_t* sourceSize)
{
    int errorCode = CL_SUCCESS;

    FILE* fp = NULL;
    fopen_s(&fp, fileName, "rb");
    if (fp == NULL)
    {
        errorCode = CL_INVALID_VALUE;
    }
    else {
        fseek(fp, 0, SEEK_END);
        *sourceSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        *source = new char[*sourceSize];
        if (*source == NULL)
        {
            errorCode = CL_OUT_OF_HOST_MEMORY;
        }
        else {
            fread(*source, 1, *sourceSize, fp);
        }
    }
    return errorCode;
}

int main()
{
    // how many platform ids do we need? 
    cl_uint numberOfPlatforms; 
    ::clGetPlatformIDs(0, NULL, &numberOfPlatforms); 

    std::cout << "There are " << numberOfPlatforms << " of platforms." << std::endl; 

    // actually get the platform ids
    std::vector<cl_platform_id> platforms(numberOfPlatforms); 
    ::clGetPlatformIDs(numberOfPlatforms, &platforms[0], NULL); 

    // what are the platform names? 
    cl_platform_id intelOpenClPlatform = nullptr; 
    std::string intelName("Intel(R) OpenCL"); 
    std::vector<cl_device_id> devices; 
    std::for_each(platforms.begin(), platforms.end(), [&](cl_platform_id& id)
    {
        size_t stringLength = 0;
        ::clGetPlatformInfo(id, CL_PLATFORM_NAME, 0, NULL, &stringLength);
        
        std::vector<char> nameBuffer(stringLength); 
        ::clGetPlatformInfo(id, CL_PLATFORM_NAME, stringLength, (void*)&nameBuffer[0], NULL);

        std::string strName(nameBuffer.begin(), nameBuffer.end()); 

        std::cout << strName << std::endl; 

        if (strstr(&nameBuffer[0], "Intel(R) OpenCL"))
        {
            intelOpenClPlatform = id; 

            cl_uint numberDevices = 0; 
            ::clGetDeviceIDs(intelOpenClPlatform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numberDevices);
            devices.resize(numberDevices); 

            ::clGetDeviceIDs(intelOpenClPlatform, CL_DEVICE_TYPE_GPU, numberDevices, &devices[0], NULL);

            std::cout << "Found Intel(R) OpenCL compatible platform. The devices are..." << std::endl; 

            std::for_each(devices.begin(), devices.end(), [&](cl_device_id& id)
            {
                std::cout << "\t" << GetDeviceName(id) << std::endl; 
            }); 
        }
    }); 

    if (intelOpenClPlatform)
    {
        // create the context. 
        const cl_context_properties props[] = 
        {
            CL_CONTEXT_PLATFORM,
            reinterpret_cast<cl_context_properties>(intelOpenClPlatform),
            0,0
        }; 

        cl_int error; 
        cl_context context = 
            ::clCreateContext(props, devices.size(), devices.data(), nullptr, nullptr, &error); 

        if (error == CL_SUCCESS)
        {
            std::cout << "Context created." << std::endl; 

            // create a command queue for this device. 
            cl_command_queue queue = ::clCreateCommandQueue(context, devices[0], NULL, &error); 

            if (error == CL_SUCCESS)
            {
                char* source = NULL; 
                size_t sourceSize; 
                ReadSourceFromFile("kernel.cl", &source, &sourceSize); 

                // create a program from source. 
                cl_program program = clCreateProgramWithSource(context, 1, (const char**)&source, &sourceSize, &error);

                if (error == CL_SUCCESS)
                {
                    std::cout << "Successfully created the program." << std::endl; 

                    const cl_device_id* constDevice = &devices[0]; 
                    error = ::clBuildProgram(program, 1, constDevice, "", NULL, NULL); 

                    if (error == CL_SUCCESS)
                    {
                        std::cout << "Successfully built the program." << std::endl; 

                        cl_kernel kernel = ::clCreateKernel(program, "Add", &error); 

                        if (error == CL_SUCCESS)
                        {
                            std::cout << "Successfully created the kernel." << std::endl; 

                            int minX = 277; 
                            int maxX = 427; 
                            int minY = 232; 
                            int maxY = 375; 

                            int width = (maxX - minX); 
                            int height = (maxY - minY); 

                            int bufferSize = width * height;

                            cl_mem outputBuffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
                                bufferSize, NULL, &error);

                            if (error == CL_SUCCESS)
                            {
                                std::cout << "Buffer created successfully." << std::endl; 

                                unsigned char* mask = (unsigned char*)_aligned_malloc(bufferSize, 4096);
                                memset(mask, 0, bufferSize);

                                cl_event clearCompleted;
                                clEnqueueWriteBuffer(queue, // the command queue
                                    outputBuffer, // the output buffer to which we write. 
                                    true, // make a blocking queue, 
                                    0, // no offset
                                    bufferSize, // size of the buffer,
                                    mask, // host memory buffer
                                    0, // no events to wait on 
                                    NULL, // no events to wait on
                                    &clearCompleted); // the event others will wait on

                                error = clWaitForEvents(1, &clearCompleted);

                                if (error == CL_SUCCESS)
                                {
                                    std::cout << "Buffer successfully enqueued for write." << std::endl; 

                                    error = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void*)&outputBuffer);
                                    error |= clSetKernelArg(kernel, 1, sizeof(minX), &minX); 
                                    error |= clSetKernelArg(kernel, 2, sizeof(maxX), &maxX);
                                    error |= clSetKernelArg(kernel, 3, sizeof(minY), &minY);
                                    error |= clSetKernelArg(kernel, 4, sizeof(maxY), &maxY);

                                    if(error == CL_SUCCESS)
                                    {
                                        std::cout << "Parameters set fine." << std::endl; 

                                        size_t workgroupDims[2]; 
                                        workgroupDims[0] = width; 
                                        workgroupDims[1] = height; 

                                        //https://www.khronos.org/registry/cl/sdk/1.0/docs/man/xhtml/clEnqueueNDRangeKernel.html
                                        error = clEnqueueNDRangeKernel(queue, // the queue
                                            kernel, // the kernel
                                            2, // the number of dimensions of this work-group
                                            NULL, // always NULL
                                            workgroupDims, // the dimensons of the work group
                                            NULL, // # of work-items in a work group. NULL = letting OpenCL figure it out. 
                                            0, 0, 0); // no events to wait on, nor are we waiting. 

                                        if (error == CL_SUCCESS)
                                        {
                                            std::cout << "We are executing...."; 
                                            clFinish(queue); // blocks until the queue has finished 
                                            std::cout << "...done";
                                        }
                                        else
                                        {
                                            std::cout << "We encountered an error." << std::endl; 
                                        }
                                    }
                                    else
                                    {
                                        std::cout << "Failed to set parameters." << std::endl; 
                                    }
                                }
                            }
                            else if(error == CL_INVALID_HOST_PTR)
                            {
                                std::cout << "Failed to create the buffer. Invalid host pointer." << std::endl; 
                            }
                        }
                    }
                }
            }
        }
    }

    getchar(); 
    
    return 0; 
}