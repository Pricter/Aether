#include <kernel/kprintf.h>
#include <stdint.h>

struct source_location {
  const char *file;
  uint32_t line;
  uint32_t column;
};

struct type_descriptor {
  uint16_t kind;
  uint16_t info;
  char name[];
};

struct overflow_data {
  struct source_location location;
  struct type_descriptor *type;
};

struct shift_out_of_bounds_data {
  struct source_location location;
  struct type_descriptor *left_type;
  struct type_descriptor *right_type;
};

struct invalid_value_data {
  struct source_location location;
  struct type_descriptor *type;
};

struct array_out_of_bounds_data {
  struct source_location location;
  struct type_descriptor *array_type;
  struct type_descriptor *index_type;
};

struct type_mismatch_v1_data {
  struct source_location location;
  struct type_descriptor *type;
  unsigned char log_alignment;
  unsigned char type_check_kind;
};

struct negative_vla_data {
  struct source_location location;
  struct type_descriptor *type;
};

struct nonnull_return_data {
  struct source_location location;
};

struct nonnull_arg_data {
  struct source_location location;
};

struct unreachable_data {
  struct source_location location;
};

struct invalid_builtin_data {
  struct source_location location;
  unsigned char kind;
};

static void print_location(const char *message,
                              struct source_location loc) {
  kprintf("Undefined behaviour: %s at %s:%d:%d\n", message, loc.file,
         loc.line, loc.column);
}

void __ubsan_handle_add_overflow(struct overflow_data *data) {
  print_location("addition overflow", data->location);
}

void __ubsan_handle_sub_overflow(struct overflow_data *data) {
  print_location("subtraction overflow", data->location);
}

void __ubsan_handle_mul_overflow(struct overflow_data *data) {
  print_location("multiplication overflow", data->location);
}

void __ubsan_handle_divrem_overflow(struct overflow_data *data) {
  print_location("division overflow", data->location);
}

void __ubsan_handle_negate_overflow(struct overflow_data *data) {
  print_location("negation overflow", data->location);
}

void __ubsan_handle_pointer_overflow(struct overflow_data *data) {
  print_location("pointer overflow", data->location);
}

void __ubsan_handle_shift_out_of_bounds(
    struct shift_out_of_bounds_data *data) {
  print_location("shift out of bounds", data->location);
}

void __ubsan_handle_load_invalid_value(struct invalid_value_data *data) {
  print_location("invalid load value", data->location);
}

void __ubsan_handle_out_of_bounds(struct array_out_of_bounds_data *data) {
  print_location("array out of bounds", data->location);
}

void __ubsan_handle_type_mismatch_v1(struct type_mismatch_v1_data *data,
                                     uintptr_t ptr) {
  if (!ptr) {
    print_location("use of NULL pointer", data->location);
  }

  else if (ptr & ((1 << data->log_alignment) - 1)) {
    print_location("use of misaligned pointer", data->location);
  } else {
    print_location("no space for object", data->location);
  }
}

void __ubsan_handle_vla_bound_not_positive(struct negative_vla_data *data) {
  print_location("variable-length argument is negative", data->location);
}

void __ubsan_handle_nonnull_return(struct nonnull_return_data *data) {
  print_location("non-null return is null", data->location);
}

void __ubsan_handle_nonnull_arg(struct nonnull_arg_data *data) {
  print_location("non-null argument is null", data->location);
}

void __ubsan_handle_builtin_unreachable(struct unreachable_data *data) {

  print_location("unreachable code reached", data->location);
}

void __ubsan_handle_invalid_builtin(struct invalid_builtin_data *data) {

  print_location("invalid builtin", data->location);
}