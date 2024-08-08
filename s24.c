
#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>

#define max_token_len 256
#define token_stride max_token_len
#define max_tokens 300
#define max_stack 1000
#define max_vars 100
#define program_max_tokens tokens + token_count*max_token_len


typedef enum {
    value_null, constant, operation, nest, array, character
} value_type;

typedef struct value {
    value_type type;
    union {
        double constant;
        char c;
        struct value* array;
        char* nest;
    } data;
    int size;
    bool auto_exec;
} value;


int stack_size = 0;
value stack[max_stack];
int stack_print_limit = 12;

int token_count = 0;
char tokens[max_token_len * max_tokens];

int var_count = 0;
char* var_names[max_vars];
value var_data[max_vars];

void execute(char*, int);



bool is_unit(value array) {
    return array.size == 1;
}

bool is_unit_constant(value array) {
    return array.size == 1 && array.data.array[0].type == constant;
}

bool is_constant(value array) {
    return array.type == constant;
}

bool is_char(value array) {
    return array.type == character;
}

bool is_string(value array) {
    return array.size > 0 && array.data.array[0].type == character;
}

char* get_string(value string) {
    assert(is_string(string));

    char* ret = malloc(sizeof(char) * string.size + 1);
    for (int i = 0; i < string.size; i++){
        ret[i] = string.data.array[i].data.c;
    }
    ret[string.size] = '\0';
    return ret;
}

#define max(x, y) x > y ? x : y
#define min(x, y) x < y ? x : y

char* get_string_slice(value string, int start, int end) {
    assert(is_string(string));
    assert(end >= start);

    end = min(string.size, end);

    int size = end - start;

    char* ret = malloc(sizeof(char) * size + 1);
    for (int i = 0; i < size; i++){
        ret[i] = string.data.array[i + start].data.c;
    }
    ret[size] = '\0';
    return ret;
}

bool is_array(value v) {
    return v.type == array;
}

value array_at(value array, int at) {
    assert(is_array(array));
    assert(at >= 0 && at < array.size);
    // assert(array.data.array[at].type == constant); // porque?

    return array.data.array[at];
}

// gracias gpt
int read_file_to_string(const char *filename, char** out) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open file\n");
        exit(1);
    }

    // Seek to the end of the file to get the file size
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    // Allocate memory for the file content
    char* buffer = malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        exit(1);
    }

    // Read the file content into the buffer
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != fileSize) {
        fprintf(stderr, "Failed to read file\n");
        free(buffer);
        fclose(file);
        exit(1);
    }

    // Null-terminate the string
    buffer[fileSize] = '\0';

    fclose(file);
    *out = buffer;
    return fileSize;
}




char* pretty_value(value v) {
    char* ret = malloc(sizeof(char) * 1000); // dane-se
    switch (v.type) {
        case character:
            sprintf(ret, "%c", v.data.c);
            // sprintf(ret, " %d ", v.data.c);
            break;
        case constant:
            sprintf(ret, fmodl(v.data.constant, 1) == 0 ? "%.0lf" : "%.2lf", v.data.constant);
            break;
        case array:

            if (v.size > 0 && v.data.array[0].type == character) {
                free(ret); // fixme
                ret = malloc(sizeof(char) * v.size + 3); // fixme
                ret[0] = '"';
                for (int i = 0; i < v.size; i++){
                    ret[i+1] = v.data.array[i].data.c;
                }
                ret[v.size + 1] = '"';
                ret[v.size + 2] = '\0';
                return ret;
            }
            if (is_unit_constant(v)) {
                free(ret);
                return pretty_value(array_at(v, 0));
            }

            ret[0] = '(';
            ret[1] = '(';
            ret[2] = '\0';
            for (int i = 0; i < v.size; i++) {
                strcat(ret, " ");
                strcat(ret, pretty_value(v.data.array[i]));
            }
            strcat(ret, " ))");
            break;
        case nest:
            ret[0] = '[';
            ret[1] = '\0';
            for (int i = 0; i < v.size; i++) {
                strcat(ret, " ");
                strcat(ret, v.data.nest + i * token_stride);
            }
            strcat(ret, " ]");
            break;
        default:
            sprintf(ret, "?value %d?", v.type);
            break;
    }
    return ret;
}

void print_pretty_value(value v) {
    char* pp = pretty_value(v);
    printf("%s\n", pp);
    free(pp);
    // fflush(stdout);
}

