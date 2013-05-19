// This file is intended to be included into an array of 'const char*'

"(define (string-copy str)"
"  (let* ((size (string-length str))"
"         (str2 (make-string size)))"
"    (do ((i 0 (+ i 1)))"
"        ((= i size) str2)"
"      (string-set! str2 i (string-ref str i)))))",

"(define (string-fill! str fill)"
"  (let ((size (string-length str)))"
"    (do ((i 0 (+ i 1)))"
"        ((= i size) str)"
"      (string-set! str i fill))))",