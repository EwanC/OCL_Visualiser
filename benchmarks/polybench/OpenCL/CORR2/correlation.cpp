/**
 * correlation.c: This file is part of the PolyBench/GPU 1.0 test suite.
 *
 *
 * Contact: Scott Grauer-Gray <sgrauerg@gmail.com>
 * Louis-Noel Pouchet <pouchet@cse.ohio-state.edu>
 * Web address: http://www.cse.ohio-state.edu/~pouchet/software/polybench/GPU
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <math.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include "Buffer.h"
#include "Event.h"
#include "Kernel.h"
#include "Program.h"
#include "Queue.h"
#include "Device.h"
#include "Platform.h"

#include "bench_support.h"
#include "MathUtils.h"
#include "SystemConfig.h"
#include "SystemConfiguration.h"
#include "Utils.h"

//define the error threshold for the results "not matching"

#define MAX_SOURCE_SIZE (0x100000)

/* Problem size */
#define M 256 //2048
#define N 256 //2048

/* Thread block dimensions for kernel 1*/
#define DIM_LOCAL_WORK_GROUP_KERNEL_1_X 256
#define DIM_LOCAL_WORK_GROUP_KERNEL_1_Y 1

/* Thread block dimensions for kernel 2*/
#define DIM_LOCAL_WORK_GROUP_KERNEL_2_X 256
#define DIM_LOCAL_WORK_GROUP_KERNEL_2_Y 1

/* Thread block dimensions for kernel 3*/
#define DIM_LOCAL_WORK_GROUP_KERNEL_3_X 32
#define DIM_LOCAL_WORK_GROUP_KERNEL_3_Y 8

/* Thread block dimensions for kernel 4*/
#define DIM_LOCAL_WORK_GROUP_KERNEL_4_X 256
#define DIM_LOCAL_WORK_GROUP_KERNEL_4_Y 1

#define sqrt_of_array_cell(x, j) sqrt(x[j])

#if defined(cl_khr_fp64) // Khronos extension available?
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#elif defined(cl_amd_fp64) // AMD extension available?
#pragma OPENCL EXTENSION cl_amd_fp64 : enable
#endif

#define KERNEL_FILE_NAME "correlation.cl"
#define PLATFORM_ID 0
#define DEVICE_ID 0

/* Can switch DATA_TYPE between float and double */
typedef float DATA_TYPE;

#define FLOAT_N 3214212.01
#define EPS 0.005

cl_int errcode;
Platform* platform;
Context* context;

Kernel* kernel_mean;
Kernel* kernel_std;
Kernel* kernel_reduce;
Kernel* kernel_corr;

Buffer* data_mem_obj;
Buffer* stddev_mem_obj;
Buffer* mean_mem_obj;
Buffer* symmat_mem_obj;

std::string meanKernelName = "mean_kernel";
std::string stdKernelName = "std_kernel";
std::string reduceKernelName = "reduce_kernel";
std::string corrKernelName = "corr_kernel";

void compareResults(DATA_TYPE *symmat, DATA_TYPE *symmat_outputFromGpu) {
  unsigned int i, j, fail;
  fail = 0;

  for (i = 0; i <= M; i++) {
    for (j = 0; j <= N; j++) {
      if (percentDiff(symmat[i * (N + 1) + j],
                      symmat_outputFromGpu[i * (N + 1) + j]) >
          PERCENT_DIFF_ERROR_THRESHOLD) {
        fail++;
      }
    }
  }
  assert(fail == 0 && "CPU - GPU Computation does not match!");
  std::cout << "Ok!\n";                                         
}



void init_arrays(DATA_TYPE *data) {
  unsigned int i, j;

  for (i = 0; i <= M; i++) {
    for (j = 0; j <= N; j++) {
      data[i * N + j] = ((DATA_TYPE)i * j) / (M + 1);
    }
  }
}

void cl_mem_init(DATA_TYPE *data, DATA_TYPE *mean, DATA_TYPE *stddev,
                 DATA_TYPE *symmat,Queue& queue) {

  data_mem_obj = new Buffer(*(platform->getContext()),Buffer::ReadWrite ,sizeof(DATA_TYPE) * (M + 1) * (N + 1), NULL);

  symmat_mem_obj = new Buffer(*(platform->getContext()), Buffer::ReadWrite,sizeof(DATA_TYPE) * (M + 1) * (N + 1), NULL);
  stddev_mem_obj = new Buffer(*(platform->getContext()),Buffer::ReadWrite,sizeof(DATA_TYPE) * (M + 1), NULL);
  mean_mem_obj = new Buffer(*(platform->getContext()),Buffer::ReadWrite ,sizeof(DATA_TYPE) * (M + 1), NULL);


  queue.writeBuffer(*data_mem_obj, sizeof(DATA_TYPE) * (M + 1) * (N + 1), data);
  queue.writeBuffer(*symmat_mem_obj,sizeof(DATA_TYPE) * (M + 1) * (N + 1), symmat);
  queue.writeBuffer(*stddev_mem_obj, sizeof(DATA_TYPE) * (M + 1), stddev);
  queue.writeBuffer(*mean_mem_obj,sizeof(DATA_TYPE) * (M + 1), mean);
  queue.finish();
}


