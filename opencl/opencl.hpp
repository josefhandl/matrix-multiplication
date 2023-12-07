
#include <iostream>
#include <vector>
#include <fstream>

#include "../tools.hpp"
#include "../benchmarked-object-float.hpp"

#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 300

#ifdef _WIN32
    // https://github.com/microsoft/cppwinrt/issues/479
    // https://stackoverflow.com/questions/1394132/macro-and-member-function-conflict
    #ifdef max
    #undef max
    #endif

    #include <CL/opencl.hpp>
#elif __APPLE__
    #include <OpenCL/opencl.hpp>
#elif __linux__
    //#include <CL/opencl.hpp>
    //#include <OpenCL/cl.hpp>
    //#include <OpenCL/opencl.hpp>
    #include <CL/cl2.hpp>
#endif


#define KERNEL_FILE "opencl/kernel.cl"
#define KERNEL_FUNCTION "vector_add"

class ModuleOpencl {

private:
    std::unique_ptr<BenchmarkedObjectFloat> bo;

    std::vector<cl::Device> cl_devices;

    int64_t make_opencl_benchmark(const cl::Device &device, unsigned matSize, unsigned testIter, const float *matA, const float *matB, float *matR) {
        // https://raw.githubusercontent.com/ULHPC/tutorials/devel/gpu/opencl/slides.pdf
        // https://programmerclick.com/article/47811146604/

        cl::Context context({device});
        cl::CommandQueue queue(context, device);

        std::ifstream kernelFile(KERNEL_FILE);
        std::string kernelCode((std::istreambuf_iterator<char>(kernelFile)), std::istreambuf_iterator<char>());

        cl::Program::Sources sources;
        sources.push_back({ kernelCode.c_str(),kernelCode.length() });

        cl::Program program(context, sources);
        if(program.build({device})!=CL_SUCCESS){
            std::cout<<" Error building: "<<program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device)<<"\n";
            exit(1);
        }

        cl::KernelFunctor<cl::Buffer, cl::Buffer, cl::Buffer, cl::Buffer> vector_add(cl::Kernel(program, KERNEL_FUNCTION));
        cl::NDRange ndRange(matSize);
        //cl::NDRange workGroupSize(256);

        auto s = std::chrono::high_resolution_clock::now();

        cl::Buffer cl_testIter(context, CL_MEM_READ_ONLY, sizeof(unsigned));
        cl::Buffer cl_matA(context, CL_MEM_READ_ONLY, sizeof(float) * matSize);
        cl::Buffer cl_matB(context, CL_MEM_READ_ONLY, sizeof(float) * matSize);
        cl::Buffer cl_matR(context, CL_MEM_WRITE_ONLY, sizeof(float) * matSize);

        queue.enqueueWriteBuffer(cl_testIter, CL_TRUE, 0, sizeof(unsigned), &testIter);
        queue.enqueueWriteBuffer(cl_matA, CL_TRUE, 0, sizeof(float) * matSize, matA);
        queue.enqueueWriteBuffer(cl_matB, CL_TRUE, 0, sizeof(float) * matSize, matB);

        //vector_add(cl::EnqueueArgs(queue, ndRange, workGroupSize), cl_testIter, cl_matA, cl_matB, cl_matR).wait();
        vector_add(cl::EnqueueArgs(queue, ndRange), cl_testIter, cl_matA, cl_matB, cl_matR).wait();

        queue.enqueueReadBuffer(cl_matR, CL_TRUE, 0, sizeof(float) * matSize, matR);

        auto e = std::chrono::high_resolution_clock::now();
        auto t = std::chrono::duration_cast<std::chrono::microseconds>(e - s);

        return t.count();
    }

public:
    void printInfo() {
        std::cout << "OpenCL info:" << std::endl;
        std::cout << "--------------------------------------" << std::endl;

        std::vector<cl::Platform> platformList;
        cl::Platform::get(&platformList);

        if (platformList.size() == 0) {
            std::cout << "No OpenCL platform available" << std::endl;
            return;
        }

        int deviceTotal = 0;
        for (const cl::Platform &platform : platformList) {
            std::cout << "Platform name: " << platform.getInfo<CL_PLATFORM_NAME>() << std::endl;

            std::vector<cl::Device> deviceList;
            platform.getDevices(CL_DEVICE_TYPE_ALL, &deviceList);

            if (deviceList.size() == 0) {
                std::cout << "No OpenCL device available for this platform" << std::endl;
            }

            deviceTotal += deviceList.size();

            std::cout << "Devices: " << deviceList.size() << std::endl;

            for (const cl::Device &device : deviceList) {
                cl_devices.push_back(device);

                std::cout << "  Device Type: ";
                switch (device.getInfo<CL_DEVICE_TYPE>()) {
                case CL_DEVICE_TYPE_GPU:         std::cout << "GPU"; break;
                case CL_DEVICE_TYPE_CPU:         std::cout << "CPU"; break;
                case CL_DEVICE_TYPE_ACCELERATOR: std::cout << "Accelerator"; break;
                default:                         std::cout << "Unknown"; break;
                }
                std::cout << std::endl;

                std::cout << "  Name: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
                std::cout << "  Vendor: " << device.getInfo<CL_DEVICE_VENDOR>() << std::endl;
                std::cout << "  HW version: " << device.getInfo<CL_DEVICE_VERSION>() << std::endl;
                std::cout << "  OpenCL C version: " << device.getInfo<CL_DEVICE_OPENCL_C_VERSION>() << std::endl;
                std::cout << "  Driver version: " << device.getInfo<CL_DRIVER_VERSION>() << std::endl;
                std::cout << "  Compute Units: " << device.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>() << std::endl;
                std::cout << "  Memory bus size: " << device.getInfo<CL_DEVICE_ADDRESS_BITS>() << std::endl;

                cl_ulong memSize = device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
                std::string memSizeStr;
                num_bin_prefix((int64_t)memSize, &memSizeStr);
                std::cout << "  Memory size: " << memSizeStr << std::endl;

                std::cout << std::endl;
            }
        }

        std::cout << "------" << std::endl;
        std::cout << "Platforms: " << platformList.size() << std::endl;
        std::cout << "Total number of devices: " << deviceTotal << std::endl;

        std::cout << std::endl;
    }

    void benchmark_prepare(unsigned size, unsigned iterations) {
        bo = std::make_unique<BenchmarkedObjectFloat>(size, iterations);
    }

    void benchmark() {
        std::cout << "OpenCL Benchmark:" << std::endl;
        std::cout << "--------------------------------------" << std::endl;

        std::string points;

        for (const cl::Device &device : cl_devices) {
            try {
                std::cout << device.getInfo<CL_DEVICE_NAME>() << ": ";

                int64_t time;
                for (int i = 0; i < 4; ++i) {
                    time = make_opencl_benchmark(device, bo->vectorSize, bo->iterations, bo->vecA, bo->vecB, bo->vecR);
                }
                compute_points(bo->vectorSize, bo->iterations, time, &points);

                bool benchmark_ok = test_benchmark(*bo);
                std::cout << (benchmark_ok ? points : "Invalid result") << std::endl;
            } catch (cl::Error e) {
                std::cout << "Failed" << std::endl;
            }
        }

        std::cout << std::endl;
    }
};
