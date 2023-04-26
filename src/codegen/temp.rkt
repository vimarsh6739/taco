#lang rosette
(require rosette/lib/synthax)
(require rosette/lib/angelic)
(require racket/pretty)
(require data/bit-vector)
(require rosette/lib/destruct)
(require rosette/solver/smt/boolector)
(require hydride)


;; Uncomment the line below to enable verbose logging
(enable-debug)

(current-bitwidth 16)
(custodian-limit-memory (current-custodian) (* 20000 1024 1024))
(define reg_1_bitvector (bv 0 (bitvector 512)))
(define reg_1 (halide:create-buffer reg_1_bitvector 'uint8))
(define reg_0_bitvector (bv 0 (bitvector 512)))
(define reg_0 (halide:create-buffer reg_0_bitvector 'uint8))

; Creating a map between buffers and halide call node arguments
(define id-map (make-hash))
(hash-set! id-map reg_1 (bv 1 (bitvector 8)))
(hash-set! id-map reg_0 (bv 0 (bitvector 8)))

(define halide-expr 
  (vec-max
    reg_0
    reg_1)
  )

(clear-vc!)
;; Certain solvers do not support direct optimization, and hence we iteratively optimize by progressively tightening cost
;(set-iterative-optimize)

(define synth-res (synthesize-halide-expr halide-expr id-map 1 64 'z3 #t #f  ""  ""  "x86"))
(dump-synth-res-with-typeinfo synth-res id-map)
; Translate synthesized hydride-expression into LLVM-IR
(compile-to-llvm synth-res id-map "hydride.node.max_pool_x86_depth1_rafae.0" "max_pool_x86_depth1_rafae")
(save-synth-map "/tmp/hydride_hash_max_pool_x86_depth1_rafae_0.rkt" "synth_hash_max_pool_x86_depth1_rafae_0" synth-log)
