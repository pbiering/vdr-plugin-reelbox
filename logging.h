
#ifndef LOGGING_H_INCLUDED
#define LOGGING_H_INCLUDED

#define isyslog_rb(format, arg...) isyslog("reelbox: INFO  " format, ## arg)
#define esyslog_rb(format, arg...) esyslog("reelbox: ERROR " format, ## arg)
#define dsyslog_rb(format, arg...) dsyslog("reelbox: DEBUG %s " format, __FUNCTION__, ## arg)

#endif
