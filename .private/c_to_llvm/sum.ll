; ModuleID = 'sum.ll'
source_filename = "sum.ll"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: alwaysinline
define i32 @Sum(i32 %a, i32 %b) #0 {
  %c = add i32 %a, %b
  ret i32 %c
}

; vec_width: fixed to compile time cosntant 4
define void @Sum_vect_4(i32* %dst, i32* %a, i32* %b, i32 %offset) #0 {
  
  ;Get the pointers to the correct locations for all inputs and the outputs
  %gep_a = getelementptr i32, i32* %a, i32 %offset
  %gep_b = getelementptr i32, i32* %b, i32 %offset
  %gep_dst = getelementptr i32, i32* %dst, i32 %offset

  ;Bitcast to fixed length vectors
  %vect_a_ptr = bitcast i32* %gep_a to <4 x i32>*
  %vect_b_ptr = bitcast i32* %gep_b to <4 x i32>*
  
  ;Load values into registers
  %vect_a = load <4 x i32>,<4 x i32>* %vect_a_ptr
  %vect_b = load <4 x i32>,<4 x i32>* %vect_b_ptr
  
  ;Replace this with a call to the lifted llvm function from hydride with typecasted operands
  %sum_val = add <4 x i32> %vect_a, %vect_b

  ; Bitcast the destination pointer as a ptr to a fixed length vector 
  %vect_dst_ptr = bitcast i32* %gep_dst to <4 x i32>*

  ; Store the computed result to the destination
  store volatile <4 x i32> %sum_val, <4 x i32>* %vect_dst_ptr, align 1
  
  ; Return
  ret void
}

; vec_width: fixed to compile time cosntant 8
define void @Sum_vect_8(i32* %dst, i32* %a, i32* %b, i32 %offset) #0 {
  %gep_a = getelementptr i32, i32* %a, i32 %offset
  %gep_b = getelementptr i32, i32* %b, i32 %offset
  %gep_dst = getelementptr i32, i32* %dst, i32 %offset

  %vect_a_ptr = bitcast i32* %gep_a to <8 x i32>*
  %vect_b_ptr = bitcast i32* %gep_b to <8 x i32>*
  ; %vect_a_ptr = bitcast %gep_a <4 x i32>*
  ; %vect_b_ptr = bitcast %gep_b <4 x i32>*
  %vect_a = load <8 x i32>,<8 x i32>* %vect_a_ptr
  %vect_b = load <8 x i32>,<8 x i32>* %vect_b_ptr

  %sum_val = add <8 x i32> %vect_a, %vect_b

  ; gep dst pointer
  ; cast dest pointer as 8 x i32
  %vect_dst_ptr = bitcast i32* %gep_dst to <8 x i32>*
  store volatile <8 x i32> %sum_val, <8 x i32>* %vect_dst_ptr, align 1
  
  ; store %sum_val %gep_dst
  ret void
}

define i32 @get_index(i32* %a, i32 %index){
  %val_ptr = getelementptr i32, i32* %a, i32 %index
   %value = load i32, i32* %val_ptr
   ret i32 %value
}

attributes #0 = { alwaysinline }
