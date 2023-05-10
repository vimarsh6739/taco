; ModuleID = 'vec-add.ll'
source_filename = "vec-add.ll"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "unknown-unknown-unknown"
attributes #0 = {alwaysinline}

;a[i] = b[j] + d + c[k] vectorized :)
; [[a,i],[b,j],d,[c,k]]
define void @vect_add(i32* %a, i32 %i, i32* %b ......) #0 {
    ;&a[i]
    %gep_a = getelementptr i32, i32* %a, i32 %i
    %vect_a_ptr = bitcast i32* %gep_a to <4 x i32>*

    %gep_b = getelementptr i32, i32* %b, i32 %j
    %vect_b_ptr = bitcast i32* %gep_b to <4 x i32>*
    %vect_b = load <4 x i32>,<4 x i32>* %vect_b_ptr

    %sum_val = add <4 x i32> %vect_a, %r2 ; hydride call
    store volatile <4 x i32> %sum_val, <4 x i32>* %vect_a_ptr, align 1
    ret void
}

; a[i] = b[j] + c
define void @vect_add(i32* %a, i32* %b, i32 c) #0 {
  %gep_a = getelementptr i32, i32* %a, i32 0
  %vect_a_ptr = bitcast i32* %gep_a to <4 x i32>*

  %gep_b = getelementptr i32, i32* %b, i32 0
  %vect_b_ptr = bitcast i32* %gep_b to <4 x i32>*
  %vect_b = load <4 x i32>,<4 x i32>* %vect_b_ptr

  %sum_val = add <4 x i32> %vect_a, %r2 ; hydride call
  store volatile <4 x i32> %sum_val, <4 x i32>* %vect_a_ptr, align 1
  ret void
}