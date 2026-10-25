#ifndef EMU_PORTMACRO_WRAPPER_H
#define EMU_PORTMACRO_WRAPPER_H

// Enveloppe du portmacro.h officiel du port Windows (port/MSVC-MingW/portmacro.h, non modifie).
// Ce dernier inclut windows.h, dont certaines macros portent le meme nom que des identifiants du
// VCU. On reduit ce que windows.h apporte, puis on retire les macros en conflit.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "port/MSVC-MingW/portmacro.h"

// winbase.h : STATUS_TIMEOUT est aussi une valeur de DataTimeStatus_t (include/structs.h)
#undef STATUS_TIMEOUT

#endif // EMU_PORTMACRO_WRAPPER_H
