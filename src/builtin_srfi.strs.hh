// This file is intended to be included into an array of 'const char*'

"(define (ignore-errors thunk)"
"  (call-with-current-continuation"
"    (lambda (cont)"
"      (with-exception-handler (lambda (e) (cont #f e))"
"        thunk))))",                       

// from SRFI-34 ( http://srfi.schemers.org/srfi-34/srfi-34.html )
"(define-syntax guard"
"  (syntax-rules ()"
"    ((guard (var clause ...) e1 e2 ...)"
"     ((call-with-current-continuation"
"       (lambda (guard-k)"
"         (with-exception-handler"
"          (lambda (condition)"
"            ((call-with-current-continuation"
"               (lambda (handler-k)"
"                 (guard-k"
"                  (lambda ()"
"                    (let ((var condition))"
"                      (guard-aux (handler-k (lambda ()"
"                                              (raise condition)))"
"                                 clause ...))))))))"
"          (lambda ()"
"            (call-with-values"
"             (lambda () e1 e2 ...)"
"             (lambda args"
"               (guard-k (lambda ()"
"                          (apply values args)))))))))))))",

"(define-syntax guard-aux"
"  (syntax-rules (else =>)"
"    ((guard-aux reraise (else result1 result2 ...))"
"     (begin result1 result2 ...))"
"    ((guard-aux reraise (test => result))"
"     (let ((temp test))"
"       (if temp "
"           (result temp)"
"           reraise)))"
"    ((guard-aux reraise (test => result) clause1 clause2 ...)"
"     (let ((temp test))"
"       (if temp"
"           (result temp)"
"           (guard-aux reraise clause1 clause2 ...))))"
"    ((guard-aux reraise (test))"
"     test)"
"    ((guard-aux reraise (test) clause1 clause2 ...)"
"     (let ((temp test))"
"       (if temp"
"           temp"
"           (guard-aux reraise clause1 clause2 ...))))"
"    ((guard-aux reraise (test result1 result2 ...))"
"     (if test"
"         (begin result1 result2 ...)"
"         reraise))"
"    ((guard-aux reraise (test result1 result2 ...) clause1 clause2 ...)"
"     (if test"
"         (begin result1 result2 ...)"
"         (guard-aux reraise clause1 clause2 ...)))))",