char* value_repr(value v) {
    char* ret = malloc(sizeof(char) * 300);
    switch (v.type) {
        case constant:
            sprintf(ret, "( constant ) %lf", v.data.constant);
            break;
        case array:
            {
                char* pp = pretty_value(v);
                sprintf(ret, is_unit_constant(v) ? "( unit ) %s" : "( array ) %s", pp);
                free(pp);
            }
            break;
        case nest:
            {
                char* pp = pretty_value(v);
                sprintf(ret, "( nest ) %s", pp);
                free(pp);
            }
            break;
        // case operation:
        //     sprintf(ret, "( operation ) %d", v.data.operation);
        //     break;
        default:
            assert(8 && "default?");
            break;
    }
    return ret;
}

void print_vars() {
    for (int i = 0; i < var_count; i++) {

        char* value_string = value_repr(var_data[i]);
        printf("\"%s\": %s\n", var_names[i], value_string);

        free(value_string);
    }
}

void print_stack() {
    int limit = stack_size < stack_print_limit ?  0 : stack_size - stack_print_limit;;

    for (int i = stack_size-1; i >= limit; i--) {

        value v = stack[i];

        char* value_string = value_repr(v);

        printf("%d: %s\n", i, value_string);
        printf("========================================\n");

        free(value_string);
    }
}

void push(value v) {
    if (stack_size >= max_stack) {
        fprintf(stderr, "error: max stack achieved (%d)!\n", max_stack);
        print_stack();
        exit(1);
    }
    stack[stack_size++] = v;
}

value pop() {
    if (stack_size <= 0) {
        fprintf(stderr, "error: ran out of stack! (%d)\n", stack_size);
        exit(1);
    }
    return stack[--stack_size];
}

value peek() {
    if (stack_size <= 0) {
        fprintf(stderr, "error: ran out of stack! (%d)\n", stack_size);
        exit(1);
    }
    return stack[stack_size-1];
}

int find_var(char* token) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(token, var_names[i]) == 0) {
            return i;
        }
    return -1; // not found
}

value new_character(char c) {
    return (value) {
        .type = character,
        .data.c = c
    };
}

value new_constant(double c) {
    return (value) {
        .type = constant,
        .data.constant = c
    };
}

value string_to_constant(value string) {
    double number;
    char* s = get_string(string);
    if (sscanf(s, "%lf", &number) == 0) {
        fprintf(stderr, "error: failure at converting number!\n");
        exit(1);
    }
    free(s);
    return new_constant(number);
}


double get_constant(value c) {
    assert(c.type == constant);

    return c.data.constant;
}

value new_nest() {
    return (value) { 
        .type = nest, 
        .data.nest = NULL,
        .size = 0,
    };
}

value new_array(int size) {
    value new = (value) { 
        .type = array, 
        .data.array = malloc(sizeof(value) * size),
        .size = size,
    };
    return new;
}

void array_append(value* array, value x) {
    array->data.array = realloc(array->data.array , sizeof(value) * ++array->size);
    array->data.array[array->size-1] = x;
}

void nest_append(value* n, char* tok) {
    assert(n->type == nest);
    n->data.nest = realloc(n->data.nest , max_token_len * ++n->size);
    strcpy(n->data.nest + (n->size-1)*token_stride, tok);
}

value new_unit(double c) {
    value arr = new_array(0);
    array_append(&arr, new_constant(c));
    return arr;
}


double get_unit(value array) {
    assert(is_unit_constant(array));

    return array.data.array[0].data.constant;
}

double get_unit_constant(value array) {
    assert(is_unit_constant(array));

    return array.data.array[0].data.constant;
}

value coerce_unit_to_const(value v)
{
    if (is_unit_constant(v))
        return array_at(v, 0);

    return v;
}

value coerce_const_to_unit(value c) {
    if (c.type != constant)
        return c;

    value arr = new_array(0);
    array_append(&arr, c);
    return arr;
}

void free_value(value v) {
    switch (v.type) {

        case array:

            for (int i = 0; i < v.size; i++) {
                free_value(v.data.array[i]);
            }
            free(v.data.array);
            break;

        case nest:

            free(v.data.nest);
            break;

        default:
            break;
    }
}



// --------------------------- //
// ----     built-ins    ----- //
// --------------------------- //

// binary built-ins

#define binary_iter(lvalue)                                 \
bool map_val1 = !is_unit_constant(val1), map_val2 = !is_unit_constant(val2);  \
int size = max(val1.size, val2.size);                       \
value arr = new_array(size);                                \
for (int i = 0; i < size; i++) {                            \
    double result = lvalue;                                 \
    arr.data.array[i] = new_constant(result);               \
}

