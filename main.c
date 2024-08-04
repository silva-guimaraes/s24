
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define max_token_len 256
#define token_stride max_token_len
#define max_tokens 300
#define max_stack 200
#define max_vars 100
#define program_max_tokens tokens + token_count*max_token_len


typedef enum {
    value_null, constant, operation, nest, array, character
} value_type;

// typedef enum {
//     opcode_null, sum, sub, dlv, mul, let, _abs, pp, dup, mod, rou, clr, ipr, to_array, ran, exe
// } opcode;

typedef struct value {
    value_type type;
    union {
        double constant;
        char c;
        struct value* array;
        char* nest;
    } data;
    int size;
} value;

typedef value (*opfunc)();


int stack_size = 0;
value stack[max_stack];

int token_count = 0;
char tokens[max_token_len * max_tokens];

int var_count = 0;
char* var_names[max_vars];
value var_data[max_vars];





bool is_unit(value array) {
    return array.size == 1 && array.data.array[0].type == constant;
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
                for (int i = 0; i < v.size; i++){
                    ret[i] = v.data.array[i].data.c;
                }
                break;
            }
            if (is_unit(v)) {
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
    fprintf(stderr, "%s\n", pp);
    free(pp);
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
                sprintf(ret, is_unit(v) ? "( unit ) %s" : "( array ) %s", pp);
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
    for (int i = stack_size-1; i >= 0; i--) {

        value v = stack[i];

        char* value_string = value_repr(v);

        printf("%d: %s\n", i, value_string);
        printf("========================================\n");

        free(value_string);
    }
}

void push(value v) {
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

double get_constant(value c) {
    assert(c.type == constant);

    return c.data.constant;
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

value new_unit(double c) {
    value arr = new_array(0);
    array_append(&arr, new_constant(c));
    return arr;
}


double get_unit(value array) {
    assert(is_unit(array));

    return array.data.array[0].data.constant;
}

value coerce_unit_to_const(value v)
{
    if (is_unit(v))
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

#define max(x, y) x > y ? x : y

#define binary_iter(lvalue)                                 \
bool map_val1 = !is_unit(val1), map_val2 = !is_unit(val2);  \
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

value equal() {
    value val1 = pop(), val2 = pop();
    binary_map_op(==);
    return arr;
}










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
double __neg(double x) { return !x; }

def_simple_unary_func_op(   _round,         roundl  );
def_simple_unary_func_op(    is_prime,    __is_prime);
def_simple_unary_func_op(   _abs,           fabsl   );
def_simple_unary_func_op(   _cos,           cos     );
def_simple_unary_func_op(   _sin,           sin     );
def_simple_unary_func_op(    gt0,         __gt0     );
def_simple_unary_func_op(    lt0,         __lt0     );
def_simple_unary_func_op(    negative,    __neg     );

// value _round() {
//     value val = pop();
//     unary_map_func(roundl);
//     return arr;
// }

// value is_prime() {
//     value val = pop();
//     unary_map_func(__is_prime);
//     return arr;
// }

// value _abs() {
//     value val = pop();
//     unary_map_func(fabsl);
//     return arr;
// }
//
// value _cos() {
//     value val = pop();
//     unary_map_func(cos);
//     return arr;
// }
//
// value _sin() {
//     value val = pop();
//     unary_map_func(sin);
//     return arr;
// }





// special


value list() {
    value take = pop();
    int size = get_unit(take);

    value arr = new_array(size);

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







void execute(char* start, int amount) {
    char* last = start + amount*token_stride;

    int parens = 0;

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
            // print_pretty_value(string);
            push(string);
            current = t;
            continue;
        }

        if (strcmp(current, "[" /*]*/) == 0) {
            value new_nest = (value) { 
                .type = nest,
                .data.nest = NULL,
                .size = 0,
            };
            char* i = current + max_token_len;
            for (; i < program_max_tokens; i += max_token_len) {
                if (strcmp(i, "[" /*]*/ ) == 0) {
                    fprintf(stderr, "error: forbidden nested nesting!\n");
                    exit(1);
                }
                if (strcmp(i, /*[*/ "]") == 0) {
                    break;
                }

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


        if      (strcmp(current, "+") == 0)
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


        else if (strcmp(current, "->")  == 0)
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

            var_names[idx] = name;
            var_data[idx] = assign;
            current = name; // proxima iteração pula o nome da variável
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

        else if (strcmp(current, "abs") == 0)
        {
            push(_abs());
        }

        else if (strcmp(current, "pp")  == 0) 
        {
            print_pretty_value(peek());
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

        else if (strcmp(current, "+1")   == 0)
        {
            push(new_unit(1));
            push(sum());
        }

        else if (strcmp(current, "-1")   == 0)
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

        else if (strcmp(current, "del") == 0)
        {
            push(negative());
            push(mask());
        }

        else if (strcmp(current, "rev") == 0)
        {
            push(reverse());
        }

        else if (strcmp(current, "neg") == 0)
        {
            push(negative());
        }

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
                exit(1);
            }
            execute(nesting.data.nest, nesting.size);
        }

        else if (strcmp(current, ".") == 0)
        {
            value arr = pop();
            if (arr.type != array) {
                fprintf(stderr, "error: can't unbind what is not an array!\n");
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
                exit(1);
            }
            push(var_data[idx]);
        }
    }
}


int main(int argc, char** argv) {

    // input
    if (argc != 2) {
        fprintf(stderr, "argc != 2");
        return 1;
    }
    FILE* in = fopen(argv[1], "r");


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
