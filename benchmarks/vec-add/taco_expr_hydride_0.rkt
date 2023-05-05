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

(current-bitwidth 512)
(custodian-limit-memory (current-custodian) (* 20000 1024 1024))

;; Creating a map between buffers and halide call node arguments
(define id-map (make-hash))

(define halide-expr 
	(vec-add (<load> (<prop> ) reg_0) (<load> (<prop> ) reg_0))
)

(clear-vc!)

(define synth-res (synthesize-halide-expr halide-expr id-map 3 1 'z3 #t #f "" "" "x86"))
(dump-synth-res-with-typeinfo synth-res id-map)

;; Translate synthesized hydride-expression into LLVM-IR
(compile-to-llvm synth-res id-map "hydride_node_hydride_0" "hydride")

(save-synth-map "/tmp/hydride_hash_hydride_0.rkt" "synth_hash_hydride_0" synth-log)