#define binary_map_op(op)                                   \
binary_iter(                                                \
        val1.data.array[map_val1 ? i : 0].data.constant     \
        op                                                  \
        val2.data.array[map_val2 ? i : 0].data.constant;    \
)

#define binary_map_func(func)                               \
binary_iter(                                                \
func(                                                       \
     val1.data.array[map_val1 ? i : 0].data.constant,       \
     val2.data.array[map_val2 ? i : 0].data.constant)       \
)

#define array_iter(arr, idx, lvalue) \
for (int i = 0; i < arr.size; i++) { \
    arr.data.array[idx] = lvalue; \
}

value sum() {
    value val1 = pop(), val2 = pop();
    binary_map_op(+);
    return arr;
}

value subtraction() {
    value val2 = pop(), val1 = pop();
    binary_map_op(-);
    return arr;
}

value multiplication() {
    value val1 = pop(), val2 = pop();
    binary_map_op(*);
    return arr;
}

value division() {
    value val2 = pop(), val1 = pop();
    binary_map_op(/);
    return arr;
}

value power() {
    value val2 = pop(), val1 = pop();
    binary_map_func(powl);
    return arr;
}

value mod() {
    value val2 = pop(), val1 = pop();
    binary_map_func(fmodl);
    return arr;
}

value __equal(value v1, value v2) {

    if (is_constant(v1) && is_constant(v2)) {
        return new_constant(get_constant(v1) == get_constant(v2));
    }
    else if (is_char(v1) && is_char(v2)) {
        return new_constant(v1.data.c == v2.data.c);
    }
    else if (is_string(v1) && is_string(v2)) {
        if (v1.size != v2.size) 
            return new_constant(false);

        for (int i = 0; i < v1.size; i++) {
            if (array_at(v1, i).data.c != array_at(v2, i).data.c) {
                return new_constant(false);
            }
        }
        return new_constant(true);
    }

    if (is_constant(v1) && is_string(v2)) {
        return __equal(v1, string_to_constant(v2));
    }
    else if (is_string(v1) && is_constant(v2)) {
        return __equal(string_to_constant(v1), v2);
    }

    fprintf(stderr, "error: anything but here!\n");
    exit(1);
}

value __nequal(value v1, value v2) {
    return new_constant(!get_constant(__equal(v1, v2)));
}

#define def_binary_op2(name, func)\
value name() { \
    value val1 = pop(), val2 = pop(); \
    bool map_val1 = !is_unit(val1), map_val2 = !is_unit(val2); \
    int size = max(val1.size, val2.size); \
    value arr = new_array(size); \
    for (int i = 0; i < size; i++) { \
        arr.data.array[i] = func( \
            array_at(val1, map_val1 ? i : 0), \
            array_at(val2, map_val2 ? i : 0) \
        ); \
    } \
    return arr; \
}

// value equal() {
//     value val1 = pop(), val2 = pop();
//
//     bool map_val1 = !is_unit(val1), map_val2 = !is_unit(val2);
//     int size = max(val1.size, val2.size);
//     value arr = new_array(size);
//
//     for (int i = 0; i < size; i++) {
//         
//         arr.data.array[i] = __equal(
//             array_at(val1, map_val1 ? i : 0),
//             array_at(val2, map_val2 ? i : 0)
//         );
//     }
//
//     return arr;
// }

value __or(value v1, value v2) {

    if (is_constant(v1) && is_constant(v2)) {
        return new_constant(get_constant(v1) || get_constant(v2));
    }
    else if (is_char(v1) && is_char(v2)) {
        return new_constant(v1.data.c || v2.data.c);
    }
    if (is_constant(v1) && is_string(v2)) {
        return __equal(v1, string_to_constant(v2));
    }
    else if (is_string(v1) && is_constant(v2)) {
        return __equal(string_to_constant(v1), v2);
    }

    return new_constant(true);
}

// value or() {
//     value val1 = pop(), val2 = pop();
//
//     bool map_val1 = !is_unit(val1), map_val2 = !is_unit(val2);
//     int size = max(val1.size, val2.size);
//     value arr = new_array(size);
//
//     for (int i = 0; i < size; i++) {
//         
//         arr.data.array[i] = __or(
//             array_at(val1, map_val1 ? i : 0),
//             array_at(val2, map_val2 ? i : 0)
//         );
//     }
//
//     return arr;
// }

def_binary_op2(equal, __equal);
def_binary_op2(or, __or);
def_binary_op2(not_equal, __nequal);










// unary built-ins

