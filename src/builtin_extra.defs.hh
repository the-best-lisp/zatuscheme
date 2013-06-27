// This file is intended to be included into an array of 'BuiltinFunc'

{"transcript-on", {
    builtin::transcript_on,
    {0}}},
{"transcript-off", {
    builtin::transcript_off,
    {0}}},
{"traditional-transformer", {
    builtin::traditional_transformer,
    {1}}},
{"gensym", {
    builtin::gensym,
    {0}}},
{"sc-macro-transformer", {
    builtin::sc_macro_transformer,
    {1}}},
{"make-syntactic-closure", {
    builtin::make_syntactic_closure,
    {3}}},
{"%current-environment", {
    builtin::internal_current_environment,
    {0}}},
{"identifier?", {
    builtin::identifierp,
    {1}}},
{"identifier=?", {
    builtin::identifier_eq,
    {4}}},
{"make-synthetic-identifier", {
    builtin::make_synthetic_identifier,
    {1}}},
{"with-exception-handler", {
    builtin::with_exception_handler,
    {2, 2, Passing::eval, Returning::pass, MoveReturnValue::f}}},
{"raise", {
    builtin::raise,
    {1, 1}}},
{"exit", {
    builtin::exit,
    {0, 1}}},
{"hard-repl", {
    builtin::hard_repl,
    {0}}},
{"tmp-file", {
    builtin::tmp_file,
    {0}}},
