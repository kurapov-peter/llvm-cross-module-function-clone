; module-b

declare i32 @some_declaration()

define i32 @foo(i64 %param) nounwind {
    %param.i32 = trunc i64 %param to i32
    %res = add i32 %param.i32, 2
    ret i32 %res
}

define i32 @callee() nounwind {
    ret i32 1
}

define i32 @foo_with_call(i64 %param) nounwind {
    %param.i32 = trunc i64 %param to i32
    %callee = call i32 @callee()
    %res = add i32 %param.i32, %callee
    ret i32 %res
}