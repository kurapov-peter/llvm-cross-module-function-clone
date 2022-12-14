#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

void verify(const llvm::Module *M) {
  std::stringstream ss;
  llvm::raw_os_ostream os(ss);
  if (llvm::verifyModule(*M, &os)) {
    std::cerr << ss.str();
    throw std::runtime_error("");
  }
}

std::unique_ptr<llvm::Module> read(const std::string &path,
                                   llvm::LLVMContext &ctx) {
  llvm::SMDiagnostic err;
  auto parsed = llvm::parseIRFile(path, err, ctx);
  if (!parsed) {
    err.print("llvm-function-clone", llvm::errs());
    throw std::runtime_error("Failed to read " + path + " module.");
  }
  verify(parsed.get());
  return parsed;
}

void replace_func(llvm::Module *from, llvm::Module *to,
                  const std::string &fname) {
  auto target_fn = to->getFunction(fname);
  auto from_fn = from->getFunction(fname);
  assert(target_fn);
  assert(from_fn);

  target_fn->deleteBody();

  llvm::ValueToValueMapTy vmap;
  llvm::Function::arg_iterator pos_fn_arg_it = target_fn->arg_begin();
  for (llvm::Function::const_arg_iterator j = from_fn->arg_begin();
       j != from_fn->arg_end(); ++j) {
    pos_fn_arg_it->setName(j->getName());
    vmap[&*j] = &*pos_fn_arg_it++;
  }
  llvm::SmallVector<llvm::ReturnInst *, 8> returns;
  llvm::CloneFunctionInto(target_fn, from_fn, vmap,
                          llvm::CloneFunctionChangeType::GlobalChanges,
                          returns);
  // no module verification here as it may produce invalid IR
}

void clone_decl(llvm::Module *from, llvm::Module *to,
                const std::string &fname) {
  auto fn = from->getFunction(fname);
  assert(fn);
  assert(fn->isDeclaration());

  llvm::Function::Create(fn->getFunctionType(),
                         llvm::GlobalValue::ExternalLinkage, fn->getName(),
                         *to);

  verify(to);
}

int main() {
  llvm::LLVMContext ctx;
  auto mod_a = read("module-a.ll", ctx);
  auto mod_b = read("module-b.ll", ctx);
  clone_decl(mod_b.get(), mod_a.get(), "some_declaration");
  replace_func(mod_b.get(), mod_a.get(), "foo");
  replace_func(mod_b.get(), mod_a.get(), "callee");
  replace_func(mod_b.get(), mod_a.get(), "foo_with_call");

  auto cloned = mod_a->getFunction("foo_with_call");
  for (auto &BB : *cloned) {
    for (llvm::BasicBlock::iterator bbi = BB.begin(); bbi != BB.end();) {
      llvm::Instruction *inst = &*bbi++;
      if (auto *call = llvm::dyn_cast<llvm::CallInst>(&*inst)) {
        auto local_callee =
            mod_a->getFunction(call->getCalledFunction()->getName());
        assert(local_callee);
        auto new_call = llvm::CallInst::Create(local_callee, "local_callee");

        llvm::ReplaceInstWithInst(call, new_call);
      }
    }
  }

  verify(mod_a.get());

  mod_a->print(llvm::errs(), nullptr);

  std::string error;
  llvm::InitializeNativeTarget();
  // These just prevent LLVM ERROR: Target does not support MC emission!
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto engine = llvm::EngineBuilder(std::move(mod_a))
                    .setErrorStr(&error)
                    .setOptLevel(llvm::CodeGenOpt::Aggressive)
                    .setEngineKind(llvm::EngineKind::JIT)
                    .create();
  if (!engine) {
    std::cerr << "EE Error: " << error << '\n';
    return 1;
  }

  engine->finalizeObject();
  typedef int32_t (*Function)(int64_t);
  Function bar =
      reinterpret_cast<Function>(engine->getPointerToNamedFunction("bar"));
  int32_t a = bar(2);
  std::cout << "bar(2) = " << a << std::endl;

  Function foo_with_call = reinterpret_cast<Function>(
      engine->getPointerToNamedFunction("foo_with_call"));
  int32_t b = foo_with_call(2);
  std::cout << "foo_with_call(2) = " << b << std::endl;
}
