
#ifndef LOGGING_H_INCLUDED
#define LOGGING_H_INCLUDED

#include <vdr/tools.h>

extern int m_debugmask;

#define DEBUG_MASK_RB_OSD	0x00010000

#if 1

#define isyslog_rb(format, arg...) isyslog("reelbox: INFO  " format, ## arg)
#define esyslog_rb(format, arg...) esyslog("reelbox: ERROR " format, ## arg)
#define dsyslog_rb(format, arg...) dsyslog("reelbox: DEBUG %s/%s " format, __FILE__, __FUNCTION__, ## arg)
#define printf(format, arg...)     dsyslog("reelbox: DEBUG %s/%s " format, __FILE__, __FUNCTION__, ## arg)

#else

// stderr debugging
#define isyslog_rb(format, arg...) fprintf(stderr, "reelbox: INFO  " format, ## arg)
#define esyslog_rb(format, arg...) fprintf(stderr, "reelbox: ERROR " format, ## arg)
#define dsyslog_rb(format, arg...) fprintf(stderr, "reelbox: DEBUG " format, ## arg)
#define printf(format, arg...)     fprintf(stderr, "reelbox: DEBUG " format, ## arg)

#endif

#define DEBUG_RB_OSD	if (m_debugmask & DEBUG_MASK_RB_OSD) dsyslog_rb

#endif