#define unary_iter(lvalue)                    \
int size = val.size;                          \
value arr = new_array(size);                  \
for (int i = 0; i < size; i++) {              \
    double result = lvalue;                   \
    arr.data.array[i] = new_constant(result); \
}

#define unary_map_func(func)            \
unary_iter(                             \
func(val.data.array[i].data.constant)   \
)

#define def_simple_unary_func_op(name, func) \
value name() {            \
    value val = pop();      \
    unary_map_func(func); \
    return arr;             \
}

double __is_prime(double x) {
    if (x <= 1)
        return false;
    double sqroot = sqrtl(x);
    bool is_prime = true;
    for (double i = 2; i <= sqroot; i++) {
        if (fmodl(x, i) == 0) {
            is_prime = false;
            break;
        }
    }
    return is_prime;
}
double __gt0(double x) { return x > 0; }
double __lt0(double x) { return x < 0; }
double __not(double x) { return !x; }

def_simple_unary_func_op(   _round,         roundl  );
def_simple_unary_func_op(    is_prime,    __is_prime);
def_simple_unary_func_op(   _abs,           fabsl   );
def_simple_unary_func_op(   _cos,           cos     );
def_simple_unary_func_op(   _sin,           sin     );
def_simple_unary_func_op(    gt0,         __gt0     );
def_simple_unary_func_op(    lt0,         __lt0     );
def_simple_unary_func_op(    not,         __not     );

// special


value list() {
    value take = pop();
    int size = get_unit(take);

    value arr = new_array(size == -1 ? stack_size : size);

    array_iter(
        arr, 
        arr.size - i - 1,
        coerce_unit_to_const(pop())
    );

    return arr;
}
void clear() {
    int ss = stack_size;
    for (int i = 0; i < ss; i++)
        pop();
}

value range() {
    value to = pop(), from = pop();

    int size = get_unit(to) - get_unit(from);

    value arr = new_array(size);

    array_iter(arr, i, new_constant(get_unit(from) + i));

    return arr;
}

value range1() {
    value to = pop();
    int size = get_unit(to);
    value arr = new_array(size);

    array_iter(arr, i, new_constant(i))

    return arr;
}

void swap() {
    value first = pop(), second = pop();
    push(first);
    push(second);
}

value mask() {
    value mask = pop(), filter = pop();

    if (mask.size != filter.size) {
        fprintf(stderr, "error: mask and array have different sizes!\n");
        exit(1);
    }
    value arr = new_array(0);
    for (int i = 0; i < mask.size; i++) {

        if (get_constant(array_at(mask, i)) == 1.0) {
            array_append(&arr, array_at(filter, i));
        }
    }

    return arr;
}

value _index() {
    value mask = pop();

    value arr = new_array(0);
    for (int i = 0; i < mask.size; i++) {

        if (get_constant(array_at(mask, i)) == 1.0) {
            array_append(&arr, new_constant(i));
        }
    }

    return arr;
}

value _index2() {
    value to_search = pop(), list = pop();

    value arr = new_array(0);

    for (int i = 0; i < to_search.size; i++) {

        bool found = false;
        for (int j = 0; j < list.size; j++)  {

            if (get_constant(array_at(to_search, i)) == get_constant(array_at(list, j)))  {
                array_append(&arr, new_constant(j));
                found = true;
                break;
            }
        }
        if (!found)
            array_append(&arr, new_constant(-1));
    }

    return arr;
}

value reverse() {
    value arr = pop(), new = new_array(arr.size);

    assert(arr.type == array);

    for (int i = 0; i < arr.size; i++){
        new.data.array[arr.size - i - 1] = array_at(arr, i);
    }

    return new;
}

value at() {
    value at = pop(), arr = pop();
    return array_at(arr, get_unit(at));
}

void reduce_left() {
    value nested_op = pop(), arr = pop();

    if (nested_op.type != nest) {
        fprintf(stderr, "error: can only apply nested operations to arrays!!\n");
        print_pretty_value(nested_op);
        exit(1);
    }
    if (arr.type != array) {
        fprintf(stderr, "error: can't reduce what's not an array!\n");
        print_pretty_value(arr);
        exit(1);
    }

    push(coerce_const_to_unit(array_at(arr, 0)));
    for (int i = 1; i < arr.size; i++) {
        push(coerce_const_to_unit(array_at(arr, i)));
        execute(nested_op.data.nest, nested_op.size);
    }
}

