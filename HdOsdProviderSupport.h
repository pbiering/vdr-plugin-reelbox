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
            if (Result == oeAreasOverlap) { \
                eOsdError ResultYplus1 = oeOk; \
                for (int i = 0; i < numAreas; i++) { \
                    for (int j = i + 1; j < numAreas; j++) { \
                        if (!(areas[j].x2 < areas[i].x1 || areas[j].x1 > areas[i].x2 || areas[j].y2 < areas[i].y1 || areas[j].y1 + 1 > areas[i].y2)) { \
                            ResultYplus1 = oeAreasOverlap; \
                            break; \
                        } \
                    } \
                } \
                if (ResultYplus1 == oeOk) { \
                    Result = oeOk; \
                    dsyslog_rb("cOsd::CanHandleAreas already returned Result=%d (%s) (ignored because Y+1)\n", Result, OsdErrorTexts[Result]); \
                    for (int i = 0; i < numAreas; i++) \
                       dsyslog_rb("overlap problem (ignored because Y+1): area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
                } else { \
                    esyslog_rb("cOsd::CanHandleAreas already returned Result=%d (%s)\n", Result, OsdErrorTexts[Result]); \
                    for (int i = 0; i < numAreas; i++) \
                       esyslog_rb("overlap problem: area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
                } \
            } else { \
                esyslog_rb("cOsd::CanHandleAreas already returned Result=%d (%s)\n", Result, OsdErrorTexts[Result]); \
            } \
    	    return Result; \
       	} \
        \
        for (int i = 0; i < numAreas; i++) \
        { \
            DEBUG_RB_OSD_AR("check: area=%i bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
            if (bpp_check) \
            { \
                wsyslog_rb("area color depth not supported: i=%d *bpp=%d* x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height()); \
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
