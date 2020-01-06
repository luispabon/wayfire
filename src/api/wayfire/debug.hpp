#ifndef DEBUG_HPP
#define DEBUG_HPP

#ifndef WAYFIRE_PLUGIN
#include "config.h"
#endif

#define nonull(x) ((x) ? (x) : ("nil"))

namespace wf
{
void print_trace();
}

#endif
