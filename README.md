# LLVM cross-module function clone example

This is just an experiment that I wanted to save in case I ever need to do anythin similar again. I found no single good source of infomation for llvm cross-module function cloning. It now feels pretty straightforward when I finished it. I suspect this is exactly the reason for the lack of resources.

## Goal
My goal is to be able to replace some functions in module A from module B implementations and copy some declarations (assuming there is a module C that provides definitions for B's declarations that will be linked against module A later on).

The simple solution would be to use the linker with "override" flag. However, the linker does not suit me as it produces indirect calls and I want to avoid those in this experiment. There is a function ```CloneFunctionInto``` that does something very similar to what I need. There are a couple of questions though:
1. The interfaces suggests the function should already exist in module A. So cloning a delaration is just creating a new one with the same parameters (?).
2. VMap's purpose is unclear from the first glance. My previous test failed because a copied function was still referencing something in module B. I want to be able to reproduce that here.
3. There is ```CloneFunctionChangeType``` the purpose of which is now unclear to me.

## Experiment notes
Creating a similar declartion is pretty straightforward and produces valid IR. No actual cloning involved. For example:
```C++
void clone_decl(llvm::Module *from, llvm::Module *to,
                const std::string &fname) {
  auto fn = from->getFunction(fname);
  llvm::Function::Create(fn->getFunctionType(),
                         llvm::GlobalValue::ExternalLinkage, fn->getName(),
                         *to);
}
```

Starting with the defaults and see how the function behaves. First, I create an empty VMap and Returns vector, and pick ```CloneFunctionChangeType::DifferentModule``` (just a guess, I do have different modules). This way it breaks with a segfault in ```CloneFunctionInto```. A quick glance at the implementation gives the answer: there is no assertion for VMap in NDEBUG build. So, how do I populate it? All I need here is to get all the args references for the target function in module A and map them to the args of the function from module B, so that VMap[&arg_fn_B] = &arg_fn_A.
From https://lists.llvm.org/pipermail/llvm-dev/2018-September/125881.html thread, directly reusing a snippet, cloning is successful and the module passes verification:
```llvm
; ModuleID = 'module-a.ll'
source_filename = "module-a.ll"

; Function Attrs: nounwind
define i32 @foo(i64 %param) #0 {
  %param.i32 = trunc i64 %param to i32
  %res = add i32 %param.i32, 1
  ret i32 %res

1:                                                ; No predecessors!
  %param.i321 = trunc i64 %param to i32
  %res2 = add i32 %param.i321, 2
  ret i32 %res2
}

; Function Attrs: nounwind
define i32 @bar(i64 %param) #0 {
  %call.foo = call i32 @foo(i64 %param)
  %res = add i32 %call.foo, 1
  ret i32 %res
}

attributes #0 = { nounwind }

!llvm.dbg.cu = !{}
```

Note the ```!llvm.dbg.cu = !{}```. This is only inserted when the change type is ```CloneFunctionChangeType::DifferentModule```.
According to langref this is used to collect compile units metadata (see https://llvm.org/docs/LangRef.html#dicompileunit).
It does not seem to affect my simple example anyhow: all the other types produce the same code and result with the only difference --- no ```!llvm.dbg.cu = !{}```.

So, for definitions, ```CloneFunctionInto``` inserts basic blocks at the end of the function, meaning, I need to clean up the function body first. Removing the ```@foo``` body first with ```deleteBody()``` yields a valid IR.

Let's try running it!
Added ```mcjit mc nativecodegen executionengine interpreter x86asmparser``` llvm components.
I had to add asm printer and parser for the engine to run. Otherwise there's an error: LLVM ERROR: Target does not support MC emission!
Running ```@foo``` with the JIT yeilds correct result.

Let's try a more complex function ```@foo_with_call```. This time it will have a call into module B.
The problem I expect to have here is that when I clone the function ```@foo_with_call```, ```@calleee``` will still point to a different module and the module verification should fail.
And that's exactly what happens:
```
Referencing function in another module!
  %callee = call i32 @callee()
```

Having an own version of ```@callee``` in module A doesn't help, of course. So let's try to remap the calls to target internal version of functions. The simplest way of doing this is to traverse the cloned funciton and replace all the ```CallInst```s.

Replacing all uses of callee from module B with callee from module A wouldn't work, since we will have a global referenced in a different module. I.e.
```C++
  auto callee_b = mod_b->getFunction("callee");
  auto callee_a = mod_a->getFunction("callee");
  callee_b->replaceAllUsesWith(callee_a);
```

Replacing only uses in the current module produces correct result. For example:
```C++
auto cloned = mod_a->getFunction("foo_with_call");
  for (auto &BB : *cloned) {
    for (llvm::BasicBlock::iterator bbi = BB.begin(); bbi != BB.end();) {
      llvm::Instruction *inst = &*bbi++;
      if (auto *call = llvm::dyn_cast<llvm::CallInst>(&*inst)) {
        auto local_callee =
            mod_a->getFunction(call->getCalledFunction()->getName());
        auto new_call = llvm::CallInst::Create(local_callee, "local_callee");

        llvm::ReplaceInstWithInst(call, new_call);
      }
    }
  }
```

## Resources
Clone functions sources: https://llvm.org/doxygen/CloneFunction_8cpp_source.html.

Building a cmake project and linking against LLVM: https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project.

There is a thread in llvm mailing list discussing the problem: https://lists.llvm.org/pipermail/llvm-dev/2018-September/125881.html.

Side-note: there's a nice small vscode extension for LLVM IR from rev.ng Labs plus syntax hightlighting from colejcummins that I found useful.