value accumulate_left() {
    value nested_op = pop(), arr = pop();

    if (nested_op.type != nest) {
        fprintf(stderr, "error: can only apply nested operations to arrays!!\n");
        print_pretty_value(nested_op);
        exit(1);
    }
    if (arr.type != array) {
        fprintf(stderr, "error: can't reduce what's not an array!\n");
        print_pretty_value(arr);
        exit(1);
    }

    value acc = new_array(0);
    push(coerce_const_to_unit(array_at(arr, 0)));
    for (int i = 1; i < arr.size; i++) {
        push(coerce_const_to_unit(array_at(arr, i)));
        execute(nested_op.data.nest, nested_op.size);
        array_append(&acc, peek());
    }
    pop();
    return acc;
}





void execute(char* start, int amount) {
    char* last;
    int parens;

rewind:
    last = start + amount*token_stride;
    parens = 0;

    for (char* current = start; current < last; current += token_stride) {


        if (strcmp(current, "((" /*))*/) == 0 || strcmp(current, /*((*/"))") == 0) {
            continue;
        }

        if (strcmp(current, "(" /*)*/) == 0) {
            parens++;
            continue;
        }
        if (strcmp(current,  /*(*/")") == 0) {
            if (parens == 0) {
                fprintf(stderr, "error: parens mismatch!\n");
            exit(1);
            }
            parens--;
            continue;
        }
        if (parens > 0) 
            continue;

        if (parens < 0)  {
            fprintf(stderr, "error: parens mismatch!\n");
            exit(1);
        }

        if (*current == '"') {

            value string = new_array(0);

            char* i = current + 1;
            char* t = current;

            for (; i < last; i++) {

                if (*i == '\0' || (*i == '\\' && *(i+1) == '\0')){
                    array_append(&string, new_character(' '));
                    t += token_stride;
                    i = t-1;
                }
                else if (*i == '"'){
                    assert(i == t + strlen(t) - 1);
                    break;
                } else {
                    array_append(&string, new_character(*i));
                }
            }
            // unit string?
            value r = new_array(0);
            array_append(&r, string);
            push(r);
            current = t;
            continue;
        }

        if (strcmp(current, "[" /*]*/) == 0) {
            value new_nest = (value) { 
                .type = nest,
                .data.nest = NULL,
                .size = 0,
            };
            int brackets = 1;
            char* i = current + token_stride;
            for (; i < last; i += max_token_len) {
                if (strcmp(i, "[" /*]*/ ) == 0) {
                    brackets++;
                }
                if (strcmp(i, /*[*/ "]") == 0) {
                    brackets--;
                }
                if (brackets == 0)
                    break;

                new_nest.data.nest = realloc(new_nest.data.nest , max_token_len * ++new_nest.size);
                strcpy(new_nest.data.nest + (new_nest.size-1)*token_stride, i);
            }
            if (i == program_max_tokens) {
                fprintf(stderr, "error: unmatched nesting!\n");
                exit(1);
            }

            current = i;

            push(new_nest);
            continue;
        }


        double number;
        if (sscanf(current, "%lf", &number) == 1) {
            value new_array = (value) { 
                .type = array, 
                .data.array = malloc(sizeof(value)),
                .size = 1,

            };
            new_array.data.array[0] = (value) {
                .type = constant,
                .data.constant = number,
            };
            push(new_array);
            continue;
        }

        if (current[0] == '.') {
            continue;
        }


        if      (strcmp(current, "nop") == 0)
        {
            continue;
        }

        else if (strcmp(current, "pop") == 0)
        {
            pop();
        }

        else if (strcmp(current, "+") == 0)
        {
            push(sum());
        }

        else if (strcmp(current, "-") == 0)
        {
            push(subtraction());
        }

        else if (strcmp(current, "*") == 0)
        {
            push(multiplication());
        }

        else if (strcmp(current, "/") == 0)
        {
            push(division());
        }

        else if (strcmp(current, "=") == 0)
        {
            push(equal());
        }

        else if (strcmp(current, "!=") == 0)
        {
            push(not_equal());
        }

        else if (strcmp(current, "or") == 0)
        {
            push(or());
        }

        else if (strcmp(current, "->")  == 0 || strcmp(current, "!->")  == 0)
        {
            value assign = pop(), var;

            char* name = current + token_stride;
            int idx;

            if ((idx = find_var(name)) >= 0)
            {
                free_value(var_data[idx]);
            }
            else {
                idx = var_count++;
            }

            if (current[0] == '!')
                assign.auto_exec = true;

            var_names[idx] = name;
            var_data[idx] = assign;
            current = name; // proxima iteração pula o nome da variável
        }

        else if (strcmp(current, "?") == 0)
        {
            value check = pop();

            if (get_unit_constant(check)) {
                char* goto_label = current += token_stride;

                if (goto_label[0] != '.')
                    continue;

                if (goto_label[0] == '.' && strlen(goto_label) == 1) {
                    fprintf(stderr, "error: goto label \"%s\" is not a label!\n", goto_label);
                    exit(1);
                }

                for (char* i = start; i < last; i += token_stride) {
                    if (i[0] != '.')
                        continue;
                    if (i != goto_label && strcmp(goto_label, i) == 0) {
                        current = i;
                        goto force_continue;
                    }
                }

                fprintf(stderr, "error: couldn't find label \"%s\"!\n", goto_label);
                exit(1);
            }
            else {
                char* else_label = current + token_stride*2;

                if (else_label[0] != '.') {
                    current += token_stride;
                    continue;
                }

                if (else_label[0] == '.' && strlen(else_label) == 1) {
                    fprintf(stderr, "error: goto label \"%s\" is not a label!\n", else_label);
                    exit(1);
                }

                for (char* i = start; i < last; i += token_stride) {
                    if (i[0] != '.')
                        continue;
                    if (i != else_label && strcmp(else_label, i) == 0) {
                        current = i;
                        goto force_continue;
                    }
                }

                fprintf(stderr, "error: couldn't find label \"%s\"!\n", else_label);
                exit(1);
            }
        }

        else if (strcmp(current, ";") == 0) {
            for (char* i = current; i < last; i += token_stride) {
                // printf("op: %s cmp: %d\n", i, strcmp(i, ".end"));
                if (strcmp(i, ".end") == 0) {
                    current = i;
                    goto force_continue;
                }
            }
            fprintf(stderr, "error: couldn't find .end label!\n");
            exit(1);
        }

        else if (strlen(current) > 1 && current[0] == '#')
        {
            int number;
            if (sscanf(current+1, "%d", &number) == 0){
                fprintf(stderr, "error: failed parsing number (%s)!\n", current+1);
                exit(1);
            };
            push(stack[stack_size - number - 1]);

        }

        else if (strcmp(current, "#") == 0)
        {
            push(new_unit(peek().size));
        }

        else if (strcmp(current, "at") == 0)
        {
            push(at());
        }

        else if (strcmp(current, "lst") == 0)
        {
            push(new_unit(peek().size));
            push(new_unit(-1));
            push(sum());
            push(at());
        }

        else if (strcmp(current, "abs") == 0)
        {
            push(_abs());
        }

        else if (strcmp(current, "pp")  == 0) 
        {
            print_pretty_value(peek());
        }

        else if (strcmp(current, "nl")  == 0) 
        {
            printf("\n");
        }

        else if (strcmp(current, "fmt")  == 0) 
        {
            value string = pop();
            print_pretty_value(string);
        }

        else if (strcmp(current, "ps")  == 0) 
        {
            print_stack();
        }

        else if (strcmp(current, "pv")  == 0) 
        {
            print_vars();
        }

        else if (strcmp(current, "idx")  == 0) 
        {
            push(_index());
        }

        else if (strcmp(current, "idx2")  == 0) 
        {
            push(_index2());
        }

        else if (strcmp(current, "dup") == 0) 
        {
            push(peek());
        }

        else if (strcmp(current, "mod") == 0)
        {
            push(mod());
        }

        else if (strcmp(current, "a")   == 0)
        {
            push(list());
        }

        else if (strcmp(current, "++")   == 0)
        {
            push(new_unit(1));
            push(sum());
        }

        else if (strcmp(current, "--")   == 0)
        {
            push(new_unit(-1));
            push(sum());
        }

        else if (strcmp(current, "**")   == 0)
        {
            push(power());
        }

        else if (strcmp(current, "rou") == 0)
        {
            push(_round());
        }

        else if (strcmp(current, "msk") == 0)
        {
            push(mask());
        }

        else if (strcmp(current, "rdl")  == 0) 
        {
            reduce_left();
        }

        else if (strcmp(current, "acc")  == 0) 
        {
            push(accumulate_left());
        }

        else if (strcmp(current, "del") == 0)
        {
            push(not());
            push(mask());
        }

        else if (strcmp(current, "rev") == 0)
        {
            push(reverse());
        }

        // else if (strcmp(current, "neg") == 0)
        // {
        //     push(negative());
        // }

        else if (strcmp(current, "clr") == 0)
        {
            clear();
        }

        else if (strcmp(current, "ipr") == 0)
        {
            push(is_prime());
        }

        else if (strcmp(current, "ran") == 0)
        {
            push(range());
        }

        else if (strcmp(current, "ran1") == 0)
        {
            push(range1());
        }

        else if (strcmp(current, "x") == 0)
        {
            value nesting = pop();
            if (nesting.type != nest) {
                fprintf(stderr, "error: can't execute what is not a nest!\n");
                print_pretty_value(nesting);
                exit(1);
            }
            execute(nesting.data.nest, nesting.size);
        }

        else if (strcmp(current, "rx") == 0)
        {
            value nesting = pop();
            if (nesting.type != nest) {
                fprintf(stderr, "error: can't execute what is not a nest!\n");
                print_pretty_value(nesting);
                exit(1);
            }

            if (nesting.data.nest != start) {
                fprintf(stderr, "error: can't rewind a nest outside of itself! "
                        "try using the 'x' command!\n");
                print_pretty_value(nesting);
                exit(1);
            }
            // execute(nesting.data.nest, nesting.size);

            start = nesting.data.nest;
            amount = nesting.size;
            goto rewind;
        }

        else if (strcmp(current, ".") == 0)
        {
            value arr = pop();
            if (arr.type != array) {
                fprintf(stderr, "error: can't unbind what is not an array!\n");
                print_pretty_value(arr);
                exit(1);
            }
            for (int i = 0; i < arr.size; i++) {
                push(coerce_const_to_unit(array_at(arr, i)));
            }
        }

        else if (strcmp(current, "swp") == 0)
        {
            swap();
        }

        else if (strcmp(current, "ld") == 0)
        {
            value string = pop();

            string = array_at(string, 0); // fixme

            char* builder = malloc(sizeof(char) * string.size + 1);
            for (int i = 0; i < string.size; i++) {
                builder[i] = array_at(string, i).data.c;
            }
            builder[string.size] = '\0';

            char* file;
            int file_size = read_file_to_string(builder, &file);

            value new_string = new_array(0);

            for (int i = 0; i < file_size; i++) {
                array_append(&new_string, new_character(file[i]));
            }

            push(new_string);
        }

        else if (strcmp(current, "sws") == 0)
        {
            value string = pop();

            value r = new_array(0);
            value b = new_array(0);

            for (int i = 0; i < string.size; i++) {
                value at = array_at(string, i);
                switch (at.data.c) {
                    case ' ':
                    case '\n':
                    case '\t':
                        if (b.size > 0)  {
                            array_append(&r, b);
                            b = new_array(0);
                        }
                        break;
                    default:
                        array_append(&b, at);
                        break;
                }
            }
            if (b.size > 0)
                array_append(&r, b);

            push(r);
        }

        else if (strcmp(current, "ss") == 0)
        {
            char* delimiter = get_string(pop());

            value string = pop();
            string = array_at(string, 0);

            int delimiter_len = strlen(delimiter);
            value r = new_array(0);
            value b = new_array(0);

            for (int i = 0; i < string.size; i++) {
                char* s = get_string_slice(string, i, i+delimiter_len);
                if (strcmp(s, delimiter) == 0) {

                }
                free(s);
            }
            if (b.size > 0)
                array_append(&r, b);

            push(r);
            free(delimiter);
        }

        else if (strcmp(current, "a2n") == 0)
        {
            value arr = pop();
            if (is_string(arr)) {
                value n = new_array(0);
                array_append(&n, arr);
                arr = n;
            }

            value r = new_array(0);

            for (int i = 0; i < arr.size; i++) {

                double number;
                char* s = get_string(array_at(arr, i));
                if (sscanf(s, "%lf", &number) == 0) {
                    fprintf(stderr, "error: failure at converting number!\n");
                    exit(1);
                }
                free(s);

                array_append(&r, new_constant(number));
            }
            push(r);
        }

        else if (strcmp(current, ">0") == 0)
        {
            push(gt0());
        }

        else if (strcmp(current, "<0") == 0)
        {
            push(lt0());
        }

        else if (strcmp(current, "cos") == 0)
        {
            push(_cos());
        }

        else if (strcmp(current, "sin") == 0)
        {
            push(_sin());
        }

        else {
            int idx;
            if ((idx = find_var(current)) < 0) {
                fprintf(stderr, "error: unrecognized token: \"%s\"!\n", current);
                fprintf(stderr, "available variables:\n");
                print_vars();
                exit(1);
            }
            value v = var_data[idx];
            if (v.type == nest && v.auto_exec) {
                execute(v.data.nest, v.size);
                continue;
            }
            push(v);
        }

    force_continue: {}

        // favor não fazer nada aqui.
    }
}

