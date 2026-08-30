/* eval.h */
#ifndef EVAL_H
#define EVAL_H

#include "data.h"

extern Value* global_env;

Value* mkenv(Value* parent);
Value* envget(Value* env, Value* symbol);
void envset(Value* env, Value* symbol, Value* val);

Value* mk_eval(Value* expr, Value* env);
Value* apply(Value* fn, Value* args);
void init_eval(void);

int check_equality(Value* a, Value* b);
void print_env_scope(Value* env);

Value* builtin_car(Value* args);
Value* builtin_cdr(Value* args);
Value* builtin_cons(Value* args);
Value* builtin_add(Value* args);
Value* builtin_sub(Value* args);
Value* builtin_mul(Value* args);
Value* builtin_div(Value* args);

Value* builtin_inteq(Value* args);
Value* builtin_intlt(Value* args);
Value* builtin_intgt(Value* args);

Value* builtin_eq(Value* args);
Value* builtin_ispair(Value* args);
Value* builtin_isnil(Value* args);
Value* builtin_not(Value* args);
Value* builtin_and(Value* args);
Value* builtin_or(Value* args);

Value* builtin_apply(Value* args);
Value* builtin_print_env(Value* args);

#endif /* EVAL_H */
