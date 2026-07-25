; ModuleID = 'build/main.cat'
source_filename = "build/main.cat"

%list.i32 = type { i64, i64, ptr }

@0 = private unnamed_addr constant [20 x i8] c"mapped list[4] = %d\00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @square(i32 %a) {
entry:
  %0 = mul i32 %a, %a
  ret i32 %0
}

define i32 @main() {
entry:
  %list = alloca %list.i32, align 8
  store i64 5, ptr %list, align 4
  %0 = getelementptr inbounds nuw i8, ptr %list, i64 8
  store i64 10, ptr %0, align 4
  %1 = getelementptr inbounds nuw i8, ptr %list, i64 16
  %listdata = call ptr @malloc(i64 20)
  store ptr %listdata, ptr %1, align 8
  store i32 1, ptr %listdata, align 4
  %2 = getelementptr i8, ptr %listdata, i64 4
  store i32 2, ptr %2, align 4
  %3 = getelementptr i8, ptr %listdata, i64 8
  store i32 3, ptr %3, align 4
  %4 = getelementptr i8, ptr %listdata, i64 12
  store i32 4, ptr %4, align 4
  %5 = getelementptr i8, ptr %listdata, i64 16
  store i32 5, ptr %5, align 4
  %.unpack = load i64, ptr %list, align 8
  %6 = insertvalue %list.i32 poison, i64 %.unpack, 0
  %.elt3 = getelementptr inbounds nuw i8, ptr %list, i64 8
  %.unpack4 = load i64, ptr %.elt3, align 8
  %7 = insertvalue %list.i32 %6, i64 %.unpack4, 1
  %.elt5 = getelementptr inbounds nuw i8, ptr %list, i64 16
  %.unpack6 = load ptr, ptr %.elt5, align 8
  %8 = insertvalue %list.i32 %7, ptr %.unpack6, 2
  call void @__lambda_0(ptr nonnull @square, %list.i32 %8)
  %list.tmp = alloca %list.i32, align 8
  store i64 %.unpack, ptr %list.tmp, align 8
  %list.tmp.repack7 = getelementptr inbounds nuw i8, ptr %list.tmp, i64 8
  store i64 %.unpack4, ptr %list.tmp.repack7, align 8
  %list.tmp.repack9 = getelementptr inbounds nuw i8, ptr %list.tmp, i64 16
  store ptr %.unpack6, ptr %list.tmp.repack9, align 8
  %9 = icmp ugt i64 %.unpack, 4
  br i1 %9, label %bounds.ok, label %bounds.fail

bounds.ok:                                        ; preds = %entry
  %10 = getelementptr inbounds nuw i8, ptr %list.tmp, i64 16
  %11 = load ptr, ptr %10, align 8
  %12 = getelementptr i8, ptr %11, i64 16
  %13 = load i32, ptr %12, align 4
  %14 = call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @0, i32 %13)
  %putchar = call i32 @putchar(i32 10)
  ret i32 0

bounds.fail:                                      ; preds = %entry
  call void @llvm.trap()
  unreachable
}

define internal void @__lambda_0(ptr %f, ptr %l) {
entry:
  %l2 = alloca ptr, align 8
  store ptr %l, ptr %l2, align 8
  br label %while.cond

while.cond:                                       ; preds = %bounds.ok10, %entry
  %i.0 = phi i32 [ 0, %entry ], [ %15, %bounds.ok10 ]
  %0 = load i64, ptr %l2, align 4
  %1 = trunc i64 %0 to i32
  %2 = icmp slt i32 %i.0, %1
  br i1 %2, label %while.body, label %while.exit

while.body:                                       ; preds = %while.cond
  %l5 = load ptr, ptr %l2, align 8
  %.unpack = load i64, ptr %l5, align 8
  %.elt13 = getelementptr inbounds nuw i8, ptr %l5, i64 8
  %.unpack14 = load i64, ptr %.elt13, align 8
  %.elt15 = getelementptr inbounds nuw i8, ptr %l5, i64 16
  %.unpack16 = load ptr, ptr %.elt15, align 8
  %3 = zext i32 %i.0 to i64
  %list.tmp = alloca %list.i32, align 8
  store i64 %.unpack, ptr %list.tmp, align 8
  %list.tmp.repack17 = getelementptr inbounds nuw i8, ptr %list.tmp, i64 8
  store i64 %.unpack14, ptr %list.tmp.repack17, align 8
  %list.tmp.repack19 = getelementptr inbounds nuw i8, ptr %list.tmp, i64 16
  store ptr %.unpack16, ptr %list.tmp.repack19, align 8
  %4 = icmp ugt i64 %.unpack, %3
  br i1 %4, label %bounds.ok, label %bounds.fail

while.exit:                                       ; preds = %while.cond
  ret void

bounds.ok:                                        ; preds = %while.body
  %5 = getelementptr inbounds nuw i8, ptr %list.tmp, i64 16
  %6 = load ptr, ptr %5, align 8
  %7 = getelementptr i32, ptr %6, i64 %3
  %8 = load i32, ptr %7, align 4
  %9 = call i32 %f(i32 %8)
  %l7 = load ptr, ptr %l2, align 8
  %.unpack21 = load i64, ptr %l7, align 8
  %.elt22 = getelementptr inbounds nuw i8, ptr %l7, i64 8
  %.unpack23 = load i64, ptr %.elt22, align 8
  %.elt24 = getelementptr inbounds nuw i8, ptr %l7, i64 16
  %.unpack25 = load ptr, ptr %.elt24, align 8
  %10 = zext i32 %i.0 to i64
  %list.tmp9 = alloca %list.i32, align 8
  store i64 %.unpack21, ptr %list.tmp9, align 8
  %list.tmp9.repack26 = getelementptr inbounds nuw i8, ptr %list.tmp9, i64 8
  store i64 %.unpack23, ptr %list.tmp9.repack26, align 8
  %list.tmp9.repack28 = getelementptr inbounds nuw i8, ptr %list.tmp9, i64 16
  store ptr %.unpack25, ptr %list.tmp9.repack28, align 8
  %11 = icmp ugt i64 %.unpack21, %10
  br i1 %11, label %bounds.ok10, label %bounds.fail11

bounds.fail:                                      ; preds = %while.body
  call void @llvm.trap()
  unreachable

bounds.ok10:                                      ; preds = %bounds.ok
  %12 = getelementptr inbounds nuw i8, ptr %list.tmp9, i64 16
  %13 = load ptr, ptr %12, align 8
  %14 = getelementptr i32, ptr %13, i64 %10
  store i32 %9, ptr %14, align 4
  %15 = add i32 %i.0, 1
  br label %while.cond

bounds.fail11:                                    ; preds = %bounds.ok
  call void @llvm.trap()
  unreachable
}

; Function Attrs: cold noreturn nounwind memory(inaccessiblemem: write)
declare void @llvm.trap() #0

declare ptr @malloc(i64)

declare i32 @printf(ptr, ...)

; Function Attrs: nofree nounwind
declare noundef i32 @putchar(i32 noundef) #1

attributes #0 = { cold noreturn nounwind memory(inaccessiblemem: write) }
attributes #1 = { nofree nounwind }
