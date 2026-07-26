; ModuleID = '/home/buyi/code/cat-lang/build/main.cat'
source_filename = "/home/buyi/code/cat-lang/build/main.cat"

%Point = type { i32, i32 }

define ptr @Point_ctor(i32 %0, i32 %1) {
entry:
  %this = call ptr @malloc(i64 ptrtoint (ptr getelementptr (%Point, ptr null, i32 1) to i64))
  %2 = getelementptr inbounds nuw %Point, ptr %this, i32 0, i32 0
  store i32 %0, ptr %2, align 4
  %3 = getelementptr inbounds nuw %Point, ptr %this, i32 0, i32 1
  store i32 %1, ptr %3, align 4
  ret ptr %this
}

declare ptr @malloc(i64)

define i32 @main() {
entry:
  %0 = call ptr @Point_ctor(i32 1, i32 2)
  ret i32 0
}
