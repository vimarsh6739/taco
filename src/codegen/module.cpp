#include "taco/codegen/module.h"

#include <iostream>
#include <fstream>
#include <dlfcn.h>
#include <unistd.h>
#if USE_OPENMP
#include <omp.h>
#endif

#include "taco/tensor.h"
#include "taco/error.h"
#include "taco/util/strings.h"
#include "taco/util/env.h"
#include "codegen/codegen_c.h"
#include "codegen/codegen_cuda.h"
#include "taco/cuda.h"

using namespace std;

namespace taco {
namespace ir {

std::string Module::chars = "abcdefghijkmnpqrstuvwxyz0123456789";
std::default_random_engine Module::gen = std::default_random_engine();
std::uniform_int_distribution<int> Module::randint =
    std::uniform_int_distribution<int>(0, chars.length() - 1);

void Module::setJITTmpdir() {
  tmpdir = util::getTmpdir();
}

void Module::setJITLibname() {
  libname.resize(12);
  for (int i=0; i<12; i++)
    libname[i] = chars[randint(gen)];
}

void Module::addFunction(Stmt func) {
  funcs.push_back(func);
}

bool Module::compileToSource(string path, string prefix, bool emitHydride) {
  std::cout << "Writing generated C file to: " << path << "" << prefix << ".c" << std::endl;
  bool mutated_expr = false;

  if (!moduleFromUserSource) {
  
    // create a codegen instance and add all the funcs
    bool didGenRuntime = false;
    
    header.str("");
    header.clear();
    source.str("");
    source.clear();

    taco_tassert(target.arch == Target::C99) <<
        "Only C99 codegen supported currently";

    std::shared_ptr<CodeGen> sourcegen;
    std::shared_ptr<CodeGen> headergen;
    
    if (emitHydride) {
      sourcegen = CodeGen::init_hydride(source, CodeGen::ImplementationGen);
      headergen = CodeGen::init_hydride(header, CodeGen::HeaderGen);
    } else {
      sourcegen = CodeGen::init_default(source, CodeGen::ImplementationGen);
      headergen = CodeGen::init_default(header, CodeGen::HeaderGen);
    }

    for (auto func: funcs) {
      sourcegen->compile(func, !didGenRuntime);
      headergen->compile(func, !didGenRuntime);
      didGenRuntime = true;
    }

    if (emitHydride) {
      mutated_expr = std::dynamic_pointer_cast<CodeGen_C>(sourcegen)->did_mutate_expr();
    }
  }

  ofstream source_file;
  string file_ending = should_use_CUDA_codegen() ? ".cu" : ".c";
  source_file.open(path+prefix+file_ending);
  source_file << source.str();
  source_file.close();
  
  ofstream header_file;
  header_file.open(path+prefix+".h");
  header_file << header.str();
  header_file.close();

  return mutated_expr;
}

void Module::compileToStaticLibrary(string path, string prefix, bool emitHydride) {
  taco_tassert(false) << "Compiling to a static library is not supported";
}
  
namespace {

void writeShims(vector<Stmt> funcs, string path, string prefix) {
  stringstream shims;
  for (auto func: funcs) {
    if (should_use_CUDA_codegen()) {
      CodeGen_CUDA::generateShim(func, shims);
    }
    else {
      CodeGen_C::generateShim(func, shims);
    }
  }
  
  ofstream shims_file;
  if (should_use_CUDA_codegen()) {
    shims_file.open(path+prefix+"_shims.cpp");
  }
  else {
    shims_file.open(path+prefix+".c", ios::app);
  }
  shims_file << "#include \"" << path << prefix << ".h\"\n";
  shims_file << shims.str();
  shims_file.close();
}

} // anonymous namespace

string Module::compile(bool emitHydride) {
  string prefix = tmpdir+libname;
  string fullpath = prefix + ".so";
  
  string cc;
  string cflags;
  string file_ending;
  string shims_file;
  if (should_use_CUDA_codegen()) {
    cc = util::getFromEnv("TACO_NVCC", "nvcc");
    cflags = util::getFromEnv("TACO_NVCCFLAGS",
    get_default_CUDA_compiler_flags());
    file_ending = ".cu";
    shims_file = prefix + "_shims.cpp";
  } else {
    cc = util::getFromEnv(target.compiler_env, target.compiler);
#ifdef TACO_DEBUG
    // In debug mode, compile the generated code with debug symbols and a
    // low optimization level.
    string defaultFlags = "-g -O0 -std=c99";
#else
    // Otherwise, use the standard set of optimizing flags.
    string defaultFlags = "-O3 -ffast-math -std=c99";
#endif
    cflags = util::getFromEnv("TACO_CFLAGS", defaultFlags) + " -shared -fPIC";
#if USE_OPENMP
    cflags += " -fopenmp";
#endif
    file_ending = ".c";
    shims_file = "";
  }

  // open the output file & write out the source
  bool mutated_expr = compileToSource(tmpdir, libname, emitHydride);

  // write out the shims
  writeShims(funcs, tmpdir, libname);

  // now compile it
  string cmd;
  int err;
  if (emitHydride) {
    if (mutated_expr) {
      cmd = "clang -g -O0 -std=c99 -S -emit-llvm " + prefix + ".c -o " + prefix + ".ll";
      err = system(cmd.data());
      taco_uassert(err == 0) << "Compilation command failed:" << std::endl << cmd << std::endl << "returned " << err;

      cmd = "llvm-link -S " + prefix + ".ll bin/llvm_shim_tydride.ll bin/tydride.ll.legalize.ll > " + prefix + "_linked.ll";
      err = system(cmd.data());
      taco_uassert(err == 0) << "Linking command failed:" << std::endl << cmd << std::endl << "returned " << err;

      cmd = "opt --O3 --adce --aggressive-instcombine --always-inline -S " + prefix + "_linked.ll > " + prefix + "_linked_opt.ll";
      err = system(cmd.data());
      taco_uassert(err == 0) << "Inlining command failed:" << std::endl << cmd << std::endl << "returned " << err;

      cmd = "clang -shared -fPIC " + prefix + "_linked_opt.ll -o " + fullpath + " -lm";
      err = system(cmd.data());
      taco_uassert(err == 0) << "Compilation command failed:" << std::endl << cmd << std::endl << "returned " << err;

      cmd = "cp " + prefix + "_linked_opt.ll " + "bin/linked_opt.ll";
      err = system(cmd.data());
      taco_uassert(err == 0) << "Error copying the linked ll file." << std::endl;

      // std::cout << "Beginning hydride emission" << std::endl;
      // cmd = "clang -g -O0 -std=c99 -shared -fPIC " + prefix + ".c bin/llvm_shim_tydride.ll bin/tydride.ll.legalize.ll -o " + fullpath + " -lm";
      // err = system(cmd.data());
      // taco_uassert(err == 0) << "Compilation command failed:" << std::endl << cmd << std::endl << "returned " << err;
    } else {
      cmd = "clang -g -O0 -std=c99 -shared -fPIC " + prefix + ".c -o " + fullpath + " -lm";
      err = system(cmd.data());
      taco_uassert(err == 0) << "Compilation command failed:" << std::endl << cmd << std::endl << "returned " << err;
    }
  } else {
    cmd = cc + " " + cflags + " " + prefix + file_ending + " " + shims_file + " " + "-o " + fullpath + " -lm";
    err = system(cmd.data());
    taco_uassert(err == 0) << "Compilation command failed:" << std::endl << cmd << std::endl << "returned " << err;
  }

  // use dlsym() to open the compiled library
  if (lib_handle) {
    dlclose(lib_handle);
  }
  lib_handle = dlopen(fullpath.data(), RTLD_NOW | RTLD_LOCAL);
  taco_uassert(lib_handle) << "Failed to load generated code, error is: " << dlerror();

  return fullpath;
}

void Module::setSource(string source) {
  this->source << source;
  moduleFromUserSource = true;
}

string Module::getSource() {
  return source.str();
}

void* Module::getFuncPtr(std::string name) {
  return dlsym(lib_handle, name.data());
}

int Module::callFuncPackedRaw(std::string name, void** args) {
  typedef int (*fnptr_t)(void**);
  static_assert(sizeof(void*) == sizeof(fnptr_t),
    "Unable to cast dlsym() returned void pointer to function pointer");
  void* v_func_ptr = getFuncPtr(name);
  fnptr_t func_ptr;
  *reinterpret_cast<void**>(&func_ptr) = v_func_ptr;

#if USE_OPENMP
  omp_sched_t existingSched;
  ParallelSchedule tacoSched;
  int existingChunkSize, tacoChunkSize;
  int existingNumThreads = omp_get_max_threads();
  omp_get_schedule(&existingSched, &existingChunkSize);
  taco_get_parallel_schedule(&tacoSched, &tacoChunkSize);
  switch (tacoSched) {
    case ParallelSchedule::Static:
      omp_set_schedule(omp_sched_static, tacoChunkSize);
      break;
    case ParallelSchedule::Dynamic:
      omp_set_schedule(omp_sched_dynamic, tacoChunkSize);
      break;
    default:
      break;
  }
  omp_set_num_threads(taco_get_num_threads());
#endif

  int ret = func_ptr(args);

#if USE_OPENMP
  omp_set_schedule(existingSched, existingChunkSize);
  omp_set_num_threads(existingNumThreads);
#endif

  return ret;
}

} // namespace ir
} // namespace taco