void cl_launch_kernel(Queue& queue) {
  int m = M;
  int n = N;

  DATA_TYPE float_n = FLOAT_N;
  DATA_TYPE eps = EPS;

  size_t localWorkSize_Kernel1[2], globalWorkSize_Kernel1[2];
  size_t localWorkSize_Kernel2[2], globalWorkSize_Kernel2[2];
  size_t localWorkSize_Kernel3[2], globalWorkSize_Kernel3[2];
  size_t localWorkSize_Kernel4[2], globalWorkSize_Kernel4[2];

  localWorkSize_Kernel1[0] = DIM_LOCAL_WORK_GROUP_KERNEL_1_X;
  localWorkSize_Kernel1[1] = DIM_LOCAL_WORK_GROUP_KERNEL_1_Y;
  globalWorkSize_Kernel1[0] =
      (size_t)ceil(((float)M) / ((float)DIM_LOCAL_WORK_GROUP_KERNEL_1_X)) *
      DIM_LOCAL_WORK_GROUP_KERNEL_1_X;
  globalWorkSize_Kernel1[1] = 1;

  localWorkSize_Kernel2[0] = DIM_LOCAL_WORK_GROUP_KERNEL_2_X;
  localWorkSize_Kernel2[1] = DIM_LOCAL_WORK_GROUP_KERNEL_2_Y;
  globalWorkSize_Kernel2[0] =
      (size_t)ceil(((float)M) / ((float)DIM_LOCAL_WORK_GROUP_KERNEL_2_X)) *
      DIM_LOCAL_WORK_GROUP_KERNEL_2_X;
  globalWorkSize_Kernel2[1] = 1;

  localWorkSize_Kernel3[0] = DIM_LOCAL_WORK_GROUP_KERNEL_3_X;
  localWorkSize_Kernel3[1] = DIM_LOCAL_WORK_GROUP_KERNEL_3_Y;
  globalWorkSize_Kernel3[0] =
      (size_t)ceil(((float)M) / ((float)DIM_LOCAL_WORK_GROUP_KERNEL_3_X)) *
      DIM_LOCAL_WORK_GROUP_KERNEL_3_X;
  globalWorkSize_Kernel3[1] =
      (size_t)ceil(((float)N) / ((float)DIM_LOCAL_WORK_GROUP_KERNEL_3_Y)) *
      DIM_LOCAL_WORK_GROUP_KERNEL_3_Y;

  localWorkSize_Kernel4[0] = DIM_LOCAL_WORK_GROUP_KERNEL_4_X;
  localWorkSize_Kernel4[1] = DIM_LOCAL_WORK_GROUP_KERNEL_4_Y;
  globalWorkSize_Kernel4[0] =
      (size_t)ceil(((float)M) / ((float)DIM_LOCAL_WORK_GROUP_KERNEL_4_X)) *
      DIM_LOCAL_WORK_GROUP_KERNEL_4_X;
  globalWorkSize_Kernel4[1] = 1;

  // Set the arguments of the kernel
 
  kernel_mean->setArgument( 0,*mean_mem_obj);
  kernel_mean->setArgument( 1,*data_mem_obj);
  kernel_mean->setArgument( 2, sizeof(DATA_TYPE), (void *)&float_n);
  kernel_mean->setArgument( 3, sizeof(int), (void *)&m);
  kernel_mean->setArgument( 4, sizeof(int), (void *)&n);

  // Execute the OpenCL kernel
  queue.run(*kernel_mean, 1, 0,globalWorkSize_Kernel1,localWorkSize_Kernel1);
  
  // Set the arguments of the kernel
  kernel_std->setArgument( 0,*mean_mem_obj);
  kernel_std->setArgument( 1,*stddev_mem_obj);
  kernel_std->setArgument( 2,*data_mem_obj);
  kernel_std->setArgument( 3, sizeof(DATA_TYPE), (void *)&float_n);
  kernel_std->setArgument( 4, sizeof(DATA_TYPE), (void *)&eps);
  kernel_std->setArgument( 5, sizeof(int), (void *)&m);
  kernel_std->setArgument( 6, sizeof(int), (void *)&n);

  // Execute the OpenCL kernel
  queue.run(*kernel_std, 1,0,globalWorkSize_Kernel2,localWorkSize_Kernel2);
  

  // Set the arguments of the kernel
  kernel_reduce->setArgument( 0,*mean_mem_obj);
  kernel_reduce->setArgument( 1,*stddev_mem_obj);
  kernel_reduce->setArgument( 2,*data_mem_obj);
  kernel_reduce->setArgument( 3, sizeof(DATA_TYPE), (void *)&float_n);
  kernel_reduce->setArgument( 4, sizeof(int), (void *)&m);
  kernel_reduce->setArgument( 5, sizeof(int), (void *)&n);

  // Execute the OpenCL kernel
  queue.run(*kernel_reduce, 2,0,globalWorkSize_Kernel3,localWorkSize_Kernel3);
  

  // Set the arguments of the kernel
  kernel_corr->setArgument( 0,*symmat_mem_obj);
  kernel_corr->setArgument( 1,*data_mem_obj);
  kernel_corr->setArgument( 2, sizeof(int), (void *)&m);
  kernel_corr->setArgument( 3, sizeof(int), (void *)&n);

  // Execute the OpenCL kernel
  queue.run(*kernel_corr, 1, 0,globalWorkSize_Kernel4,localWorkSize_Kernel4);
  
  DATA_TYPE val = 1.0;
  queue.writeBuffer(*symmat_mem_obj,((M) * (M + 1) + (M)) * sizeof(DATA_TYPE),sizeof(DATA_TYPE), &val);

  queue.finish();
}

