#ifndef HDOSDPROVIDERSUPPPORT_H_INCLUDED
#define HDOSDPROVIDERSUPPPOR_H_INCLUDED

// copy from osd.c (is defined as static there, can't be reused...)
static const char *OsdErrorTexts[] = {
  "ok",
  "too many areas",
  "too many colors",
  "bpp not supported",
  "areas overlap",
  "wrong alignment",
  "out of memory",
  "wrong area size",
  "unknown",
  };

#define DEBUG_DISPLAY_Areas() \
        for (int i = 0; i < numAreas; i++) \
            DEBUG_RB_OSD_AR("area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \

#define SUPPORT_CanHandleAreas(bpp_check, osd_width_max, osd_height_max) \
        if (numAreas == 0) { \
            DEBUG_RB_OSD_AR("called with numAreas=%i (nothing to do)\n", numAreas); \
            return oeOk; \
        }; \
        \
        DEBUG_RB_OSD_AR("called with numAreas=%i\n", numAreas); \
        \
        for (int i = 0; i < numAreas; i++) \
            DEBUG_RB_OSD_AR("area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
        \
        eOsdError Result = cOsd::CanHandleAreas(areas, numAreas); \
        if (Result != oeOk) \
        { \
            esyslog_rb("cOsd::CanHandleAreas already returned Result=%d (%s)\n", Result, OsdErrorTexts[Result]); \
    	  	if (Result == oeAreasOverlap) \
                for (int i = 0; i < numAreas; i++) \
                   esyslog_rb("overlap problem: area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
    	    return Result; \
       	} \
        \
        for (int i = 0; i < numAreas; i++) \
        { \
            DEBUG_RB_OSD_AR("check: area=%i bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
            if (bpp_check) \
            { \
                esyslog_rb("area color depth not supported: i=%d *bpp=%d* x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
                Result = oeBppNotSupported; \
                break; \
            }; \
            if (areas[i].Width() < 1 || areas[i].Width() > osd_width_max) \
            { \
                esyslog_rb("area width not supported: i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d *W=%d* H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
                Result = oeWrongAreaSize; \
                break; \
            } \
            if (areas[i].Height() < 1 || areas[i].Height() > osd_height_max) \
            { \
                esyslog_rb("area height not supported: i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d *H=%d*\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
                Result = oeWrongAreaSize; \
                break; \
            } \
        } \
        \
        DEBUG_RB_OSD_AR("Result=%d (%s)\n", Result, OsdErrorTexts[Result]);



#endif

// vim: ts=4 sw=4 et
