#ifdef __linux__
#include_next <linux/errno.h>
#else
#include <errno.h>
#endif
#ifndef EBADMSG
#define EBADMSG 74
#endif
