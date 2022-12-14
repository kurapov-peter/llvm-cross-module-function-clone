; module-a

define i32 @foo(i64 %param) nounwind {
    %param.i32 = trunc i64 %param to i32
    %res = add i32 %param.i32, 1
    ret i32 %res
}

define i32 @bar(i64 %param) nounwind {
    %call.foo = call i32 @foo(i64 %param)
    %res = add i32 %call.foo, 1
    ret i32 %res
}

define i32 @callee() nounwind {
    ret i32 0
}

define i32 @foo_with_call(i64 %param) nounwind {
    ret i32 0
}