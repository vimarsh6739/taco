; ModuleID = 'sum.ll'
source_filename = "sum.ll"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: alwaysinline
define i32 @Sum(i32 %a, i32 %b) #0 {
  %c = add i32 %a, %b
  ret i32 %c
}

; vec_width: fixed to compile time cosntant 8
define void @Sum_vect(i32* %dst, i32* %a, i32* %b, i32 %offset) #0 {
  %gep_a = getelementptr i32* %a, i32 %offset, i32 0
  %gep_b = getelementptr i32* %b, i32 %offset, i32 0
  %gep_dst = getelementptr i32* %dst, i32 %offset, i32 0
  %vect_a_ptr = bitcast %gep_a <4 x i32>*
  %vect_b_ptr = bitcast %gep_b <4 x i32>*
  %vect_a = load <4 x i32>* %vect_a_ptr
  %vect_b = load <4 x i32>* %vect_b_ptr
  %sum_val = <4 x i32> add %vect_a, %vect_b
  


  ; gep dst pointer
  ; cast dest pointer as 8 x i32
  store %sum_val %gep_dst
  ret void
}

attributes #0 = { alwaysinline }
