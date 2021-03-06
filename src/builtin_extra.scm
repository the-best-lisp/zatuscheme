LOAD
(define list* %list*)

LOAD
(define (transcript-on)
  (%transcript-set-state #t))
LOAD
(define (transcript-off)
  (%transcript-set-state #f))

LOAD
(define traditional-transformer %traditional-transformer)

LOAD
(define gensym %gensym)

LOAD
(define sc-macro-transformer %sc-macro-transformer)

LOAD
(define make-syntactic-closure %make-syntactic-closure)

LOAD
(define (rsc-macro-transformer fun)
  (sc-macro-transformer
   (lambda (body usage-env)
     (capture-syntactic-environment
      (lambda (trans-env)
        (make-syntactic-closure usage-env ()
                                (fun body trans-env)))))))

LOAD
(define (close-syntax form env)
  (make-syntactic-closure env () form))

LOAD
(define (capture-syntactic-environment proc)
  `(,eval (,proc (,%current-environment)) (,%current-environment)))

LOAD
(define identifier? %identifier?)
LOAD
(define identifier=? %identifier=?)

LOAD
(define (make-synthetic-identifier identifier)
  (make-syntactic-closure (%make-empty-environment) () identifier))

LOAD
(define raise %raise)

LOAD
(define (error reason . args)
  (raise (apply string-append reason args)))

LOAD
(define (with-exception-handler handler thunk)
  (%push-exception-handler handler)
  (%multiple-value-prog1
   (thunk)
   (%pop-exception-handler)))

LOAD
(define (ignore-errors thunk)
  (call-with-current-continuation
   (lambda (cont)
     (with-exception-handler (lambda (e) (cont #f e))
                             thunk))))

LOAD
(define-syntax guard
  (syntax-rules ()
    ((guard (var clause ...) e1 e2 ...)
     ((call-with-current-continuation
       (lambda (guard-k)
         (with-exception-handler
          (lambda (condition)
            ((call-with-current-continuation
              (lambda (handler-k)
                (guard-k
                 (lambda ()
                   (let ((var condition))
                     (guard-aux (handler-k (lambda ()
                                             (raise condition)))
                                clause ...))))))))
          (lambda ()
            (call-with-values
                (lambda () e1 e2 ...)
              (lambda args
                (guard-k (lambda ()
                           (apply values args)))))))))))))

LOAD
(define-syntax guard-aux
  (syntax-rules (else =>)
    ((guard-aux reraise (else result1 result2 ...))
     (begin result1 result2 ...))
    ((guard-aux reraise (test => result))
     (let ((temp test))
       (if temp 
           (result temp)
           reraise)))
    ((guard-aux reraise (test => result) clause1 clause2 ...)
     (let ((temp test))
       (if temp
           (result temp)
           (guard-aux reraise clause1 clause2 ...))))
    ((guard-aux reraise (test))
     test)
    ((guard-aux reraise (test) clause1 clause2 ...)
     (let ((temp test))
       (if temp
           temp
           (guard-aux reraise clause1 clause2 ...))))
    ((guard-aux reraise (test result1 result2 ...))
     (if test
         (begin result1 result2 ...)
         reraise))
    ((guard-aux reraise (test result1 result2 ...) clause1 clause2 ...)
     (if test
         (begin result1 result2 ...)
         (guard-aux reraise clause1 clause2 ...)))))

LOAD
(define open-input-string %open-input-string)

LOAD
(define open-output-string %open-output-string)

LOAD
(define get-output-string %get-output-string)

LOAD
(define-syntax cond-expand
  (syntax-rules (and or not else srfi-0 srfi-6 srfi-23 srfi-34)
    ((cond-expand) (error "Unfulfilled cond-expand"))
    ((cond-expand (else body ...))
     (begin body ...))
    ((cond-expand ((and) body ...) more-clauses ...)
     (begin body ...))
    ((cond-expand ((and req1 req2 ...) body ...) more-clauses ...)
     (cond-expand
       (req1
         (cond-expand
           ((and req2 ...) body ...)
           more-clauses ...))
       more-clauses ...))
    ((cond-expand ((or) body ...) more-clauses ...)
     (cond-expand more-clauses ...))
    ((cond-expand ((or req1 req2 ...) body ...) more-clauses ...)
     (cond-expand
       (req1
        (begin body ...))
       (else
        (cond-expand
           ((or req2 ...) body ...)
           more-clauses ...))))
    ((cond-expand ((not req) body ...) more-clauses ...)
     (cond-expand
       (req
         (cond-expand more-clauses ...))
       (else body ...)))
    ((cond-expand (srfi-0 body ...) more-clauses ...)
       (begin body ...))
    ((cond-expand (srfi-6 body ...) more-clauses ...)
       (begin body ...))
    ((cond-expand (srfi-23 body ...) more-clauses ...)
       (begin body ...))
    ((cond-expand (srfi-34 body ...) more-clauses ...)
       (begin body ...))
    ((cond-expand (feature-id body ...) more-clauses ...)
       (begin (cond-expand more-clauses ...)))))

LOAD
(define (read-eval-print-loop)
  (define read-obj #f)
  (let repl-loop ()
    (display ">> ")
    (set! read-obj (read))
    (if (eof-object? read-obj) #t
        (begin
          (call-with-current-continuation
           (lambda (c)
             (with-exception-handler
              (lambda (e) (display e) (c #f))
              (lambda () (display (eval read-obj (interaction-environment)))))))
          (newline)
          (repl-loop)))))

LOAD
(define exit %exit)

LOAD
(define tmp-file %tmp-file)
