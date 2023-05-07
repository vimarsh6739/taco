#include "rosette.h"

#include <chrono>

using namespace std;

namespace taco {
namespace ir {

void hydride_generate_llvm_bitcode(std::string input_file, std::string output_file, std::string benchmark_name) {

    std::string target_flag = "-x86-hydride-legalize";

    const char* hydride_src = getenv("HYDRIDE_ROOT");
    assert(hydride_src && "HYDRIDE_ROOT environment variable needs to be defined for codegen");

    const char* legalizer_so = getenv("LEGALIZER_PATH");
    assert(legalizer_so && "LEGALIZER_PATH environment variable must be defined for hydride codegen");

    const char* intrin_wrapper = getenv("INTRINSICS_LL");
    assert(intrin_wrapper && "INTRINSICS_LL must be defined for hydride llvm code-gen");

    std::string cmd = "python " + std::string(hydride_src)
                    + "/codegen-generator/tools/low-level-codegen/RoseLowLevelCodeGen.py "
                    + input_file + " "
                    + std::string(legalizer_so) + " "
                    + std::string(intrin_wrapper) + " "
                    + target_flag + " "
                    + output_file;
    
    auto start = std::chrono::system_clock::now();
    int ret_code = system(cmd.c_str());
    taco_iassert(ret_code == 0) << "Codegen crashed, exiting ...";

    auto end = std::chrono::system_clock::now();
    std::cout << "Compilation took " << (end - start).count() << "seconds." << std::endl;

    // TEMP CMD
    std::string temp_cmd = "cp /tmp/" + benchmark_name + ".ll.legalize.ll " + output_file;
    ret_code = system(temp_cmd.c_str());
    taco_iassert(ret_code == 0) << "Copying crashed, exiting ...";

}

}
}
