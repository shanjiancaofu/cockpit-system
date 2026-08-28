#include <cuda_runtime.h>

#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

#define CUDA_CHECK(call)                                                                            \
  do {                                                                                              \
    const cudaError_t status = (call);                                                             \
    if (status != cudaSuccess) {                                                                   \
      std::cerr << #call << " failed: " << cudaGetErrorString(status) << '\n';                    \
      return 1;                                                                                   \
    }                                                                                               \
  } while (false)

__device__ __forceinline__ std::uint16_t Sample(const std::uint16_t* raw, int width, int height,
                                                 int x, int y) {
  x = max(0, min(width - 1, x));
  y = max(0, min(height - 1, y));
  return raw[y * width + x];
}

__device__ __forceinline__ std::uint8_t Correct(std::uint16_t value, float gain) {
  const float normalized = fminf(1.0F, static_cast<float>(value) / 255.0F * gain);
  return static_cast<std::uint8_t>(fminf(255.0F, powf(normalized, 1.0F / 2.2F) * 255.0F));
}

__global__ void ProcessKernel(const std::uint16_t* raw, std::uint8_t* bgra, int width, int height) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) return;

  const bool row_red = (y & 1) == 0;
  const bool col_red = (x & 1) == 0;
  const auto value = [=](int px, int py) {
    const auto container = Sample(raw, width, height, px, py);
    const auto sample = (container >> 6U) > 64U ? (container >> 6U) - 64U : 0U;
    return static_cast<std::uint8_t>(min(255U, (static_cast<unsigned>(sample) * 255U) / 959U));
  };
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  if (row_red && col_red) {
    red = value(x, y);
    green = static_cast<std::uint8_t>((value(x - 1, y) + value(x + 1, y) + value(x, y - 1) +
                                       value(x, y + 1)) /
                                      4U);
    blue = static_cast<std::uint8_t>((value(x - 1, y - 1) + value(x + 1, y - 1) +
                                      value(x - 1, y + 1) + value(x + 1, y + 1)) /
                                     4U);
  } else if (!row_red && !col_red) {
    blue = value(x, y);
    green = static_cast<std::uint8_t>((value(x - 1, y) + value(x + 1, y) + value(x, y - 1) +
                                       value(x, y + 1)) /
                                      4U);
    red = static_cast<std::uint8_t>((value(x - 1, y - 1) + value(x + 1, y - 1) +
                                     value(x - 1, y + 1) + value(x + 1, y + 1)) /
                                    4U);
  } else {
    green = value(x, y);
    const bool red_row = row_red;
    red = red_row ? static_cast<std::uint8_t>((value(x - 1, y) + value(x + 1, y)) / 2U)
                  : static_cast<std::uint8_t>((value(x, y - 1) + value(x, y + 1)) / 2U);
    blue = red_row ? static_cast<std::uint8_t>((value(x, y - 1) + value(x, y + 1)) / 2U)
                   : static_cast<std::uint8_t>((value(x - 1, y) + value(x + 1, y)) / 2U);
  }
  const int offset = (y * width + x) * 4;
  bgra[offset] = Correct(blue, 1.5F);
  bgra[offset + 1] = Correct(green, 1.0F);
  bgra[offset + 2] = Correct(red, 1.8F);
  bgra[offset + 3] = 255;
}