void cl_clean_up() {
  // Clean up
  delete platform;
  delete kernel_reduce;
  delete kernel_mean;
  delete kernel_std;
  delete kernel_corr;
  delete symmat_mem_obj;
  delete data_mem_obj;
  delete mean_mem_obj;
  delete stddev_mem_obj;
}

void correlation(DATA_TYPE *data, DATA_TYPE *mean, DATA_TYPE *stddev,
                 DATA_TYPE *symmat) {
  unsigned int i, j, j1, j2;

  // Determine mean of column vectors of input data matrix
  for (j = 1; j <= M; j++) {
    mean[j] = 0.0;

    for (i = 1; i <= N; i++) {
      mean[j] += data[i * (M + 1) + j];
    }

    mean[j] /= (DATA_TYPE)FLOAT_N;
  }

  // Determine standard deviations of column vectors of data matrix.
  for (j = 1; j <= M; j++) {
    stddev[j] = 0.0;

    for (i = 1; i <= N; i++) {
      stddev[j] +=
          (data[i * (M + 1) + j] - mean[j]) * (data[i * (M + 1) + j] - mean[j]);
    }

    stddev[j] /= FLOAT_N;
    stddev[j] = sqrt_of_array_cell(stddev, j);
    stddev[j] = stddev[j] <= EPS ? 1.0 : stddev[j];
  }

  // Center and reduce the column vectors.
  for (i = 1; i <= N; i++) {
    for (j = 1; j <= M; j++) {
      data[i * (M + 1) + j] -= mean[j];
      data[i * (M + 1) + j] /= sqrt(FLOAT_N);
      data[i * (M + 1) + j] /= stddev[j];
    }
  }

  // Calculate the m * m correlation matrix.
  for (j1 = 1; j1 <= M - 1; j1++) {
    symmat[j1 * (M + 1) + j1] = 1.0;

    for (j2 = j1 + 1; j2 <= M; j2++) {
      symmat[j1 * (M + 1) + j2] = 0.0;

      for (i = 1; i <= N; i++) {
        symmat[j1 * (M + 1) + j2] +=
            (data[i * (M + 1) + j1] * data[i * (M + 1) + j2]);
      }

      symmat[j2 * (M + 1) + j1] = symmat[j1 * (M + 1) + j2];
    }
  }

  symmat[M * (M + 1) + M] = 1.0;
}

int main(void) {
  DATA_TYPE *data;
  DATA_TYPE *mean;
  DATA_TYPE *stddev;
  DATA_TYPE *symmat;
  DATA_TYPE *symmat_outputFromGpu;

  data = (DATA_TYPE *)malloc((M + 1) * (N + 1) * sizeof(DATA_TYPE));
  mean = (DATA_TYPE *)malloc((M + 1) * sizeof(DATA_TYPE));
  stddev = (DATA_TYPE *)malloc((M + 1) * sizeof(DATA_TYPE));
  symmat = (DATA_TYPE *)malloc((M + 1) * (N + 1) * sizeof(DATA_TYPE));
  symmat_outputFromGpu =
      (DATA_TYPE *)malloc((M + 1) * (N + 1) * sizeof(DATA_TYPE));

  init_arrays(data);
  
  platform = new Platform(PLATFORM_ID);
  context = platform->getContext();
  Device device = platform->getDevice(DEVICE_ID);
  Queue queue(*context,device,Queue::EnableProfiling); 
 
  cl_mem_init(data, mean, stddev, symmat,queue);

  Program program(context,KERNEL_DIRECTORY KERNEL_FILE_NAME);
  if(!program.build(device)){
    std::cout << "Error building the program: \n";
    std::cout <<program.getBuildLog(device); 
  }

  kernel_mean=program.createKernel(meanKernelName.c_str());
  kernel_std=program.createKernel(stdKernelName.c_str());
  kernel_corr=program.createKernel(corrKernelName.c_str());
  kernel_reduce=program.createKernel(reduceKernelName.c_str());
  cl_launch_kernel(queue);

  queue.readBuffer(*symmat_mem_obj,(M + 1) * (N + 1) * sizeof(DATA_TYPE),symmat_outputFromGpu);
  queue.finish();

  correlation(data, mean, stddev, symmat);
  compareResults(symmat, symmat_outputFromGpu);
  cl_clean_up();

  free(data);
  free(mean);
  free(stddev);
  free(symmat);
  free(symmat_outputFromGpu);

  return 0;
}

