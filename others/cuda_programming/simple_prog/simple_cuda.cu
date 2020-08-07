#include <iostream>
#include <math.h>

// function to add elements of 2 arrays
// CUDA Kernel function to add the elements of two arrays on the GPU
__global__
void add(int n, float *x, float *y)
{
    for (int i = 0; i < n; i++)
        y[i] = x[i] + y[i];
}

// main function
int main(void)
{
    int N = 1 << 20; // 1 milion elements
    // allocate on GPU
    float *x, *y;
    cudaMallocManaged(&x, N*sizeof(float));
    cudaMallocManaged(&y, N*sizeof(float));

    // initialize x and y
    std::cout << "1. Init array x, y" << std::endl;
    for (int i = 0; i < N; i++){
        x[i] = 1.0f;
        y[i] = 2.0f;
    }

    // run add kernel on GPU
    std::cout << "2. Run add kernel on GPU" << std::endl;
    add<<<1, 1>>>(N, x, y);

    // synchronization: wait for GPU finishes
    cudaDeviceSynchronize();

    // check the first 10 elements
    for (int i = 0; i < 10; i++)
        std::cout << y[i] << ",";
    std::cout << std::endl;

    // Check for errors (all values should be 3.0f)
    float maxError = 0.0f;
    for (int i = 0; i < N; i++)
        maxError = fmax(maxError, fabs(y[i]-3.0f));
    std::cout << "Max error: " << maxError << std::endl;

    // Free mem on GPU
    cudaFree(x);
    cudaFree(y);

    return 0;
}