double CpuMs(const rusage& usage) {
  return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 +
         static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4 || argc > 6) {
    std::cerr << "usage: camera-isp-cuda-benchmark RAW WIDTH HEIGHT [ITERATIONS] [OUTPUT_BGRX]\n";
    return 2;
  }
  const std::string path = argv[1];
  const int width = std::stoi(argv[2]);
  const int height = std::stoi(argv[3]);
  const int iterations = argc >= 5 ? std::stoi(argv[4]) : 300;
  const std::string output_path = argc == 6 ? argv[5] : "";
  if (width <= 0 || height <= 0 || iterations <= 0) return 2;
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  std::vector<std::uint16_t> host_raw(pixels);
  std::ifstream input(path, std::ios::binary);
  input.read(reinterpret_cast<char*>(host_raw.data()), static_cast<std::streamsize>(pixels * 2U));
  if (input.gcount() != static_cast<std::streamsize>(pixels * 2U)) {
    std::cerr << "RAW file is shorter than width*height*2\n";
    return 1;
  }
  std::uint16_t* device_raw = nullptr;
  std::uint8_t* device_bgra = nullptr;
  CUDA_CHECK(cudaMalloc(&device_raw, pixels * sizeof(std::uint16_t)));
  CUDA_CHECK(cudaMalloc(&device_bgra, pixels * 4U));
  std::vector<std::uint8_t> host_bgra(pixels * 4U);
  const dim3 block(16, 16);
  const dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
  for (int i = 0; i < 10; ++i) {
    CUDA_CHECK(cudaMemcpy(device_raw, host_raw.data(), pixels * sizeof(std::uint16_t), cudaMemcpyHostToDevice));
    ProcessKernel<<<grid, block>>>(device_raw, device_bgra, width, height);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
  }
  cudaEvent_t begin{}, end{}, kernel_begin{}, kernel_end{};
  CUDA_CHECK(cudaEventCreate(&begin));
  CUDA_CHECK(cudaEventCreate(&end));
  CUDA_CHECK(cudaEventCreate(&kernel_begin));
  CUDA_CHECK(cudaEventCreate(&kernel_end));
  rusage usage_started{};
  getrusage(RUSAGE_SELF, &usage_started);
  const auto wall_started = std::chrono::steady_clock::now();
  float total_kernel_ms = 0.0F;
  CUDA_CHECK(cudaEventRecord(begin));
  for (int i = 0; i < iterations; ++i) {
    CUDA_CHECK(cudaMemcpy(device_raw, host_raw.data(), pixels * sizeof(std::uint16_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaEventRecord(kernel_begin));
    ProcessKernel<<<grid, block>>>(device_raw, device_bgra, width, height);
    CUDA_CHECK(cudaEventRecord(kernel_end));
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaMemcpy(host_bgra.data(), device_bgra, pixels * 4U, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaEventSynchronize(kernel_end));
    float kernel_ms = 0.0F;
    CUDA_CHECK(cudaEventElapsedTime(&kernel_ms, kernel_begin, kernel_end));
    total_kernel_ms += kernel_ms;
  }
  CUDA_CHECK(cudaEventRecord(end));
  CUDA_CHECK(cudaEventSynchronize(end));
  const auto wall_elapsed = std::chrono::steady_clock::now() - wall_started;
  rusage usage_finished{};
  getrusage(RUSAGE_SELF, &usage_finished);
  const double wall_ms = std::chrono::duration<double, std::milli>(wall_elapsed).count();
  const double cpu_ms = CpuMs(usage_finished) - CpuMs(usage_started);
  if (!output_path.empty()) {
    std::ofstream output(output_path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(host_bgra.data()),
                 static_cast<std::streamsize>(host_bgra.size()));
    if (!output) {
      std::cerr << "failed to write CUDA output: " << output_path << '\n';
      return 1;
    }
    std::cout << "output_path=" << output_path << '\n';
  }
  std::cout << std::fixed << std::setprecision(3)
            << "iterations=" << iterations << " pixels=" << pixels << '\n'
            << "cuda_kernel_mean_ms=" << total_kernel_ms / iterations << '\n'
            << "cuda_end_to_end_mean_ms=" << wall_ms / iterations << '\n'
            << "process_cpu_ms=" << cpu_ms << " process_cpu_percent="
            << (wall_ms > 0.0 ? cpu_ms / wall_ms * 100.0 : 0.0) << '\n'
            << "output_bytes=" << host_bgra.size() << '\n';
  cudaEventDestroy(begin);
  cudaEventDestroy(end);
  cudaEventDestroy(kernel_begin);
  cudaEventDestroy(kernel_end);
  cudaFree(device_raw);
  cudaFree(device_bgra);
  return 0;
}
