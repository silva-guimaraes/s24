#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>
#include "std.c"

#define max_token_len 256
#define token_stride max_token_len
#define max_tokens 300
#define max_stack 1000
#define max_vars 100
#define program_max_tokens tokens + token_count*max_token_len


typedef enum {
    value_null, constant, operation, nest, array, character, string
} value_type;

char* type_string[] = {
    "null", "constant", "operation", "nest", "array", "character", "string",
};

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

typedef value s24_array;
typedef value s24_nest;
typedef value s24_constant;
typedef value s24_string;


int stack_size = 0;
value stack[max_stack];
int stack_print_limit = 12;

int token_count = 0;

int var_count = 0;
char* var_names[max_vars];
value var_data[max_vars];

void execute(char*, int);



bool is_constant(value array)
{
    return array.type == constant;
}

bool is_char(value array)
{
    return array.type == character;
}

bool is_string(value array)
{
    return array.type == string;
}

char* get_string(value string)
{
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

char* get_string_slice(value string, int start, int end)
{
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

bool is_array(value v)
{
    return v.type == array || v.type == string;
}

value array_at(value array, int at)
{
    assert(is_array(array));
    assert(at >= 0 && at < array.size);

    return array.data.array[at];
}

// gracias gpt
int read_file_to_string(const char *filename, char** out)
{
    FILE* file = fopen(filename, "r");
    if (file == NULL) 
    {
        fprintf(stderr, "Failed to open file\n");
        exit(1);
    }

    // Seek to the end of the file to get the file size
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    // Allocate memory for the file content
    char* buffer = malloc(fileSize + 1);
    if (buffer == NULL) 
    {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(file);
        exit(1);
    }

    // Read the file content into the buffer
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != fileSize) 
    {
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

int tally(value arr)
{
    if (is_array(arr))
    {
        int sum = 0;
        for (int i = 0; i < arr.size; i++)
            sum += tally(array_at(arr, i));
        return sum;
    }
    return 1;
}

void _array_elements_type(value v, int* idx, value_type* types)
{
    if (is_array(v))
    {
        for (int i = 0; i < v.size; i++)
            _array_elements_type(array_at(v, i), idx, types);
        return;
    }
    types[*idx] = v.type;
    *idx += 1;
}

value_type array_elements_type(value arr)
{
    assert(is_array(arr));

    int t = tally(arr);
    value_type* types = malloc(sizeof(value_type) * t);
    int idx = 0;
    _array_elements_type(arr, &idx, types);

    value_type lead = types[0];
    for (int i = 1; i < t; i++) {
        if (lead != types[i]) {
            free(types);
            return -1;
        }
    }
    free(types);
    return lead;
}


void pretty_value(value v, bool a)
{
    switch (v.type) 
    {
        case character:
            printf("'%c'", v.data.c);
            break;
        case constant:
            printf(
                fmodl(v.data.constant, 1) == 0 ? "%-4.0lf" : "%-4.4lf",
                v.data.constant
            );
            break;
        case string:
            {
                char* s = get_string(v);
                printf("\"%s\"", s);
            }
            break;
        case array:
            if (v.size > 0 && array_at(v, 0).type == array) {
                if (a) printf("\n");
                for (int i = 0; i < v.size; i++)
                {
                    value v2 = array_at(v, i);
                    for (int j = 0; j < v2.size; j++)
                    {
                        pretty_value(array_at(v2, j), a);
                        printf(" ");
                    }
                    if (i != v.size-1)
                        printf("\n");
                }
                return;
            }
            printf("(( ");

            for (int i = 0; i < v.size; i++)  {
                pretty_value(v.data.array[i], false);
                printf(" ");
            }

            printf("))");
            break;
        case nest:
            printf("[ ");
            for (int i = 0; i < v.size; i++) 
                printf("%s ", v.data.nest + (i*token_stride));
            printf("]");
            break;
        default:
            printf("?value %d?", v.type);
            break;
    }
}

void print_pretty_value(value v, bool display_type)
{
    if (display_type)
    {
        if (v.type == array && v.size > 1)
        {
            int et = array_elements_type(v);
            if (et < 0)
                printf("( array ) ");
            else {
                printf("( %s array ) ", type_string[et]);
            }
        }
        else {
            printf("( %s ) ", type_string[v.type]);
        }
    }
    pretty_value(v, display_type);
    printf("\n");
}

void print_vars()
{
    for (int i = 0; i < var_count; i++) 
    {

        // char* value_string = value_repr(var_data[i]);
        printf("\"%s\":\n", var_names[i]);
        print_pretty_value(var_data[i], false);
        printf("\n");
    }
}

void print_stack()
{
    int limit = stack_size < stack_print_limit 
        ?  0 
        : stack_size - stack_print_limit;

    printf("stack (%d-%d):\n\n", stack_size, stack_size-limit);

    for (int i = stack_size-1; i >= limit; i--) 
    {

        value v = stack[i];

        print_pretty_value(v, true);
        // printf("--\t--\t--\n");
    }
    printf("end of stack\n");
    printf("--\t--\t--\n");
}

void push(value v)
{
    if (stack_size >= max_stack) 
    {
        fprintf(stderr, "error: max stack achieved (%d)!\n", max_stack);
        print_stack();
        exit(1);
    }
    stack[stack_size++] = v;
}

value pop()
{
    if (stack_size <= 0) 
    {
        fprintf(stderr, "error: ran out of stack! (%d)\n", stack_size);
        exit(1);
    }
    return stack[--stack_size];
}

value peek()
{
    if (stack_size <= 0) 
    {
        fprintf(stderr, "error: ran out of stack! (%d)\n", stack_size);
        exit(1);
    }
    return stack[stack_size-1];
}

int find_var(char* token)
{
    for (int i = 0; i < var_count; i++)
        if (strcmp(token, var_names[i]) == 0) 
            return i;
    return -1; // not found
}

value new_character(char c)
{
    return (value) 
        {
            .type = character,
            .data.c = c,
            .size = 0
        };
}

value new_constant(double c)
{
    return (value) 
        {
            .type = constant,
            .data.constant = c,
            .size = 0
        };
}

value string_to_constant(value v)
{
    assert(v.type == string);
    double number;
    char* s = get_string(v);
    if (sscanf(s, "%lf", &number) == 0)
    {
        fprintf(stderr, "error: failure at converting string \"%s!\"\n", s);
        exit(1);
    }
    free(s);
    return new_constant(number);
}


double get_constant(value c)
{
    assert(c.type == constant);

    return c.data.constant;
}

value new_nest()
{
    return (value) { 
        .type = nest, 
        .data.nest = NULL,
        .size = 0,
    };
}

value new_array(int size)
{
    value new = (value) { 
        .type = array, 
        .data.array = malloc(sizeof(value) * size),
        .size = size,
    };
    return new;
}

value new_string(int size)
{
    return (value) { 
        .type = string, 
        .data.array = malloc(sizeof(value) * size),
        .size = size,
    };
}

value from_string(char* s)
{
    int len = strlen(s);
    value r = new_string(len);

    for (int i = 0; i < r.size; i++)
    {
        r.data.array[i] = new_character(s[i]);
    }
    return r;
}


void array_append(value* array, value x) 
{
    array->data.array = realloc(array->data.array , sizeof(value) * ++array->size);
    array->data.array[array->size-1] = x;
}

void nest_append(value* n, char* tok) 
{
    assert(n->type == nest);
    n->data.nest = realloc(n->data.nest , max_token_len * ++n->size);
    strcpy(n->data.nest + (n->size-1)*token_stride, tok);
}

value wrap_array(value v) 
{
    value a = new_array(1);
    a.data.array[0] = v;
    return a;
}


value copy(value v) 
{
    if (v.type == array || v.type == string) 
    {

        value new = new_array(v.size);
        new.type = v.type;

        for (int i = 0; i < new.size; i++) 
        {
            new.data.array[i] = copy(array_at(v, i));
        }

        return new;
    }

    return v;
}

void free_value(value v) 
{
    switch (v.type) 
    {
        case array:
            for (int i = 0; i < v.size; i++) 
            {
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

value coerce_to_array(value v) 
{
    if (v.type == array)
        return v;

    value r = new_array(1);
    r.data.array[0] = v;
    return r;
}

value binary_op(value val1, value val2, value (*func)(value,value)) 
{
    val1 = coerce_to_array(val1);
    val2 = coerce_to_array(val2);

    bool map_val1 = val1.size > 1, map_val2 = val2.size > 1; 

    if (map_val1 && map_val2 && val1.size != val2.size) 
    {
        fprintf(
            stderr,
            "error: size mismatch (%d x %d)!\n",
            val1.size, val2.size);
        // exit() é o melhor free() de todos
        exit(1);
    }

    int size = max(val1.size, val2.size);
    value arr = new_array(size);
    for (int i = 0; i < size; i++) 
    {
        arr.data.array[i] = func(
            val1.data.array[map_val1 ? i : 0],
            val2.data.array[map_val2 ? i : 0]
        );
    }
    if (size == 1)
        return array_at(arr, 0);
    else
        return arr;
}

#define binary_op_type_handling(name, \
constant_handling, character_handling, string_handling) \
value name(value a, value b) { \
    if (is_constant(a) && is_constant(b)) { \
        return new_constant(constant_handling); \
    } \
    else if (is_char(a) && is_char(b)) { \
        character_handling; \
    } \
    else if (is_string(a) && is_string(b)) { \
        string_handling \
    } \
    if (is_constant(a) && is_string(b)) { \
        return name(a, string_to_constant(b)); \
    } \
    else if (is_string(a) && is_constant(b)) { \
        return name(string_to_constant(a), b); \
    } \
    if (is_array(a) || is_array(b)) { \
        return binary_op(a, b, name); \
    } \
    if (is_char(a) && !is_char(b)) { \
        return binary_op(new_constant(a.data.c), b, name); \
    } \
    else if (!is_char(a) && is_char(b)) { \
        return binary_op(a, new_constant(b.data.c), name); \
    } \
    assert(false); \
}

#define binary_type_handler_op_not_defined_error(operation, handler) \
fprintf( \
        stderr, \
        "error: %s operation between %ss not defined!\n", \
        operation, handler); \
exit(1); \


binary_op_type_handling(
    __sum, 
    get_constant(a) + get_constant(b),
    {
        return new_character(a.data.c + b.data.c);
    },
    {
        value n = copy(a);
        for (int i = 0; i < b.size; i++) 
        {
            array_append(&n, array_at(b, i));
        }
        return n;
    }
);

value sum()
{
    return binary_op(pop(), pop(), __sum);
}

binary_op_type_handling(
    __sub, 
    get_constant(a) - get_constant(b),
    {
        return new_character(a.data.c - b.data.c);
    },
    binary_type_handler_op_not_defined_error("subtraction", "string")
);


value subtraction()
{
    value subtrahend = pop(), minuend = pop();
    return binary_op(minuend, subtrahend, __sub);
}

binary_op_type_handling(
    __mul, 
    get_constant(a) * get_constant(b),
    {
        return new_character(a.data.c * b.data.c);
    },
    binary_type_handler_op_not_defined_error("multiplication", "string")
);

value multiplication()
{
    return binary_op(pop(), pop(), __mul);
}

binary_op_type_handling(
    __div, 
    a.data.constant / b.data.constant,
    {
        return new_character(a.data.c * b.data.c);
    },
    {
        return __sum(__sum(a, from_string("/")), b);
    }
);


value division()
{
    value divisor = pop(), dividend = pop();
    return binary_op(dividend, divisor, __div);
}

binary_op_type_handling(
    ___pow, 
    pow(a.data.constant, b.data.constant),
    binary_type_handler_op_not_defined_error("power", "character"),
    binary_type_handler_op_not_defined_error("power", "string")
);

value power()
{
    value exponent = pop(), base = pop();
    return binary_op(base, exponent, ___pow);
}

binary_op_type_handling(
    __mod, 
    fmodl(a.data.constant, b.data.constant),
    binary_type_handler_op_not_defined_error("modulo", "character"),
    binary_type_handler_op_not_defined_error("modulo", "string")
);

value mod() 
{
    value divisor = pop(), dividend = pop();
    return binary_op(dividend, divisor, __mod);
}

binary_op_type_handling(
    __equal, 
    a.data.constant == b.data.constant,
    {
        return new_constant(a.data.c == b.data.c);
    },
    {
        if (a.size != a.size) 
            return new_constant(false);

        for (int i = 0; i < a.size; i++) 
        {
            if (array_at(a, i).data.c != array_at(b, i).data.c) 
            {
                return new_constant(false);
            }
        }
        return new_constant(true);
    }

);

value equal() 
{
    return binary_op(pop(), pop(), __equal);
}

binary_op_type_handling(
    __or, 
    a.data.constant || b.data.constant,
    binary_type_handler_op_not_defined_error("or", "character"),
    binary_type_handler_op_not_defined_error("or", "string")
);


value or() 
{
    return binary_op(pop(), pop(), __or);
}

// binary_op_type_handling(
//     __mask, 
//     a.data.constant || b.data.constant,
//     binary_type_handler_op_not_defined_error("or", "character"),
//     binary_type_handler_op_not_defined_error("or", "string")
// );
//
//
// value mask() 
// {
//     return binary_op(pop(), pop(), __mask);
// }

value mask() 
{
    value mask = pop(), filter = pop();

    if (mask.size != filter.size) 
    {
        fprintf(stderr, "error: mask and array have different sizes!\n");
        exit(1);
    }
    value arr = new_array(0);
    for (int i = 0; i < mask.size; i++) 
    {

        if (get_constant(array_at(mask, i)) == 1.0) 
        {
            array_append(&arr, array_at(filter, i));
        }
    }
    return arr;
}













// unary built-ins

value unary_op(value v, value (*func)(value)) 
{
    v = coerce_to_array(v);

    int size = v.size;
    value arr = new_array(size);

    for (int i = 0; i < size; i++) 
        arr.data.array[i] = func(v.data.array[i]);

    if (size == 1)
        return array_at(arr, 0);
    else
        return arr;
}

#define unary_type_handler_op_not_defined_error(operation, handler) \
fprintf( \
        stderr, \
        "error: %s operation for %ss not defined!\n", \
        operation, handler); \
exit(1);

#define unary_op_type_handling(name, \
constant_handling, character_handling, string_handling) \
value name(value v) { \
    switch (v.type) { \
        case constant: \
            constant_handling; \
            break; \
        case character: \
            character_handling; \
            break; \
        case string: \
            string_handling; \
            break; \
        case array: \
            return unary_op(v, name); \
            break; \
        default: \
            assert(false); \
            break; \
    } \
    assert(false); \
}

unary_op_type_handling(
    _is_prime, 
    {
        int x = get_constant(v);
        if (x <= 1)
            return new_constant(false);

        double sqroot = sqrtl(x);
        bool is_prime = true;

        for (double i = 2; i <= sqroot; i++) 
            if (fmodl(x, i) == 0) 
            {
                is_prime = false;
                break;
            }

        return new_constant(is_prime);
    },
    {
        return _is_prime(new_constant(v.data.c));
    },
    {
        return _is_prime(string_to_constant(v));
    }
)

unary_op_type_handling(
    _gt0, 
    {
        return new_constant(v.data.constant > 0);
    },
    {
        return _gt0(new_constant(v.data.c));
    }, 
    {
        return _gt0(string_to_constant(v));
    }
)

value  gt0()
{
    return unary_op(pop(), _gt0);
}

unary_op_type_handling(
    _lt0, 
    {
        return new_constant(v.data.constant < 0);
    },
    {
        return _gt0(new_constant(v.data.c));
    }, 
    {
        return _gt0(string_to_constant(v));
    }
)

value  lt0()
{
    return unary_op(pop(), _lt0);
}

unary_op_type_handling(
    _not, 
    {
        return new_constant(!v.data.constant);
    },
    {
        return _gt0(new_constant(v.data.c));
    }, 
    {
        return _gt0(string_to_constant(v));
    }
)

unary_op_type_handling(
    ___round, 
    {
        return new_constant(lroundf(v.data.constant));
    },
    {
        return v;
    }, 
    unary_type_handler_op_not_defined_error("round", "string")
)

unary_op_type_handling(
    ___abs, 
    {
        return new_constant(fabsl(v.data.constant));
    },
    {
        return v;
    }, 
    unary_type_handler_op_not_defined_error("round", "string")
)

value  not()
{
    return unary_op(pop(), _not);
}

value is_prime()
{
    return unary_op(pop(), _is_prime);
}

value _round()
{
    return unary_op(pop(), ___round);
}

value _abs()
{
    return unary_op(pop(), ___abs);
}

value _cos()
{
    fprintf(stderr, "not implmemented yet");
    exit(1);
}

value _sin()
{
    fprintf(stderr, "not implmemented yet");
    exit(1);
}


// special


value list() 
{
    value take = pop();
    int size = get_constant(take);
    size = size == -1 ? stack_size : size;

    value arr = new_array(size);

    for (int i = 0; i < size; i++) 
    {
        arr.data.array[size - i - 1] = pop();
    }

    return arr;
}
void clear() 
{
    int ss = stack_size;
    for (int i = 0; i < ss; i++)
        pop();
}

// todo: pra std
// value range() {
//     value to = pop(), from = pop();
//
//     int size = get_constant(to) - get_constant(from);
//
//     value arr = new_array(size);
//
//     for (int i = 0; i < size; i++) {
//         arr.data.array[i] = new_constant(get_constant(from) + i);
//     }
//
//     return arr;
// }

// todo: pra std
// value range1() {
//     value to = pop();
//     int size = get_unit(to);
//     value arr = new_array(size);
//     array_iter(arr, i, new_constant(i))
//     return arr;
// }

value _index() 
{
    value mask = pop();

    value arr = new_array(0);
    for (int i = 0; i < mask.size; i++) 
    {

        if (get_constant(array_at(mask, i)) == 1.0) 
        {
            array_append(&arr, new_constant(i));
        }
    }

    return arr;
}

value _index2() 
{
    value to_search = pop(), list = pop();

    value arr = new_array(0);

    for (int i = 0; i < to_search.size; i++) 
    {

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

value reverse() 
{
    value arr = pop(), new = new_array(arr.size);

    assert(arr.type == array);

    for (int i = 0; i < arr.size; i++){
        new.data.array[arr.size - i - 1] = array_at(arr, i);
    }

    return new;
}

value at() 
{
    value at = pop(), arr = pop();
    push(arr), push(at);

    double index = get_constant(at);

    if (index < 0 || index >= arr.size) {
        fprintf(stderr, "error: index out of bounds!\n");
        exit(1);
    }
    if (fmodl(index, 1) != 0) {
        fprintf(
            stderr,
            "error: can't index array by floating point constant! "
            "(index: %lf)\n", index);
        print_pretty_value(arr, false);
        exit(1);
    }

    return array_at(arr, index);
}

void reduce_left() 
{
    value nested_op = pop(), arr = pop();

    if (nested_op.type != nest) 
    {
        fprintf(stderr, "error: can only apply nested operations to arrays!!\n");
        print_pretty_value(nested_op, false);
        exit(1);
    }
    if (arr.type != array) 
    {
        fprintf(stderr, "error: can't reduce what's not an array!\n");
        print_pretty_value(arr, false);
        exit(1);
    }

    push(array_at(arr, 0));
    for (int i = 1; i < arr.size; i++) 
    {
        push(array_at(arr, i));
        execute(nested_op.data.nest, nested_op.size);
    }
}

value accumulate_left() 
{
    value nested_op = pop(), arr = pop();

    if (nested_op.type != nest) 
    {
        fprintf(stderr, "error: can only apply nested operations to arrays!!\n");
        print_pretty_value(nested_op, false);
        exit(1);
    }
    if (arr.type != array) 
    {
        fprintf(stderr, "error: can't reduce what's not an array!\n");
        print_pretty_value(arr, false);
        exit(1);
    }

    value acc = new_array(0);
    push(array_at(arr, 0));
    for (int i = 1; i < arr.size; i++) 
    {
        push(array_at(arr, i));
        execute(nested_op.data.nest, nested_op.size);
        array_append(&acc, peek());
    }
    pop();
    return acc;
}




s24_nest broadcast;

value _unary_broadcast(value v) {
    if (is_array(v)) {
        return unary_op(v, _unary_broadcast);
    }

    push(v);
    execute(broadcast.data.nest, broadcast.size);
    return pop();
}

value _binary_broadcast(value a, value b) {
    if (is_array(a) || is_array(b)) {
        return binary_op(a, b, _binary_broadcast);
    }

    push(a);
    push(b);
    execute(broadcast.data.nest, broadcast.size);
    return pop();
}

value binary_broadcast() {
    broadcast = pop();
    if (broadcast.type != nest) {
        fprintf(stderr, "error: broadcast operation must be nested\n");
        exit(1);
    }
    return binary_op(pop(), pop(), _binary_broadcast);
}

value unary_broadcast() {
    broadcast = pop();
    if (broadcast.type != nest) {
        fprintf(stderr, "error: broadcast operation must be nested\n");
        exit(1);
    }
    return unary_op(pop(), _unary_broadcast);
}





void execute(char* start, int amount) 
{
    char* last;
    int parens;

rewind:
    last = start + amount*token_stride;
    parens = 0;

    for (char* current = start; current < last; current += token_stride) 
    {


        if (strcmp(current, "((" /*))*/) == 0 || strcmp(current, /*((*/"))") == 0) 
        {
            continue;
        }

        if (strcmp(current, "(" /*)*/) == 0) 
        {
            parens++;
            continue;
        }
        if (strcmp(current,  /*(*/")") == 0) 
        {
            if (parens == 0) 
            {
                fprintf(stderr, "error: parens mismatch!\n");
                exit(1);
            }
            parens--;
            continue;
        }
        if (parens > 0) 
            continue;

        if (parens < 0) 
        {
            fprintf(stderr, "error: parens mismatch!\n");
            exit(1);
        }

        if (*current == '"') 
        {

            value string = new_string(0);

            char* i = current + 1;
            char* t = current;

            for (; i < last; i++) 
            {
                if (*i == '\0' || (*i == '\\' && *(i+1) == '\0'))
                {
                    array_append(&string, new_character(' '));
                    t += token_stride;
                    i = t-1;
                }
                else if (*i == '"')
                {
                    assert(i == t + strlen(t) - 1);
                    break;
                }
                else {
                    array_append(&string, new_character(*i));
                }
            }

            push(string);
            current = t;
            continue;
        }

        if (strcmp(current, "[" /*]*/) == 0) 
        {
            value new_nest = (value) { 
                .type = nest,
                .data.nest = NULL,
                .size = 0,
            };
            int brackets = 1;
            char* i = current + token_stride;
            for (; i < last; i += max_token_len) 
            {
                if (strcmp(i, "[" /*]*/ ) == 0) 
                {
                    brackets++;
                }
                if (strcmp(i, /*[*/ "]") == 0) 
                {
                    brackets--;
                }
                if (brackets == 0)
                    break;

                new_nest.data.nest = realloc(new_nest.data.nest , max_token_len * ++new_nest.size);
                strcpy(new_nest.data.nest + (new_nest.size-1)*token_stride, i);
            }
            if (i == last) 
            {
                fprintf(stderr, "error: unmatched nesting!\n");
                exit(1);
            }

            current = i;

            push(new_nest);
            continue;
        }


        double number;
        if (sscanf(current, "%lf", &number) == 1) 
        {
            push(new_constant(number));
            continue;
        }

        if (current[0] == '.' || strcmp(current, "loop") == 0) 
        {
            continue;
        }


        if (strcmp(current, "pop") == 0)
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

        else if (strcmp(current, "not") == 0)
        {
            push(not());
        }

        else if (strcmp(current, "=") == 0)
        {
            push(equal());
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
                // free_value(var_data[idx]); // problemático
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

            if (get_constant(check)) 
            {
                char* goto_label = current += token_stride;

                if (goto_label[0] != '.')
                    continue;

                if (goto_label[0] == '.' && strlen(goto_label) == 1) 
                {
                    fprintf(stderr, "error: goto label \"%s\" is not a label!\n", goto_label);
                    exit(1);
                }

                for (char* i = start; i < last; i += token_stride) 
                {
                    if (i[0] != '.')
                        continue;
                    if (i != goto_label && strcmp(goto_label, i) == 0) 
                    {
                        current = i;
                        goto force_continue;
                    }
                }

                fprintf(stderr, "error: couldn't find label \"%s\"!\n", goto_label);
                exit(1);
            }
            else {
                char* else_label = current + token_stride*2;

                if (else_label[0] != '.') 
                {
                    current += token_stride;
                    continue;
                }

                if (else_label[0] == '.' && strlen(else_label) == 1) 
                {
                    fprintf(stderr, "error: goto label \"%s\" is not a label!\n", else_label);
                    exit(1);
                }

                for (char* i = start; i < last; i += token_stride) 
                {
                    if (i[0] != '.')
                        continue;
                    if (i != else_label && strcmp(else_label, i) == 0) 
                    {
                        current = i;
                        goto force_continue;
                    }
                }

                fprintf(stderr, "error: couldn't find label \"%s\"!\n", else_label);
                exit(1);
            }
        }

        else if (strcmp(current, "loop") == 0)
        {
            continue;
        }

        else if (strcmp(current, "do") == 0)
        {
            if (get_constant(pop())) {
                continue;
            }
            else {
                char* i = current + token_stride;
                int level = 1;
                for (; i < last; i += token_stride) {
                    if (strcmp(i, "do") == 0)
                        level++;
                    if (strcmp(i, "over") == 0)
                        level--;
                    if (level == 0)
                        break;
                }
                if (level > 0) {
                    fprintf(stderr, "error: loop without over\n");
                    exit(1);
                }
                current = i;
            }
        }

        else if (strcmp(current, "over") == 0)
        {
            int level = 1;
            char* i = current - token_stride;
            for (; i >= start ; i -= token_stride) {
                if (strcmp(i, "over") == 0)
                    level++;
                if (strcmp(i, "loop") == 0)
                    level--;
                if (level == 0)
                    break;
            }
            if (level > 0) {
                fprintf(stderr, "error: over without loop\n");
                exit(1);
            }
            current = i;
        }

        else if (strcmp(current, ";") == 0) 
        {
            for (char* i = current; i < last; i += token_stride) 
            {
                // printf("op: %s cmp: %d\n", i, strcmp(i, ".end"));
                if (strcmp(i, ".end") == 0) 
                {
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
            value p = peek();
            push(new_constant(is_array(p) || p.type == nest ? p.size : 1));
        }

        else if (strcmp(current, "at") == 0)
        {
            push(at());
        }

        else if (strcmp(current, "abs") == 0)
        {
            push(_abs());
        }

        else if (strcmp(current, "pp")  == 0) 
        {
            print_pretty_value(peek(), false);
        }

        else if (strcmp(current, "nl")  == 0) 
        {
            printf("\n");
        }

        else if (strcmp(current, "fmt")  == 0) 
        {
            value string = pop();
            print_pretty_value(string, false);
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
            push(copy(peek()));
        }

        else if (strcmp(current, "mod") == 0)
        {
            push(mod());
        }

        else if (strcmp(current, "a")   == 0)
        {
            push(list());
        }

        // else if (strcmp(current, "++")   == 0)
        // {
        //     push(new_unit(1));
        //     push(sum());
        // }
        //
        // else if (strcmp(current, "--")   == 0)
        // {
        //     push(new_unit(-1));
        //     push(sum());
        // }

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

        else if (strcmp(current, "rev") == 0)
        {
            push(reverse());
        }

        else if (strcmp(current, "clr") == 0)
        {
            clear();
        }

        else if (strcmp(current, "ipr") == 0)
        {
            push(is_prime());
        }

        // else if (strcmp(current, "ran") == 0)
        // {
        //     push(range());
        // }

        // else if (strcmp(current, "ran1") == 0)
        // {
        //     push(range1());
        // }

        else if (strcmp(current, "x") == 0)
        {
            value nesting = pop();
            if (nesting.type != nest) 
            {
                fprintf(stderr, "error: can't execute what is not a nest!\n");
                print_pretty_value(nesting, false);
                exit(1);
            }
            execute(nesting.data.nest, nesting.size);
        }

        else if (strcmp(current, "rx") == 0)
        {
            value nesting = pop();
            if (nesting.type != nest) 
            {
                fprintf(stderr, "error: can't execute what is not a nest!\n");
                print_pretty_value(nesting,false );
                exit(1);
            }

            if (nesting.data.nest != start) 
            {
                fprintf(stderr, "error: can't rewind a nest outside of itself! "
                        "try using the 'x' command!\n");
                print_pretty_value(nesting, false);
                exit(1);
            }
            // execute(nesting.data.nest, nesting.size);

            start = nesting.data.nest;
            amount = nesting.size;
            goto rewind;
        }

        else if (strcmp(current, "unb") == 0) // inutil?
        {
            value arr = pop();
            if (!is_array(arr)) 
            {
                push(arr);
                continue;
            }
            for (int i = 0; i < arr.size; i++) 
            {
                push(array_at(arr, i));
            }
        }

        else if (strcmp(current, "ld") == 0)
        {
            value string = pop();

            string = array_at(string, 0); // fixme

            char* builder = malloc(sizeof(char) * string.size + 1);
            for (int i = 0; i < string.size; i++) 
            {
                builder[i] = array_at(string, i).data.c;
            }
            builder[string.size] = '\0';

            char* file;
            int file_size = read_file_to_string(builder, &file);

            value new_string = new_array(0);

            for (int i = 0; i < file_size; i++) 
            {
                array_append(&new_string, new_character(file[i]));
            }

            push(new_string);
        }

        else if (strcmp(current, "sws") == 0)
        {
            value string = pop();

            value r = new_array(0);
            value b = new_array(0);

            for (int i = 0; i < string.size; i++) 
            {
                value at = array_at(string, i);
                switch (at.data.c) 
                {
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

            for (int i = 0; i < string.size; i++) 
            {
                char* s = get_string_slice(string, i, i+delimiter_len);
                if (strcmp(s, delimiter) == 0) 
                {

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
            if (is_string(arr)) 
            {
                value n = new_array(0);
                array_append(&n, arr);
                arr = n;
            }

            value r = new_array(0);

            for (int i = 0; i < arr.size; i++) 
            {

                double number;
                char* s = get_string(array_at(arr, i));
                if (sscanf(s, "%lf", &number) == 0) 
                {
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

        else if (strcmp(current, "$:") == 0)
        {
            push(binary_broadcast());
        }

        else if (strcmp(current, "$.") == 0)
        {
            push(unary_broadcast());
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
            if ((idx = find_var(current)) < 0) 
            {
                fprintf(stderr, "error: unrecognized token: \"%s\"!\n", current);
                // fprintf(stderr, "available variables:\n");
                // print_vars();
                exit(1);
            }
            value v = var_data[idx];
            if (v.type == nest && v.auto_exec) 
            {
                execute(v.data.nest, v.size);
                continue;
            }
            push(v);
        }

    force_continue: {}

        // favor não fazer nada aqui.
    }
}

void print_usage() 
{
    printf("usage: s24 [options] input-file\n\n"
           "options:\n"
           "   -h, --help\tprint this help text and exit\n");
}

void print_stack_and_quit() 
{
    fflush(stdout);
    fflush(stderr);

    fprintf(stderr, "SIGINT\n");
    print_stack();
    exit(1);
}


value tokenize(FILE* in) 
{

    char buf[max_token_len];
    value n = new_nest();

    while (fscanf(in, "%255s", buf) == 1) 
        nest_append(&n, buf);

    return n;
}

int main(int argc, char** argv) 
{

    signal(SIGINT, print_stack_and_quit);

    // input
    char* input_path = NULL;
    for (int i = 1; i < argc; i++) 
    {
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

    if (!input_path) 
    {
        fprintf(stderr, "error: missing input file.\n");
        print_usage();
        return 1;
    }

    FILE* in = fopen(input_path, "r");
    if (!in) 
    {
        fprintf(stderr, "error: can't open file \"%s\"\n", input_path);
        return 1;

    }


    if (chdir(dirname(input_path)) == -1) 
    {
        fprintf(stderr, "error: failed chaging directory\n");
        exit(1);
    }


    FILE* std_in = fmemopen(std, std_len, "r");
    value std_program = tokenize(std_in);
    execute(std_program.data.nest, std_program.size);


    value program = tokenize(in);

    // eval
    execute(program.data.nest, program.size);



    // output
    if (stack_size > 1) 
    {
        fprintf(stderr, "error: stack remaining!\n");
        print_stack();
        return 1;
    }
    else if (stack_size == 0) 
    {
        return 0;
    }

    value last = pop();
    if (last.type == operation)
    {
        fprintf(stderr, "error: lonely operation!\n");
        print_stack();
        return 1;
    }

    print_pretty_value(last, false);


    free_value(program);
    free_value(std_program);


    return 0;
}
