
#ifndef LOGGING_H_INCLUDED
#define LOGGING_H_INCLUDED

#include <vdr/tools.h>

extern int m_debugmask;

#define DEBUG_MASK_PLAYTS	0x00000001	// PlayTS
#define DEBUG_MASK_VPHD		0x00000010	// VideoPlayerHd
#define DEBUG_MASK_RB_OSD_DTRF	0x00000100	// OSD draw text "red frame drawing" code
#define DEBUG_MASK_RB_OSD_DRBF	0x00000200	// OSD draw rectangle "blue frame drawing" code
#define DEBUG_MASK_RB_OSD_DTSC	0x00000800	// OSD draw text "single char"
#define DEBUG_MASK_RB_OSD_DF	0x00001000	// OSD draw figures
#define DEBUG_MASK_RB_OSD_UD	0x00002000	// OSD update dirty
#define DEBUG_MASK_RB_OSD_DR	0x00004000	// OSD draw rectange
#define DEBUG_MASK_RB_OSD_DT	0x00008000	// OSD draw text
#define DEBUG_MASK_RB_OSD	0x00010000	// OSD general
#define DEBUG_MASK_RB_OSD_SC	0x00020000	// OSD SendCmd
#define DEBUG_MASK_RB_OSD_SP	0x00040000	// OSD special
#define DEBUG_MASK_RB_OSD_AR	0x00080000	// OSD area related
#define DEBUG_MASK_RB_OSD_BM	0x00100000	// OSD Bitmap related
#define DEBUG_MASK_RB_OSD_PM	0x00200000	// OSD Pixmap related
#define DEBUG_MASK_RB_OSD_FL	0x00800000	// OSD Flush related
#define DEBUG_MASK_RB_OSD_AC	0x01000000	// OSD active
#define DEBUG_MASK_RB_OSD_CO	0x02000000	// OSD create
#define DEBUG_MASK_RB_PICT	0x10000000	// fs453settings

#if 0

#define isyslog_rb(format, arg...) isyslog("reelbox: INFO  %s " format, __FUNCTION__, ## arg)
#define esyslog_rb(format, arg...) esyslog("reelbox: ERROR %s " format, __FUNCTION__, ## arg)
#define dsyslog_rb(format, arg...) dsyslog("reelbox: DEBUG %s/%s: " format, __FILE__, __FUNCTION__, ## arg)
#define printf(format, arg...)     dsyslog("reelbox: DEBUG %s/%s: " format, __FILE__, __FUNCTION__, ## arg)

#else

// stderr debugging
#define isyslog_rb(format, arg...) fprintf(stderr, "reelbox: INFO  %s/%s " format, __FILE__, __FUNCTION__, ## arg)
#define wsyslog_rb(format, arg...) fprintf(stderr, "reelbox: WARN  %s/%s " format, __FILE__, __FUNCTION__, ## arg)
#define esyslog_rb(format, arg...) fprintf(stderr, "reelbox: ERROR %s/%s " format, __FILE__, __FUNCTION__, ## arg)
#define dsyslog_rb(format, arg...) fprintf(stderr, "reelbox: DEBUG %s/%s " format, __FILE__, __FUNCTION__, ## arg)
#define printf(format, arg...)     fprintf(stderr, "reelbox: DEBUG %s/%s " format, __FILE__, __FUNCTION__, ## arg)

#endif

#define DEBUG_RB_OSD	if (m_debugmask & DEBUG_MASK_RB_OSD)    dsyslog_rb
#define DEBUG_RB_OSD_SC	if (m_debugmask & DEBUG_MASK_RB_OSD_SC) dsyslog_rb
#define DEBUG_RB_OSD_DR	if (m_debugmask & DEBUG_MASK_RB_OSD_DR) dsyslog_rb
#define DEBUG_RB_OSD_DT	if (m_debugmask & DEBUG_MASK_RB_OSD_DT) dsyslog_rb
#define DEBUG_RB_OSD_DF	if (m_debugmask & DEBUG_MASK_RB_OSD_DF) dsyslog_rb
#define DEBUG_RB_OSD_UD	if (m_debugmask & DEBUG_MASK_RB_OSD_UD) dsyslog_rb
#define DEBUG_RB_OSD_SP	if (m_debugmask & DEBUG_MASK_RB_OSD_SP) dsyslog_rb
#define DEBUG_RB_OSD_AR	if (m_debugmask & DEBUG_MASK_RB_OSD_AR) dsyslog_rb
#define DEBUG_RB_OSD_BM	if (m_debugmask & DEBUG_MASK_RB_OSD_BM) dsyslog_rb
#define DEBUG_RB_OSD_PM	if (m_debugmask & DEBUG_MASK_RB_OSD_PM) dsyslog_rb
#define DEBUG_RB_OSD_FL	if (m_debugmask & DEBUG_MASK_RB_OSD_FL) dsyslog_rb
#define DEBUG_RB_OSD_AC	if (m_debugmask & DEBUG_MASK_RB_OSD_AC) dsyslog_rb
#define DEBUG_RB_OSD_CO	if (m_debugmask & DEBUG_MASK_RB_OSD_CO) dsyslog_rb
#define DEBUG_RB_PICT	if (m_debugmask & DEBUG_MASK_RB_PICT)   dsyslog_rb
#define DEBUG_RB_VPHD	if (m_debugmask & DEBUG_MASK_VPHD)	dsyslog_rb
#define DEBUG_RB_PLAYTS	if (m_debugmask & DEBUG_MASK_PLAYTS)	dsyslog_rb

#endif
