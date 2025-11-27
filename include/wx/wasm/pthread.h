/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/thread.h
// Purpose:     Dummy implementations of unsupported pthread functions.
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PTHREAD_H__
#define _WX_WASM_PTHREAD_H__

#include <pthread.h>
#include <sched.h>

int pthread_attr_getschedpolicy(const pthread_attr_t *attr,
                                int *policy);

//int pthread_attr_getschedparam(const pthread_attr_t *attr,
//                               struct sched_param *param);

int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param);

int pthread_setschedparam(pthread_t thread,
                          int policy,
                          const struct sched_param *param);

int pthread_setconcurrency(int new_level);

int sched_get_priority_min(int policy);

int sched_get_priority_max(int policy);


#endif // _WX_WASM_PTHREAD_H__
