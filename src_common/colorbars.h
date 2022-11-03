#ifndef COLORBARS_H
#define COLORBARS_H

#ifdef COLORBARS


#define ColorbarsApi extern "C" __declspec(dllimport)

ColorbarsApi void colorbars_connect(const char* host, unsigned short port);
ColorbarsApi void colorbars_introduce_thread(const char* name);
ColorbarsApi void colorbars_introduce_zone(void* zone_id, const char* name, const char* file, int line);
ColorbarsApi void colorbars_event_zone_start(void* zone_id);
ColorbarsApi void colorbars_event_zone_end(void* zone_id);

// on Windows, 'time' is a QPC value
ColorbarsApi void colorbars_timestamp_label(const char* name, unsigned long long time);
ColorbarsApi void colorbars_timestamp_label_now(const char* name);


#ifndef __INTRIN_H_
extern "C" long _InterlockedIncrement(long volatile* _Addend);
#endif

#define ScopeZone(name) ScopeZone_(#name, __COUNTER__)
#define ScopeZone_(name, counter) ScopeZone__(name, counter)
#define ScopeZone__(name, counter) ScopeZone___(name, COLORBARS_ZONE__##counter)
#define ScopeZone___(name, ident)                                               \
    static long volatile ident;                                                 \
    if (!ident && _InterlockedIncrement(&ident) == 1)                           \
        colorbars_introduce_zone((void*) &ident, name, __FILE__, __LINE__);     \
    struct ident##_Scope                                                        \
    {                                                                           \
        inline  ident##_Scope() { colorbars_event_zone_start((void*) &ident); } \
        inline ~ident##_Scope() { colorbars_event_zone_end  ((void*) &ident); } \
    } ident##_scope;

#define FunctionZone() ScopeZone_(__FUNCTION__, __COUNTER__)


#else

struct Colorbars_Client;
#define colorbars_connect(...) (NULL)
#define colorbars_restore_client(...)

#define colorbars_introduce_thread(...)
#define colorbars_introduce_zone(...)
#define colorbars_event_zone_start(...)
#define colorbars_event_zone_end(...)
#define colorbars_timestamp_label(...)

#define ScopeZone(...)
#define ScopeZone_(...)
#define FunctionZone(...)


#endif  // #ifdef COLORBARS
#endif  // #ifndef COLORBARS_H
