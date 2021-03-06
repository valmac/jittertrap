#include "mq_msg_ws.h"

#define MAX_CONSUMERS 8
#define MAX_Q_DEPTH 16

#define NS(name) PRIMITIVE_CAT(mq_ws_, name)
#define PRIMITIVE_CAT(a, ...) a##__VA_ARGS__

/* note that we include the C file here, not the header. */
#include "mq_generic.c"

#undef NS
