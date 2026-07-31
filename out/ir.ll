; ModuleID = '/tmp/test_str_cleanup.cat'
source_filename = "/tmp/test_str_cleanup.cat"

%str = type { i64, ptr }

@0 = private unnamed_addr constant [6 x i8] c"hello\00", align 1

define i32 @take_str(%str %s) {
entry:
  %s1 = alloca %str, align 8
  store %str %s, ptr %s1, align 8
  %0 = getelementptr inbounds nuw %str, ptr %s1, i32 0, i32 1
  %1 = load ptr, ptr %0, align 8
  call void @free(ptr %1)
  ret i32 0
}

declare void @free(ptr)

define i32 @main() {
entry:
  %s = alloca %str, align 8
  store %str { i64 5, ptr @0 }, ptr %s, align 8
  %s1 = load %str, ptr %s, align 8
  %0 = call i32 @take_str(%str %s1)
  store %str undef, ptr %s, align 8
  ret i32 0
}