void print_usage() {
    printf("usage: s24 [options] input-file\n\n"
           "options:\n"
           "   -h, --help\tprint this help text and exit\n");
}

void print_stack_and_quit() {
    fflush(stdout);
    fflush(stderr);

    fprintf(stderr, "SIGINT\n");
    print_stack();
    exit(1);
}


// fica pra depois
// value tokenize(char* input_path) {
//     char* in;
//     int size = read_file_to_string(input_path, &in);
//
//     value nest = new_nest();
//
//     for (int i = 0; i < size;) {
//         char a = in[i], b = in[i+1];
//
//         if (a == ' ' || a == '\t' || a == '\r' || a == '\n') {
//             i++;
//             continue;
//         }
//         else if ((a == '(' && b == '(') || (a == ')' && b == ')')) {
//             i += 2;
//             continue;
//         }
//         else if (a == '(') {
//             int parens = 1;
//             int j = i+1;
//             for (; j < size; j++) {
//                 if (in[j] == '(') parens++;
//                 if (in[j] == ')') parens--;
//                 if (parens == 0) break;
//             }
//             if (j == size) {
//                 fprintf(stderr, "error: unmatched comment!\n");
//                 exit(1);
//             }
//             i = j;
//             i++;
//             continue;
//         }
//         else if (a == ')') {
//             fprintf(stderr, "error: unrecognized token \')\'!\n");
//             exit(1);
//         }
//         else if (a == '[' || a == ']') {
//             nest_append(&nest, a == '[' ? "[" : "]");
//             i++;
//             continue;
//         }
//         else if (a == '"') {
//             char* start = in + i;
//             int j = i+1;
//             for (; j < size; j++) {
//                 if (in[j] == '\\' && in[j+1] == '"') {
//                     j++;
//                     continue;
//                 }
//                 if (in[j] == '"')
//                     break;
//             }
//             if (j == size) {
//                 fprintf(stderr, "error: unmatched string!\n");
//                 exit(1);
//             }
//             int string_size = j - i - 1;
//             char* buf = malloc(sizeof(char) * string_size + 1); 
//             strncpy(buf, start, string_size);
//             buf[string_size] = '\0';
//
//             nest_append(&nest, buf);
//             free(buf);
//             i = j;
//             i++;
//             continue;
//         }
//         else {
//             printf("%c %c", a, b);
//             int j = i;
//             for (; j < size; j++) {
//                 char c = in[j];
//                 if (c == ' ' || c == '(' || c == ')' || c == '[' || c == ']')
//                     break;
//             }
//             int string_size = (j - i);
//             char* buf = malloc(sizeof(char) * string_size + 1);
//             strncpy(buf, in + i, string_size);
//         }
//     }
//     return nest;
// }


