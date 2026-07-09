/* data.h */
#ifndef DATA_H
#define DATA_H

/* 타입 태그 정의 */
typedef enum {
    T_NIL,
    T_INTEGER,
    T_SYMBOL,
    T_STRING,
    T_PAIR,
    T_BUILTIN,
    T_CLOSURE,
    T_MACRO
} ValueType;

typedef struct Value Value;

/* 내장 함수 포인터 타입 정의 */
typedef Value* (*BuiltinFunc)(Value*);

/* Value 구조체 정의 */
struct Value {
    ValueType type;
    union {
        int ival;           // T_INTEGER
        char* sval;         // T_SYMBOL, T_STRING
        BuiltinFunc fn;     // T_BUILTIN
        struct {            // T_PAIR
            Value* car;
            Value* cdr;
        } pair;
        struct {            // T_CLOSURE, T_MACRO
            Value* env;
            Value* params;
            Value* body;
        } clo;
    } as;
};

/* 전역 NIL 객체 */
extern Value *NIL;

/* 초기화 함수 */
void init_mowkow(void);

/* 생성자 함수들 */
Value* mkint(int v);
Value* mksym(char* s);
Value* mkstr(char* s);
Value* mkbuiltin(BuiltinFunc fn);
Value* cons(Value* car_val, Value* cdr_val);
Value* mkclosure(Value* env, Value* params, Value* body);
Value* mkmacro(Value* env, Value* params, Value* body);

/* 접근자 및 수정자 */
Value* car(Value* v);
Value* cdr(Value* v);
void setcar(Value* v, Value* new_car);
void setcdr(Value* v, Value* new_cdr);

/* 타입 확인 함수들 */
int is_nil(Value* v);
int is_pair(Value* v);
int is_symbol(Value* v);
int is_int(Value* v);
int is_str(Value* v);
int is_list(Value* v);

/* 유틸리티 함수 */
Value* cplist(Value* ls);
void print_value(Value* v);

#endif /* DATA_H */
