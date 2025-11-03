
#include <hip/hiprtc.h>
#include <hip/hip_runtime.h>

#include <hip_test_common.hh>
#include <hip_test_filesystem.hh>


#include <cassert>
#include <cstddef>
#include <fstream>
#include <memory>
#include <iostream>
#include <iterator>
#include <vector>

#pragma clang diagnostic ignored "-Wuninitialized"





static constexpr auto src{
    R"(
extern "C"
__global__
void saxpy(float a, float* x, float* y, float* out, size_t n)
{
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < n) {
           out[tid] = a * x[tid] + y[tid];
    }
}
)"};



std::vector<char> createBitcodeForArch(const char* src, const char* name, const char* arch) {
  hiprtcProgram program;
  HIPRTC_CHECK(hiprtcCreateProgram(&program, src, name, 0, nullptr, nullptr));

  // Compile for specific architecture
  std::string arch_opt = std::string("--gpu-architecture=") + arch;
  const char* options[] = {"-fgpu-rdc", arch_opt.c_str()};

  HIPRTC_CHECK(hiprtcCompileProgram(program, 2, options));

  size_t codesize = 0;
  HIPRTC_CHECK(hiprtcGetBitcodeSize(program, &codesize));

  std::vector<char> code(codesize, '\0');
  HIPRTC_CHECK(hiprtcGetBitcode(program, &code[0]));

  HIPRTC_CHECK(hiprtcDestroyProgram(&program));
  return code;
}

std::vector<char> bundleMultipleArchBitcodes(const char* src, const char* name,
                                             const std::vector<std::string>& archs) {
  // Step 1: Compile separate bitcode for each architecture
  std::vector<std::string> bc_files;
  for (const auto& arch : archs) {
    std::vector<char> bc = createBitcodeForArch(src, name, arch.c_str());

    // Save to temporary file
    std::string bc_file = "temp_" + arch + ".bc";
    std::ofstream out(bc_file, std::ios::binary);
    out.write(bc.data(), bc.size());
    out.flush();
    out.close();

    std::ifstream check(bc_file, std::ios::binary | std::ios::ate);

    bc_files.push_back(bc_file);
  }

  // Step 2: Bundle using clang-offload-bundler
  std::string bundler_cmd = "/opt/rocm/llvm/bin/clang-offload-bundler";
  bundler_cmd += " -type=bc";

  // Add inputs
  for (const auto& bc_file : bc_files) {
    bundler_cmd += " -input=" + bc_file;
  }

  // Add targets
  bundler_cmd += " -targets=";
  for (size_t i = 0; i < archs.size(); ++i) {
    if (i > 0) bundler_cmd += ",";
    bundler_cmd += "hip-amdgcn-amd-amdhsa--" + archs[i];
  }

  // Output
  std::string bundle_file = "bundledtt2.bc";
  bundler_cmd += " -output=" + bundle_file;

  // Execute
  int ret = system(bundler_cmd.c_str());
  if (ret != 0) {
    throw std::runtime_error("Failed to bundle bitcode");
  }

  // Step 3: Read bundled bitcode
  std::ifstream in(bundle_file, std::ios::binary);
  std::vector<char> bundled_bc((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
  in.close();

  std::string of = "output1.bc";
  std::ofstream out(of, std::ios::binary);
  out.write(bundled_bc.data(), bundled_bc.size());
  out.flush();
  out.close();

  return bundled_bc;
}

TEST_CASE("Unit_RTC_BUNDLE")  {
  std::vector<std::string> archs = {"gfx1030"};
  std::vector<char> bundled_bc = bundleMultipleArchBitcodes(src, "saxpy", archs);
  
  hiprtcLinkState linkstate;
  HIPRTC_CHECK(hiprtcLinkCreate(0, nullptr, nullptr, &linkstate));
  HIPRTC_CHECK(hiprtcLinkAddData(linkstate, HIPRTC_JIT_INPUT_LLVM_BUNDLED_BITCODE,
                                 bundled_bc.data(), bundled_bc.size(),
                                 "multi_arch_bundle", 0, nullptr, nullptr));
  
  void* finaldata;
  size_t finalsize = 0;
  HIPRTC_CHECK(hiprtcLinkComplete(linkstate, &finaldata, &finalsize));
  hipModule_t module;
  hipFunction_t kernel;
  HIP_CHECK(hipModuleLoadData(&module, finaldata));
  HIP_CHECK(hipModuleGetFunction(&kernel, module, "saxpy"));


  HIPRTC_CHECK(hiprtcLinkDestroy(linkstate));
  HIP_CHECK(hipModuleUnload(module));

}

