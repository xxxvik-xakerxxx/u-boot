#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define unlikely(x) (x)
#define pr_debug(...) do { } while (0)
#define pr_err(...) do { } while (0)
