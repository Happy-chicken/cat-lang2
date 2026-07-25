; ModuleID = '/tmp/test.cat'
source_filename = "/tmp/test.cat"

@0 = private unnamed_addr constant [21 x i8] c"hello from cat-lang!\00", align 1
@1 = private unnamed_addr constant [16 x i8] c"square(%d) = %d\00", align 1
@2 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @square(i32 %a) {
entry:
  %0 = mul i32 %a, %a
  ret i32 %0
}

define i32 @main() {
entry:
  %0 = call i32 @puts(ptr noundef nonnull dereferenceable(1) @0)
  %1 = call i32 @square(i32 42)
  %2 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @1, i32 42, i32 %1)
  %putchar = call i32 @putchar(i32 10)
  ret i32 0
}

declare i32 @puts(ptr)

declare i32 @printf(ptr, ...)

; Function Attrs: nofree nounwind
declare noundef i32 @putchar(i32 noundef) #0

attributes #0 = { nofree nounwind }