int main(int argc, char** argv) {

    signal(SIGINT, print_stack_and_quit);

    // input
    char* input_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage();
            return 0;
        }
        else if (*argv[i] == '-')
        {
            fprintf(stderr, "error: unrecognized option \"%s\"!\n", argv[i]);
            print_usage();
            return 1;
        }

        else if (input_path == NULL)
        {
            input_path = argv[i];
        }

        else {
            fprintf(stderr, "error: unrecognized argument \"%s\"!\n", argv[i]);
            return 1;
        }
    }

    if (!input_path) {
        fprintf(stderr, "error: missing input file.\n");
        print_usage();
        return 1;
    }

    FILE* in = fopen(input_path, "r");
    if (!in) {
        fprintf(stderr, "error: can't open file \"%s\"\n", input_path);
        return 1;
    }


    // tokenizer
    char* current = tokens;
    while (fscanf(in, "%255s", current) == 1) {
        token_count++;
        current += token_stride;

        if (token_count >= max_tokens) {
            fprintf(stderr, "error: max number of tokens has been achieved (%d)!\n", max_tokens);
            exit(1);
        }
    }

    if (chdir(dirname(input_path)) == -1) {
        fprintf(stderr, "error: failed chaging directory\n");
        exit(1);
    }

    // eval
    execute(tokens, token_count);





    // output
    if (stack_size > 1) {
        fprintf(stderr, "error: stack remaining!\n");
        print_stack();
        return 1;
    }
    else if (stack_size == 0) {
        return 0;
    }

    value last = pop();
    if (last.type == operation)
    {
        fprintf(stderr, "error: lonely operation!\n");
        print_stack();
        return 1;
    }

    print_pretty_value(last);
    return 0;
}
