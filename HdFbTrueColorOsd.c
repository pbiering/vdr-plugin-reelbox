/***************************************************************************
 *   Copyright (C) 2005 by Reel Multimedia                                 *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

// HdFbTrueColorOsd.c

#include "HdOsdProviderSupport.h"
#include "HdFbTrueColorOsd.h"

#include <vdr/tools.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <cstring>
#include <iostream>
#include <algorithm>

#include <png.h>

#include "font_helper.h"
#include "logging.h"

#define DrawTextWithBitmap 1    // use VDR internal Bitmap/DrawText rendering

namespace Reel
{
#define MAX_CACHED_IMAGES 256
#ifdef DrawTextWithBitmap
    static cBitmap *cacheBitmap;
#endif

    static CachedImage *cachedImages_[MAX_CACHED_IMAGES];
    std::string    imagePaths_[MAX_CACHED_IMAGES];
    bool           imageDirty_[MAX_CACHED_IMAGES];
    static int savedRegion_x0, savedRegion_y0, savedRegion_x1, savedRegion_y1;

    typedef struct
    {
       int x0, y0;
       int x1, y1;
    } dirtyArea;

    static dirtyArea dirtyArea_; /** to be able remember where the OSD has changed, to only need to flush this area */

    static uint32_t last_p1, last_p2, last_res;

    static bool Initialize() {
	for(int i=0; i<MAX_CACHED_IMAGES; i++) {
	    cachedImages_[i]=0;
	    imageDirty_[i]=true;
	}
	savedRegion_x0 = savedRegion_y0 = savedRegion_x1 = savedRegion_y1 = 0;
	memset(&dirtyArea_, 0, sizeof(dirtyArea_));
	last_p1 = last_p2 = last_res = 0;
	return true;
    }
    static bool Inited = Initialize(); 

    static inline uint32_t AlphaBlend(uint32_t p2, uint32_t p1) {

        if(p1 == last_p1 && p2 == last_p2)
            return last_res;
        else {
            last_p1 = p1;
            last_p2 = p2;
        }

        uint8_t const xa1 = (p1 >> 24);
        uint8_t const xa2 = (p2 >> 24);

        //printf("blend: fg: %#x bg: %#x\n", p2, p1);

        if (xa2==255 || !xa1) {
            last_res = p2;
            return p2;
        } else if (!xa2) {
            last_res = p1;
            return p1;
        } else {
            uint8_t const a1 = 255 - xa1;
            uint8_t const a2 = 255 - xa2;

            uint8_t const r2 = (p2 >> 16);
            uint8_t const g2 = (p2 >> 8);
            uint8_t const b2 = p2;

            uint8_t const r1 = (p1 >> 16);
            uint8_t const g1 = (p1 >> 8);
            uint8_t const b1 = p1;

            uint32_t const al1_al2 = a1 * a2;
            uint32_t const al2     = a2 * 255;

            uint32_t const al1_al2_inv = 255*255 - al1_al2; // > 0

            uint32_t const c1 = al2 - al1_al2;
            uint32_t const c2 = 255*255 - al2;

            uint8_t const a = al1_al2 / 255;
            uint8_t const r = (c1 * r1 + c2 * r2) / al1_al2_inv;
            uint8_t const g = (c1 * g1 + c2 * g2) / al1_al2_inv;
            uint8_t const b = (c1 * b1 + c2 * b2) / al1_al2_inv;
            uint32_t res = last_res = ((255 - a) << 24) | (r << 16) | (g << 8) | b;
            return res;
        }
    }

    static inline void FlushOsd(osd_t *osd) {
	    DEBUG_RB_OSD_FL("called\n");
        if (HdCommChannel::hda->osd_dont_touch&~4) {
            DEBUG_RB_OSD("blocked by 'dont-touch' bit active\n");
            return;
        }
        //HdCommChannel::hda->plane[2].alpha = 255;
        //HdCommChannel::hda->plane[2].changed++;                

        int lines = dirtyArea_.y1 - dirtyArea_.y0;
        int pixels = dirtyArea_.x1 - dirtyArea_.x0;
        int rest = pixels%4;
        if (lines < 0 || pixels < 0) {
            DEBUG_RB_OSD_FL("called but nothing dirty to flush\n");
            return;
        };
        DEBUG_RB_OSD_FL("called and flush dirty area x0=%d y0=%d x1=%d y1=%d w=%d h=%d\n", dirtyArea_.x0, dirtyArea_.y0, dirtyArea_.x1, dirtyArea_.y1, pixels, lines);
        //printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>FlushOSD %d %d/%d-%d/%d w %d h %d\n", HdCommChannel::hda->osd_hidden, dirtyArea_.x0, dirtyArea_.y0, dirtyArea_.x1, dirtyArea_.y1, pixels, lines);
        if (pixels>0)
            pixels+=4-rest;
        if(pixels+dirtyArea_.x0 > (int)osd->width)
            pixels = osd->width - dirtyArea_.x0;
        int i, ii;
        //printf("dirty: %i %i %i %i lines: %i pixels: %i\n", dirtyArea_.x0, dirtyArea_.y0, dirtyArea_.x1, dirtyArea_.y1, lines, pixels);
        if(pixels > 0)
            for(ii = 0; ii < lines; ii++) {
                if(ii&1)
                    i = lines/2-ii/2;
                else
                    i = lines/2+ii/2;
#ifdef DECYPHER_BUG
                memcpy(osd->data + (i+dirtyArea_.y0)*osd->width*osd->bpp + dirtyArea_.x0*osd->bpp, osd->buffer + (i+dirtyArea_.y0)*osd->width*osd->bpp + dirtyArea_.x0*osd->bpp, pixels*osd->bpp);
#else
                int j;
                uint32_t *dst = ((uint32_t*)osd->data + (i+dirtyArea_.y0)*osd->width + dirtyArea_.x0);
                uint32_t *src = ((uint32_t*)osd->buffer + (i+dirtyArea_.y0)*osd->width + dirtyArea_.x0);
                for(j=0; j<pixels; j++)
#if 1
                  if(dst >= (uint32_t*)osd->data && dst < (uint32_t*)(osd->data+osd->height*osd->width*osd->bpp) &&
                    src >= (uint32_t*)osd->buffer  && src  < (uint32_t*)(osd->buffer+osd->height*osd->width*osd->bpp))
#endif
                    *(dst+j) = *(src+j);
#endif
            }
        // reset dirty area to "nothing"
        dirtyArea_.x0 = osd->width-1;
        dirtyArea_.y0 = osd->height-1;
        dirtyArea_.x1 = 0;
        dirtyArea_.y1 = 0;
    }

    static bool inline ClipArea(osd_t *osd, unsigned int *l,unsigned  int *t,unsigned  int *r,unsigned  int *b) {
        if (*r >= osd->width) {
            *r = osd->width-1;
        }
        if (*b >= osd->height) {
            *b = osd->height-1;
        }
        return *l < *r && *t < *b;
    }

    void HdFbTrueColorOsd::ClearOsd(osd_t *osd) {
	    DEBUG_RB_OSD("called\n");
        if(osd && osd->buffer && osd->data) {
	        DEBUG_RB_OSD("clear buffer width=%d height=%d bpp=%d\n", osd->width, osd->height, osd->bpp);
            memset(osd->buffer, 0x00, osd->width*osd->height*osd->bpp);
            memset(osd->data, 0x00, osd->width*osd->height*osd->bpp);
        }
    }

    void HdFbTrueColorOsd::SendOsdCmd(void const *bco, UInt bcoSize, void const *payload, UInt payloadSize)
    {
	    DEBUG_RB_OSD_SC("called\n");
        static char buffer[HD_MAX_DGRAM_SIZE];

        if(bcoSize + payloadSize > HD_MAX_DGRAM_SIZE)
		return;
#if 0
        int bufferSize = 0;
        while (!bufferSize)
        {
            bufferSize = ::hd_channel_write_start(osdChannel_,
                                                   &buffer,
                                                   bcoSize + payloadSize,
                                                   1000);
        }
#endif
        std::memcpy(buffer, bco, bcoSize);
        if (payloadSize)
        {
            std::memcpy(static_cast<Byte *>((void*)buffer) + bcoSize, payload, payloadSize);
        }
        //::hd_channel_write_finish(osdChannel_, bufferSize);

        hd_packet_es_data_t packet;
        //printf("sendpacket!\n");
        HdCommChannel::chOsd.SendPacket(HD_PACKET_OSD, packet, (Reel::Byte*)(void*)buffer, bcoSize + payloadSize);
    }

    //--------------------------------------------------------------------------------------------------------------

    int          HdFbTrueColorOsd::fontGeneration_ = 0;
    cFont const *HdFbTrueColorOsd::cachedFonts_[HDOSD_MAX_CACHED_FONTS];

    int volatile *HdFbTrueColorOsd::hdCachedFonts_;

    static png_structp png_ptr;
    static png_infop   info_ptr;
    extern int lastThumbWidth[2];
    extern int lastThumbHeight[2];
    extern const char *fbdev;

    //--------------------------------------------------------------------------------------------------------------

#if APIVERSNUM >= 10509 || defined(REELVDR)
    HdFbTrueColorOsd::HdFbTrueColorOsd(int left, int top, uint level)
    :   cOsd(left, top, level),
#else
    HdFbTrueColorOsd::HdFbTrueColorOsd(int left, int top)
    :   cOsd(left, top),
#endif
        /*osdChannel_(Hd::HdCommChannel::Instance().bsc_osd),*/
        dirty_(false)
    {
	    DEBUG_RB_OSD("called with left=%d top=%d\n", left, top);
        int ret = 0;
        hdCachedFonts_ = HdCommChannel::hda->osd_cached_fonts;
        //cachedImages_ = HdCommChannel::hda->osd_cached_images;
        
#if 0
        //TB: mark all images as dirty as they are not cached at startup
        for(int i=0; i<=HdFbTrueColorOsd::MaxImageId; i++)
            imageDirty_[i] = true; 
#endif
        numBitmaps = 0;

#ifdef DrawTextWithBitmap
        cacheBitmap = new cBitmap(720, 576, 8, 0, 0);
#endif

        //std::cout << "OSD: " << osd << std::endl;
        if(osd == NULL || (osd->data == NULL && osd->buffer == NULL)) {
            ret = new_osd();
            if (ret != 0) {
	            esyslog_rb("can't create Framebuffer TrueColor OSD on HDE\n");
                exit(1);
            };
        };

        if(255 != HdCommChannel::hda->plane[2].alpha) {
            HdCommChannel::hda->plane[2].alpha = 255;
            HdCommChannel::hda->plane[2].changed++;                
        }

        // reset dirty area to "nothing"
        dirtyArea_.x0 = osd->width-1;
        dirtyArea_.y0 = osd->height-1;
        dirtyArea_.x1 = 0;
        dirtyArea_.y1 = 0;
    }


    int HdFbTrueColorOsd::new_osd() {
	    DEBUG_RB_OSD("called with defined fbdev=%s\n", fbdev);
        osd = (osd_t*)malloc(sizeof(osd_t));

        if(!osd) {
            esyslog_rb("couldn't malloc required amount of OSD memory: 0x%lx\n", sizeof(osd_t));
            return 1;
        }

        osd->fd = open(fbdev, O_RDWR|O_NDELAY);
        if(osd->fd==-1) {
            esyslog_rb("couldn't open framebuffer device %s (%s) - kernel module load option: has_fb=1\n", fbdev, strerror(errno));

            if (strcmp(fbdev, FB_DEFAULT_DEVICE) == 0) {
                // don't try same device twice
                return 1;
            };

            esyslog_rb("fallback to default device %s\n", FB_DEFAULT_DEVICE);
            osd->fd = open(FB_DEFAULT_DEVICE, O_RDWR|O_NDELAY);
            if(osd->fd==-1) {
                esyslog_rb("couldn't open default framebuffer device %s (%s) - check /proc/fb and kernel module load option: has_fb=1\n", FB_DEFAULT_DEVICE, strerror(errno));
                return 1;
            };
            // successful opened default device
            fbdev = FB_DEFAULT_DEVICE;
        };

        //assert(fb_fd!=-1);
        struct fb_fix_screeninfo screeninfoFix;
        struct fb_var_screeninfo screeninfo;

        ioctl(osd->fd, FBIOGET_FSCREENINFO, &screeninfoFix);
        if (strcmp("hde_fb", screeninfoFix.id) != 0) {
            esyslog_rb("framebuffer device is not HDE (expected: 'hde_fb' have: '%s' - check /proc/fb and kernel module load option: has_fb=1): %s\n", screeninfoFix.id, fbdev);
            close(osd->fd);
            return 1;
        };

        ioctl(osd->fd, FBIOGET_VSCREENINFO, &screeninfo);
        DEBUG_RB_OSD("name=%s bpp=%d xres=%d yres=%d\n", screeninfoFix.id, screeninfo.bits_per_pixel, screeninfo.xres, screeninfo.yres);
 
        if (screeninfo.bits_per_pixel != 32) {
            screeninfo.bits_per_pixel = 32;
            if(ioctl(osd->fd, FBIOPUT_VSCREENINFO, &screeninfo)) {
                esyslog_rb("can't set VSCREENINFO on framebuffer device: %s\n", fbdev);
                close(osd->fd);
                return 1;
            }
        }

        osd->bpp = screeninfo.bits_per_pixel/8; 
        osd->width = screeninfo.xres;
        osd->height = screeninfo.yres;
 
        osd->data = (unsigned char*) mmap(0, osd->bpp * osd->width * osd->height, PROT_READ|PROT_WRITE, MAP_SHARED, osd->fd, 0);
        if(osd->data == MAP_FAILED ) {
          esyslog_rb("can't mmap framebuffer, allocating dummy storage via malloc\n");
          osd->bpp = 4;
          osd->width = 720;
          osd->height = 576;
          osd->data = (unsigned char*)malloc(osd->bpp * osd->width * osd->height);
        }
 
        osd->buffer = (unsigned char*) malloc(osd->bpp * osd->width * osd->height);
        if(!osd->buffer) {
            esyslog_rb("can't malloc required OSD buffer\n");
            return 1;
        }
        if (mySavedRegion == NULL)
            mySavedRegion = (unsigned char*) malloc(osd->bpp*osd->width*osd->height);
        if (mySavedRegion == NULL) {
            esyslog_rb("can't malloc required mySavedRegion\n");
            return 1;
        }

        DEBUG_RB_OSD("successfully created framebuffer OSD xres=%i yres=%i bpp=%i data=%p\n", osd->width, osd->height, osd->bpp, osd->data);
        return 0;
    }

    //--------------------------------------------------------------------------------------------------------------

    HdFbTrueColorOsd::~HdFbTrueColorOsd()
    {
        DEBUG_RB_OSD("called\n");

#if APIVERSNUM >= 10509 || defined(REELVDR)
        SetActive(false);
#else
        ClearOsd();
#endif
    }
    
    //--------------------------------------------------------------------------------------------------------------
    

#if APIVERSNUM >= 10509 || defined(REELVDR)
    void HdFbTrueColorOsd::SetActive(bool On)
    {
        DEBUG_RB_OSD_AC("called with On=%i\n", On);
        if (On != Active())
        {
            cOsd::SetActive(On);

            ClearOsd(osd);

            // if Active draw
            if (On)
            {
                HdCommChannel::hda->osd_dont_touch|=4;
                // should flush only if something is already drawn
                dirty_ =  true;
                Flush();
            } else {
                HdCommChannel::hda->osd_dont_touch&=~4;
                hdcmd_osd_clear_t const bco = {HDCMD_OSD_CLEAR, 0, 0, 720, 576};
                SendOsdCmd(bco); // Allow Xine to draw its content if available
            }
        }// if

    }
#endif
    //--------------------------------------------------------------------------------------------------------------
    /** Mark the rectangle between (x0, y0) and (x1, y1) as an area that has changed */
    void HdFbTrueColorOsd::UpdateDirty(int x0, int y0, int x1, int y1) {
        DEBUG_RB_OSD_UD("called with x0=%d y0=%d x1=%d y1=%d osd->width=%d osd->heigth=%d\n", x0, y0, x1, y1, osd->width, osd->height);
        if(x0 >= (int)osd->width)  x0 = osd->width-1;
        if(x1 >= (int)osd->width)  x1 = osd->width-1;
        if(y0 >= (int)osd->height) y0 = osd->height-1;
        if(y1 >= (int)osd->height) y1 = osd->height-1;

        if (dirtyArea_.x0 > x0)  dirtyArea_.x0 = x0;
        if (dirtyArea_.y0 > y0)  dirtyArea_.y0 = y0-1; //TB: -1 is needed, 'cause else the top line of the progress bar is missing in the replay display
        if (dirtyArea_.x1 < x1)  dirtyArea_.x1 = x1;
        if (dirtyArea_.y1 < y1)  dirtyArea_.y1 = y1;
    }

    /** Mark the rectangle between (x0, y0) and (x1, y1) as an area that has changed */
    int HdFbTrueColorOsd::CacheFont(cFont const &font)
    {
#if 0 //def REELVDR
        int const fontGen = cFont::GetGeneration();
        if (fontGeneration_ != fontGen)
        {
            ClearFontCache();
            fontGeneration_ = fontGen;
        }
#endif

        // Search cache for font.
        for (int n = 0; n < HDOSD_MAX_CACHED_FONTS; ++n)
        {
            if (!hdCachedFonts_[n])
            {
                cachedFonts_[n] = 0;
            }
            if (&font == cachedFonts_[n])
            {
                // Cache hit, return index.
                return n;
            }
        }

        int const fontHeight = font.Height();

        if (fontHeight == 0)
        {
            return -1;
        }
        if (fontHeight > 32)
        {
            esyslog_rb("HdFbTrueColorOsd font height > 32\n");
            return -1;
        }


        // Font not in cache.
        // Search for empty slot.
        int ind = -1;
        for (int n = 0; n < HDOSD_MAX_CACHED_FONTS; ++n)
        {
            if (!cachedFonts_[n])
            {
                // Empty slot found.
                ind = n;
                cachedFonts_[n] = &font;
                break;
            }
        }
        if (ind == -1)
        {
            // Cache full.
            ClearFontCache();
            ind = 0;
        }


        hdcmd_osd_cache_font const bco = {HDCMD_OSD_CACHE_FONT,
                                           ind};

        UInt const charDataSize = (fontHeight + 2) * sizeof(UInt);
        UInt const payloadSize = charDataSize * 255; //cFont::NUMCHARS;

        void *buffer = (void*)malloc(sizeof(hdcmd_osd_cache_font) + payloadSize);

/*        int bufferSize = 0;

        while (!bufferSize)
        {
            bufferSize = ::hd_channel_write_start(osdChannel_,
                                                   &buffer,
                                                   sizeof(hdcmd_osd_cache_font) + payloadSize,
                                                   1000);
        }
*/
        std::memcpy(buffer, &bco, sizeof(hdcmd_osd_cache_font));

#if 0 // def REELVDR
        Byte *p = static_cast<Byte *>(buffer) + sizeof(hdcmd_osd_cache_font);

        for (int c = 0; c < cFont::NUMCHARS; ++c)
        {
            cFont::tCharData const *charData = font.CharData(implicit_cast<unsigned char>(c));
            std::memcpy(p, charData, charDataSize);
            p += charDataSize;
        }
#else
#if 0
        cFont::tCharData *charData;
        for (int c = 0; c < cFont::NUMCHARS; ++c)
        {
          if (c < 32)
          {
            charData->width = 1;
            charData->height = fontHeight;

            for (int i = 0 ; i < 22; i++)
            {
              charData->lines[i] = 0x00000000;
            }
          }
          
          else
          {
            charData->width = FontSml_iso8859_15[c-32][0];
            charData->height = FontSml_iso8859_15[c-32][1];

            for (int i = 0 ; i < 22; i++)
            {
              charData->lines[i] = FontSml_iso8859_15[c-32][i+2];
            }
          }
            
            //cFont::tCharData const *charData = font.CharData(implicit_cast<unsigned char>(c));
            std::memcpy(p, charData, charDataSize);
            p += charDataSize;
        }
#endif
#endif
        //hd_packet_es_data_t packet;
        //printf("DDDD: size: %i\n",  sizeof(hdcmd_osd_cache_font) + payloadSize);
    //HdCommChannel::chOsd.SendPacket(HD_PACKET_OSD, packet, (Reel::Byte*)buffer, sizeof(hdcmd_osd_cache_font) + payloadSize);
        //::hd_channel_write_finish(osdChannel_, bufferSize);
        free(buffer);
        return ind;
        return 0;
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ eOsdError HdFbTrueColorOsd::CanHandleAreas(tArea const *areas, int numAreas)
    {
        SUPPORT_CanHandleAreas(areas[i].bpp != 1 && areas[i].bpp != 2 && areas[i].bpp != 4 && areas[i].bpp != 8 && areas[i].bpp != 32 || !RBSetup.TRCLosd, 720, 576)
#if 0

        DEBUG_RB_OSD_AR("called with numAreas=%i\n", numAreas);
        for (int i = 0; i < numAreas; i++)
        {
            DEBUG_RB_OSD_AR("area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d Width=%d Height=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height());
        }
/*
        if (numAreas != 1) {
            esyslog_rb("HdFbTrueColorOsd::CanHandleAreas numAreas = %d\n", numAreas);
            return oeTooManyAreas;
        }

        if (areas[0].bpp != 32) {
            esyslog_rb("HdFbTrueColorOsd::CanHandleAreas bpp = %d\n", areas[0].bpp);
            return oeBppNotSupported;
        }

        return oeOk;
*/
        eOsdError Result = cOsd::CanHandleAreas(areas, numAreas);
        if (Result == oeOk)
        {
            for (int i = 0; i < numAreas; i++)
            {
                if (areas[i].bpp != 1 && areas[i].bpp != 2 && areas[i].bpp != 4 && areas[i].bpp != 8
                    && (areas[i].bpp != 32 || !RBSetup.TRCLosd))
                {
                    esyslog_rb("area color depth not supported: i=%d bpp=%d\n", i, areas[i].bpp);
                    Result = oeBppNotSupported;
                    break;
                }
                if (areas[i].Width() < 1 || areas[i].Height() < 1 || areas[i].Width() > 720 || areas[i].Height() > 576)
                {
                    esyslog_rb("area size not supported: i=%d w=%d h=%d\n", i, areas[i].Width(), areas[i].Height());
                    Result = oeWrongAreaSize;
                    break;
                }
            }
        }
        else
        {
            DEBUG_RB_OSD_AR("cOsd::CanHandleAreas already returned Result != oeOk: %d\n", Result);
        }
        DEBUG_RB_OSD_AR("Result=%d (%s)\n", Result, OsdErrorTexts[Result]);
#endif
        return Result;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    void HdFbTrueColorOsd::ClearFontCache()
    {
        for (int n = 0; n < HDOSD_MAX_CACHED_FONTS; ++n) {
            cachedFonts_[n] = 0;
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    void HdFbTrueColorOsd::ClosePngFile()
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawScaledBitmap(int X, int Y, const cBitmap &bitmap, double FactorX, double FactorY, bool AntiAlias)
    {
        DEBUG_RB_OSD_BM("called (and forward to cOsd) with X=%d Y=%d w=%d h=%d FactorX=%lf FactorY=%lf AntiAlias=%d\n", X, Y, bitmap.Width(), bitmap.Height(), FactorX, FactorY, AntiAlias);

        cOsd::DrawScaledBitmap(X, Y, bitmap, FactorX, FactorY, AntiAlias);

        return;
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawBitmap(int X,
                                                    int Y,
                                                    const cBitmap &bitmap,
                                                    tColor colorFg,
                                                    tColor colorBg,
                                                    bool replacePalette,
                                                    bool blend)
    {
        unsigned char const *srcData = bitmap.Data(0,0); //(unsigned char const *)(bco->data);
        unsigned char const *xs;
        unsigned int qx, qy, xt1, yt1, xt, yt, vfx, vfy, vfw, vfh, x, y, w, h, m, *px, n;

        x = X + Left();
        y = Y + Top();
        w = bitmap.Width();
        h = bitmap.Height();

        DEBUG_RB_OSD_BM("called with X=%d Y=%d w=%d h=%d colorFg=0x%08x colorBg=0x%08x blend=%d\n", X, Y, w, h, colorFg, colorBg, blend);

        if (w+x>osd->width || y+h>osd->height)
        {
            DEBUG_RB_OSD_BM("bitmap out-of-OSD-range: x=%d w=%d osd->width=%d y=%d h=%d osd->height=%d\n", x, w, osd->width, y, h, osd->height);
            return;
        };

        UpdateDirty(x, y, x+w, y+h);

        if (blend) {
            vfx = 0;
            vfy = 0;
            vfw = osd->width;
            vfh = osd->height;
    
            xt1 = vfx * osd->width + x * vfw;
            xt = xt1 / osd->width;
            yt1 = vfy * osd->height + y * vfh;
            yt = yt1 / osd->height;
            qy = yt1 % osd->height;
        
            for(m=0;m<h;) {
                px=(unsigned int*)(osd->buffer + osd->width * yt * osd->bpp  + xt * osd->bpp );
                xs = srcData + m * w;
                qx = xt1 % osd->width;
#ifdef OSD_EXTRA_CHECK
                if(WITHIN_OSD_BORDERS(px+w-1))
#endif
                for(n=0;n<w;) {
                    *px++=bitmap.Color(*xs);
                    qx += osd->width;
                    while (qx >= osd->width) {
                        ++xs;
                        ++n;
                        qx -= vfw;
                    }
                }
                ++yt;
                qy += osd->height;
                while (qy >= osd->height) {
                    ++m;
                    qy -= vfh;
                }
            }
        } else {
            unsigned int *bm = (unsigned int*)bitmap.Data(0,0); //bco->data;
            int o=0, val = 0;

            for (m=0;m<h;m++) {
                px=(unsigned int*)(osd->buffer +osd->width*(y+m)*osd->bpp+x*osd->bpp);
#if OSD_EXTRA_CHECK
                if(WITHIN_OSD_BORDERS(px+w-1))
#endif
                for(n=0;n<w;n++) {
                    if ((o&3)==0)
                            val=*bm++;
                        *px++=bitmap.Color(val&255);
                        val>>=8;
                        o++;
                }
            }
        }
        dirty_ = true;
    }

    /* override */ void HdFbTrueColorOsd::DrawBitmap32(int X,
                                                    int Y,
                                                    const cBitmap &bitmap,
                                                    tColor colorFg,
                                                    tColor colorBg,
                                                    bool replacePalette,
                                                    bool blend, int width, int height)
    {
        DEBUG_RB_OSD("called with x=%d y=%d w=%d h=%d BmW=%d BmH=%d\n", X, Y, width, height, bitmap.Width(), bitmap.Height());
        if (bitmap.Width() < 0 || bitmap.Height() < 0) {
            DEBUG_RB_OSD("bitmap has no size BmW=%d BmH=%d\n", bitmap.Width(), bitmap.Height());
            return;
        };

        unsigned char const *srcData = bitmap.Data(0,0); //(unsigned char const *)(bco->data);
        unsigned char const *xs;
        static unsigned int qx, qy, xt1, /* yt1, */ x, y, w, h, *px, line, row; // FIXED: variable 'yt1' set but not used
        static unsigned int pxs;

        if (X+w>osd->width || Y+h>osd->height)
        {
            DEBUG_RB_OSD_BM("bitmap out-of-OSD-range: x=%d w=%d osd->width=%d y=%d h=%d osd->height=%d\n", x, w, osd->width, y, h, osd->height);
            return;
        };

        x = X + Left();
        y = Y + Top();
        w = bitmap.Width();
        h = bitmap.Height();
        UpdateDirty(x, y, x+w, y+h);

        xt1 = x * osd->width;
        // yt1 = y * osd->height; // FIXED: variable 'yt1' set but not used
        qy = 0;
        
        for(line=0;line<h; y++) {
            px=(unsigned int*)(osd->buffer + osd->width * y * osd->bpp  + x * osd->bpp);
            xs = srcData + line * w;
            qx = xt1 % osd->width;
#ifdef OSD_EXTRA_CHECK
            if(WITHIN_OSD_BORDERS(px+w-1))
#endif
            for(row=0;row<w; px++) {
                pxs = bitmap.Color(*xs);
                if(pxs&0x00FFFFFF && pxs!=0x01ffffff){
                    *px = AlphaBlend(pxs, *px);
                } else {
                    *px = AlphaBlend(*px, pxs);
                }

                if (qx >= 0) {
                    xs++;
                    row++;
                    qx = 0;
                } else
                    qx += osd->width;
            }

            if (qy >= 0) {
                ++line;
                qy = 0;
            } else
                qy += osd->height;
        }
        dirty_ = true;
    }

    //--------------------------------------------------------------------------------------------------------------

    cPixmap *HdFbTrueColorOsd::CreatePixmap(int Layer, const cRect &ViewPort, const cRect &DrawPort) {
        //DEBUG_RB_OSD_PM("called with Layer=%d\n", Layer);
        dirty_ = true;
        return cOsd::CreatePixmap(Layer, ViewPort, DrawPort);
    };

    //--------------------------------------------------------------------------------------------------------------

    void HdFbTrueColorOsd::DrawPixmap(int X, int Y, const uint8_t *pmData, int W, int H, const int s) {
        DEBUG_RB_OSD_PM("called with X=%4d Y=%4d W=%4d H=%4d\n", X, Y, W, H);
        if (W < 0 || H < 0) {
            DEBUG_RB_OSD_PM("Pixmap H/W out-of-range W=%d H=%d (< 0) => skip\n", W, H);
            return;
        };

        if (X < 0) {
            DEBUG_RB_OSD_PM("Pixmap X out-of-range X=%d < 0 => shift right\n", X);
            X = 0;
        };

        if (Y < 0) {
            DEBUG_RB_OSD_PM("Pixmap Y out-of-range Y=%d < 0 => shift down\n", Y);
            Y = 0;
        };

        if (X + W > (int) osd->width) {
            DEBUG_RB_OSD_PM("Pixmap X+W out-of-range X+W=%d > osd-width=%d => crop right\n", X+W, osd->width);
        };

        if (Y + H > (int) osd->height) {
            DEBUG_RB_OSD_PM("Pixmap Y+H out-of-range Y+H=%d > osd->height=%d => crop bottom\n", Y+H, osd->height);
        };

        const uint8_t *pmm_pixel;
        static tColor pmm_pixel_color;
        static int line, row, x, y;
        static uint32_t *osd_pixel;

        x = X;
        y = Y;

        for (line = 0 ; line < H; line++) {
            if (y >= (int) osd->height) {
                // bottom of OSD reached
                break;
            };
            osd_pixel = (uint32_t*)(osd->buffer + (osd->width * y + x) * osd->bpp); // OSD TODO catch bpp < 32
            pmm_pixel = pmData + line * W * sizeof(tColor); // Pixmap Memory

            for(row = 0; row < W; row++) {
                pmm_pixel_color = *(uint32_t *)pmm_pixel;
                pmm_pixel += sizeof(tColor);
                if (x + row >= (int) osd->width) {
                    // right side of OSD reached
                    continue;
                };
                if (pmm_pixel_color & 0x00FFFFFF && pmm_pixel_color != 0x01ffffff) {
                    *osd_pixel = AlphaBlend(pmm_pixel_color, *osd_pixel);
                } else {
                    *osd_pixel = AlphaBlend(*osd_pixel, pmm_pixel_color);
                };
                osd_pixel++;
            };
            y++;
        }
        UpdateDirty(X, Y, X + W - 1, Y + H - 1);
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::HdFbTrueColorOsd::DrawBitmapHor(int x, int y, int w, const cBitmap &bitmap)
    {
        esyslog_rb("HdFbTrueColorOsd::DrawBitmapHor not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawBitmapVert(int x, int y, int h, const cBitmap &bitmap)
    {
        esyslog_rb("HdFbTrueColorOsd::DrawBitmapVert not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawEllipse(int X1, int Y1, int X2, int Y2, tColor color, int quadrants)
    {
        DEBUG_RB_OSD_DF("called\n");

        unsigned int l, t, r, b;
        l = /*Left() +*/ X1;
        t = /*Top() +*/ Y1;
        r = /*Left() +*/ X2 + 1;
        b = /*Top() +*/ Y2 + 1;

        int x1 = l;
        int y1 = t;
        int x2 = r - 1;
        int y2 = b - 1;
        UpdateDirty(x1, y1, x2, y2);

        int rx, ry, cx, cy;
        int TwoASquare, TwoBSquare, x, y, XChange, YChange, EllipseError, StoppingX, StoppingY;

        if (!ClipArea(osd, &l, &t, &r, &b))
            return;

        // Algorithm based on http://homepage.smc.edu/kennedy_john/BELIPSE.PDF
        rx = x2 - x1;
        ry = y2 - y1;
        cx = (x1 + x2) / 2;
        cy = (y1 + y2) / 2;
        switch (abs(quadrants)) {
            case 0: rx /= 2; ry /= 2; break;
            case 1: cx = x1; cy = y2; break;
            case 2: cx = x2; cy = y2; break;
            case 3: cx = x2; cy = y1; break;
            case 4: cx = x1; cy = y1; break;
            case 5: cx = x1;          ry /= 2; break;
            case 6:          cy = y2; rx /= 2; break;
            case 7: cx = x2;          ry /= 2; break;
            case 8:          cy = y1; rx /= 2; break;
        }
        TwoASquare = 2 * rx * rx;
        TwoBSquare = 2 * ry * ry;
        x = rx;
        y = 0;
        XChange = ry * ry * (1 - 2 * rx);
        YChange = rx * rx;
        EllipseError = 0;
        StoppingX = TwoBSquare * rx;
        StoppingY = 0;
        while (StoppingX >= StoppingY) {
            switch (quadrants) {
                case  5: DrawRectangle(cx,     cy + y, cx + x, cy + y, color); // no break
                case  1: DrawRectangle(cx,     cy - y, cx + x, cy - y, color); break;
                case  7: DrawRectangle(cx - x, cy + y, cx,     cy + y, color); // no break
                case  2: DrawRectangle(cx - x, cy - y, cx,     cy - y, color); break;
                case  3: DrawRectangle(cx - x, cy + y, cx,     cy + y, color); break;
                case  4: DrawRectangle(cx,     cy + y, cx + x, cy + y, color); break;
                case  0:
                case  6: DrawRectangle(cx - x, cy - y, cx + x, cy - y, color); if (quadrants == 6) break;
                case  8: DrawRectangle(cx - x, cy + y, cx + x, cy + y, color); break;
                case -1: DrawRectangle(cx + x, cy - y, x2,     cy - y, color); break;
                case -2: DrawRectangle(x1,     cy - y, cx - x, cy - y, color); break;
                case -3: DrawRectangle(x1,     cy + y, cx - x, cy + y, color); break;
                case -4: DrawRectangle(cx + x, cy + y, x2,     cy + y, color); break;
            }
            ++ y;
            StoppingY += TwoASquare;
            EllipseError += YChange;
            YChange += TwoASquare;
            if (2 * EllipseError + XChange > 0) {
                x--;
                StoppingX -= TwoBSquare;
                EllipseError += XChange;
                XChange += TwoBSquare;
            }
        }
        x = 0;
        y = ry;
        XChange = ry * ry;
        YChange = rx * rx * (1 - 2 * ry);
        EllipseError = 0;
        StoppingX = 0;
        StoppingY = TwoASquare * ry;
        while (StoppingX <= StoppingY) {
            switch (quadrants) {
                case  5: DrawRectangle(cx,     cy + y, cx + x, cy + y, color); // no break
                case  1: DrawRectangle(cx,     cy - y, cx + x, cy - y, color); break;
                case  7: DrawRectangle(cx - x, cy + y, cx,     cy + y, color); // no break
                case  2: DrawRectangle(cx - x, cy - y, cx,     cy - y, color); break;
                case  3: DrawRectangle(cx - x, cy + y, cx,     cy + y, color); break;
                case  4: DrawRectangle(cx,     cy + y, cx + x, cy + y, color); break;
                case  0:
                case  6: DrawRectangle(cx - x, cy - y, cx + x, cy - y, color); if (quadrants == 6) break;
                case  8: DrawRectangle(cx - x, cy + y, cx + x, cy + y, color); break;
                case -1: DrawRectangle(cx + x, cy - y, x2,     cy - y, color); break;
                case -2: DrawRectangle(x1,     cy - y, cx - x, cy - y, color); break;
                case -3: DrawRectangle(x1,     cy + y, cx - x, cy + y, color); break;
                case -4: DrawRectangle(cx + x, cy + y, x2,     cy + y, color); break;
            }
            x++;
            StoppingX += TwoBSquare;
            EllipseError += XChange;
            XChange += TwoBSquare;
            if (2 * EllipseError + YChange > 0) {
                y--;
                StoppingY -= TwoASquare;
                EllipseError += YChange;
                YChange += TwoASquare;
            }
        }
        dirty_ = true;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawHdImage(UInt imageId, int x, int y, bool blend,
                                                   int horRepeat, int vertRepeat)
    {
        DEBUG_RB_OSD("called\n");

        if (ImageIdInRange(imageId))
            LoadImage(imageId);

        x += Left();
        y += Top();

        CachedImage const *img = cachedImages_[imageId]; //hda->osd_cached_images[imageId];
        int m, n, h_, v;
        int w, h;
        uint32_t const *srcPixels;

        if(!img) return;

        if (img) {
            w = img->width;    
            h = img->height;
            unsigned int w_all = horRepeat ? horRepeat * w : w;
            unsigned int h_all = vertRepeat ? vertRepeat * h : h;
            if(horRepeat * w > Width()) horRepeat = Width() / w;
            if(vertRepeat * h > Height()) vertRepeat = Height() / h;
            UpdateDirty(x, y, x+w_all, y+h_all);

            w_all = horRepeat ? horRepeat * w : w;
            h_all = vertRepeat ? vertRepeat * h : h;

            //printf("X: %i y: %i x2: %i y2: %i vr: %i hr: %i\n", x, y, x+w_all, y+h_all, horRepeat, vertRepeat);

            if (blend) {
                for (v = vertRepeat; v > 0; --v) {
                    srcPixels = img->data;
                    for (n = img->height; n > 0; --n) {
                        uint32_t *tgtPixels = (uint32_t*)(osd->buffer + osd->bpp * osd->width * y++ + x*osd->bpp);
                        for (h_ = horRepeat; h_ > 0; --h_) {
                            uint32_t const *src = srcPixels;
#if OSD_EXTRA_CHECK
                            if(WITHIN_OSD_BORDERS(tgtPixels+w))
#endif
                            for (m = w; m > 0; --m) {
                                int res = AlphaBlend((*src++), (*tgtPixels) );          // where they come from (libpng?) - so let's filter them out

                                if( (res>>24)!=0 || (res&0xffffff) != 0xffffff){ // TB: there are white pixels on the left corner of the OSD - I don't know
                                    *tgtPixels = res;          // where they come from (libpng?) - so let's filter them out
                                } else                            // explanation: those pixels are completely transparent, but there 
                                    *tgtPixels = 0x00000000;    // is no alpha-blending done as long as there isn't any video data
                                ++tgtPixels;
                            }
                        }
                        srcPixels += w;
                    }
                }
            } else {
                for (v = vertRepeat; v > 0; --v) {
                    srcPixels = img->data;
                    for (n = h; n > 0; --n) {
                        uint32_t *tgtPixels = (uint32_t*)(osd->buffer + osd->bpp * osd->width * y++ + x*osd->bpp);
                        for (h_ = horRepeat; h_ > 0; --h_) {
                            uint32_t const *src = srcPixels;
#if OSD_EXTRA_CHECK
                            if(WITHIN_OSD_BORDERS(tgtPixels+img->width*(sizeof(int))))
#endif
#if 0
                            memcpy(tgtPixels, src, w*sizeof(int));
                            tgtPixels+=w;
                            src+=w;
#else
                            for (m = w; m > 0; --m) {
                                if((*src>>24)!=0 || (*src&0xffffff) != 0xffffff){ // TB: there are white pixels on the left corner of the OSD - I don't know
                                    *tgtPixels = *src;          // where they come from (libpng?) - so let's filter them out
                                } else                            // explanation: those pixels are completely transparent, but there 
                                    *tgtPixels = 0x00000000;    // is no alpha-blending done as long as there isn't any video data
                                ++src;
                                ++tgtPixels;
                            }
#endif
                        }
                        srcPixels += w;
                    }
                }
            }
            dirty_ = true;
        } else {
            printf("Image %d not cached.\n", imageId);
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawCropImage(UInt imageId, int x, int y, int x0, int y0, int x1, int y1, bool blend)
    {
        DEBUG_RB_OSD("called\n");

        if (! ImageIdInRange(imageId)) return;

        LoadImage(imageId);

        x+=Left();
        y+=Top();
        x0+=Left();
        y0+=Top();
        x1+=Left();
        y1+=Top();
        UpdateDirty(x0, y0, x1, y1);

        CachedImage const *img = cachedImages_[imageId];
        int m, n, h, v;
        // int width_;    // FIXED: variable 'width_' set but not used
        int height_;    
        uint32_t const *srcPixels;
        int vertRepeat = 1;
        int horRepeat = 1;

        if (img) {
            // width_ = x1-x0; // FIXED: variable 'width_' set but not used
            height_ = y1-y0;
            if(img->height)
                vertRepeat = (y1-y0)/img->height;
            if(vertRepeat < 1) vertRepeat = 1;
            if(img->width)
                  horRepeat = (x1-x0)/img->width;
            if(horRepeat < 1) horRepeat = 1;

            if (blend) {
                for (v = vertRepeat; v > 0; --v) {
                    srcPixels = img->data + img->width*(y0-y) + (x0-x);
                    for (n = height_; n > 0; --n) {
                        unsigned int *tgtPixels = (unsigned int*)(osd->buffer + osd->bpp * osd->width * y0++ + x0*osd->bpp);
                        for (h = horRepeat; h > 0; --h) {
                            unsigned int const *src = srcPixels;
                            for (m = Width(); m > 0; --m) {
                                *tgtPixels = AlphaBlend((*src++), (*tgtPixels) );
                                ++tgtPixels;
                            }
                        }
                        srcPixels += img->width;
                    }
                }
            } else {
                for (v = vertRepeat; v > 0; --v) {
                    srcPixels = img->data + img->width*(y0-y) + (x0-x);
                    for (n = height_; n > 0; --n) {
                    unsigned int *tgtPixels = (unsigned int*)(osd->buffer + osd->bpp * osd->width * y0++ + x0*osd->bpp);
                        for (h = horRepeat; h > 0; --h) {
                            unsigned int const *src = srcPixels;
                            memcpy(tgtPixels, src, img->width*sizeof(int));
                            tgtPixels += img->width;
                        }
                        srcPixels += img->width;
                    }
                }
            }
            dirty_ = true;
        } else {
            printf("Image %d not cached.\n", imageId);
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawPixel(int x, int y, tColor color)
    {
    //esyslog("HdFbTrueColorOsd: DrawPixel\n");
        esyslog_rb("HdFbTrueColorOsd::DrawPixel not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------
    static unsigned int draw_linebuf[1024]={0};
    static unsigned int draw_color=0;

    /* override */ void HdFbTrueColorOsd::DrawRectangle(int x1, int y1, int x2, int y2, tColor color)
    {
	    DEBUG_RB_OSD_DF("called with: x1=%d y1=%d x2=%d y2=%d color=%08x\n", x1, y1, x2, y2, color);

        unsigned int l, t, r, b;
        l = Left() + x1;
        t = Top() + y1;
        r = Left() + x2 + 1;
        b = Top() + y2 + 1;

        if (ClipArea(osd, &l, &t, &r, &b)) {
            UpdateDirty(l, t, r, b);

            unsigned int m, n;
            unsigned int width = r - l;
            unsigned int height = b - t;
            unsigned int *pixels = (unsigned int*)(osd->buffer + osd->width * t *osd->bpp  + l*osd->bpp);

            if (draw_color!=color) {
                for(n=0;n<1024;n++)
                    draw_linebuf[n]=color;
                draw_color=color;
            }
            for (m = height; m; --m) {
#if OSD_EXTRA_CHECK
                if(WITHIN_OSD_BORDERS(pixels+width*sizeof(int)))
#endif
                memcpy(pixels,draw_linebuf,width*sizeof(int));
                pixels+=osd->width;
            }
            dirty_ = true;

            // DEBUG_MASK_RB_OSD_DRBF: rectangle around background
            if (m_debugmask & DEBUG_MASK_RB_OSD_DRBF) {
                unsigned int xF;
                unsigned int yF;
                for (xF = l; xF < r; xF++) {
                    //line  top
                    uint32_t *dstPxFt = (uint32_t*)(osd->buffer + osd->width * t * osd->bpp  + xF * osd->bpp);
                    *dstPxFt = clrBlue;

                    // line bottom
                    uint32_t *dstPxFb = (uint32_t*)(osd->buffer + osd->width * (b - 1) * osd->bpp  + xF * osd->bpp);
                    *dstPxFb = clrBlue;
                };
                for (yF = t; yF < b; yF++) {
                    // line left
                    uint32_t *dstPxFl = (uint32_t*)(osd->buffer + osd->width * yF * osd->bpp  + l * osd->bpp);
                    *dstPxFl = clrBlue;

                    // line right
                    uint32_t *dstPxFr= (uint32_t*)(osd->buffer + osd->width * yF * osd->bpp  + (r - 1) * osd->bpp);
                    *dstPxFr = clrBlue;
                };
            };
        }
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawRectangle(int x1, int y1, int x2, int y2, tColor color, int alphaGradH, int alphaGradV, int alphaGradStepH, int alphaGradStepV)
    {
        DEBUG_RB_OSD("called with: x1=%d y1=%d x2=%d y2=%d color=%08x alphaGradH=%d alphaGradV=%d, alphaGradStepH=%d alphaGradStepV=%d\n", x1, y1, x2, y2, color, alphaGradH, alphaGradV, alphaGradStepH, alphaGradStepV);

        unsigned int l, t, r, b;
        l = Left() + x1;
        t = Top() + y1;
        r = Left() + x2 + 1;
        b = Top() + y2 + 1;

        if (ClipArea(osd, &l, &t, &r, &b)) {
            UpdateDirty(l, t, r, b);

            unsigned int m, n;
            unsigned int width = r - l;
            unsigned int height = b - t;
            unsigned int *pixels = (unsigned int*)(osd->buffer + osd->width * t *osd->bpp  + l*osd->bpp);

            if (draw_color!=color) {
                for(n=0;n<1024;n++)
                    draw_linebuf[n]=color;
                draw_color=color;
            }
            for (m = height; m; --m) {
#if OSD_EXTRA_CHECK
                if(WITHIN_OSD_BORDERS(pixels+width*sizeof(int)))
#endif
                memcpy(pixels,draw_linebuf,width*sizeof(int));
                pixels+=osd->width;
            }
            dirty_ = true;
        }
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawSlope(int x1, int y1, int x2, int y2, tColor color, int type)
    {
        esyslog_rb("HdFbTrueColorOsd::DrawSlope not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::DrawText(int x, int y, const char *s_in, tColor colorFg, tColor colorBg,
                                                  const cFont *font, int w, int h, int alignment)
    {
        if (!s_in) return;

        DEBUG_RB_OSD_DT("called with: colorFg=%08x colorBg=%08x x=%i y=%i w=%i h=%i align=0x%02x Setup.AntiAlias=%d font->Height=%d '%s'\n", colorFg, colorBg, x, y, w, h, alignment, Setup.AntiAlias, font->Height(), s_in);

        if (x < 0 || y < 0) {
            esyslog_rb("out-of-range: x=%i y=%i w=%i h=%i '%s'\n", x, y, w, h, s_in);
            return;
        };

        /* check for empty string */
        unsigned int i;

        for( i = 0; i < strlen(s_in); i++){
            if(s_in[i] == ' ')
                continue;
            else
                break;
        }
        if (i == strlen(s_in)) return;

        // code alignment with HdTrueColorOsd.c
        int width = w;
        int height = h;

        if (width == 0) width = font->Width(s_in);
        if (height == 0) height=font->Height();

#ifdef DrawTextWithBitmap
        // working code from HdTrueColorOsd.c, use VDR internal Bitmap/DrawText rendering
        cacheBitmap->SetSize(width, height);

        if(colorBg != clrTransparent) // not transparent
            DrawRectangle(Left()+x, Top()+y, width, height, colorBg); // clear the background

        DEBUG_RB_OSD_DT("draw text into bitmap: colorFg=%08x colorBg=%08x w=%i h=%i '%s'\n", colorFg, colorBg, width, height, s_in);
        cacheBitmap->DrawText(0, 0, s_in, colorFg, colorBg, font, width, height, alignment);

        // DEBUG_MASK_RB_OSD_DTRF: rectangle around background
        if (m_debugmask & DEBUG_MASK_RB_OSD_DTRF) {
            int xF;
            int yF;
            for (xF = 0; xF < width; xF++) {
                //line  top
                cacheBitmap->DrawPixel(xF, 0, clrRed);

                // line bottom
                cacheBitmap->DrawPixel(xF, height - 1, clrRed);
            };
            for (yF = 0; yF < height; yF++) {
                // line left
                cacheBitmap->DrawPixel(0, yF, clrRed);

                // line right
                cacheBitmap->DrawPixel(width - 1, yF, clrRed);
            };
        };

        DEBUG_RB_OSD_DT("draw bitmap: colorFg=%08x colorBg=%08x x=%i y=%i\n", colorFg, colorBg, x, y);
        DrawBitmap(x, y, *cacheBitmap, colorFg, colorBg, false, false);

# else
        // BROKEN CODE, rendering will exceet pre-calculated Text Width
        if(i == len) { /* every char is a space */
//            if((colorBg >> 24) != 0) /* not transparent */
//                DrawRectangle(Left()+x, Top()+y, x + w - 1, y + h - 1, colorBg); /* clear the background */
            if(colorBg != clrTransparent) /* not transparent */
                DrawRectangle(x, y, x + w - 1, y + h - 1, colorBg); /* clear the background */
            return;
        }

        if(colorBg != clrTransparent) /* not transparent */
            DrawRectangle(x, y, x + w - 1, y + h -1 , colorBg); /* clear the background */

        // DEBUG_MASK_RB_OSD_DTRF: rectangle around background prepraration
        int xFs = x + Left();
        int yFs = y + Top();
        int wFs = w;
        int hFs = h;

        // UpdateDirty(Left()+x, Top()+y, x+w, y+h); // duplicate

//        if((colorBg >> 24 == 0) || ((colorBg&0x00ffffff) == 0x00000000)){ /* TB: transparent as bgcolor is evil */
//            colorBg = colorFg&0x01ffffff; 
//        }

//        if((colorBg >> 24) != 0) /* not transparent */
//            DrawRectangle(x, y, x + w, y + h, colorBg); /* clear the background */


        int old_x = x; int old_y = y; y=0; x=0;

        int w_ = font->Width(s_in);
        int h_ = font->Height();
        int limit = 0;
        if (w || h) {
            int cw = w ? w : w_;
            //int ch = h ? h : h_;
            limit = x + cw;
            if (w) {
               if ((alignment & taLeft) != 0)
                   x = 1
                  ;
               else if ((alignment & taRight) != 0) {
                  if (w_ < w)
                     x += w - w_ - 1;
                  }
               else { // taCentered
                  if (w_ < w)
                     x += (w - w_) / 2;
                  }
               }
            if (h) {
               if ((alignment & taTop) != 0)
                  ;
               else if ((alignment & taBottom) != 0) {
                  if (h_ < h)
                     y += h - h_;
                  }
               else { // taCentered
                  if (h_ < h)
                     y += (h - h_) / 2;
                  }
               }
            }
        DEBUG_RB_OSD_DT("align=0x%02x w=%d w_=%d => x=%d / h=%d h_=%d => y=%d\n", alignment, w, w_, x, h, h_, y);

        /* adjust coordinates later with global OSD-margins */
        int xOffset = Left();
        int yOffset = Top();

        bool AntiAliased = Setup.AntiAlias;
        bool TransparentBackground = (colorBg == clrTransparent);
        static int16_t BlendLevelIndex[MAX_BLEND_LEVELS]; // tIndex is 8 bit unsigned, so a negative value can be used to mark unused entries
        if (AntiAliased && !TransparentBackground)
            memset(BlendLevelIndex, 0xFF, sizeof(BlendLevelIndex)); // initializes the array with negative values
        cPalette palette;
        palette.Index(colorFg);
        uint prevSym = 0;

        while (*s_in) {
           int sl = Utf8CharLen(s_in);
           uint sym = Utf8CharGet(s_in, sl);
           s_in += sl;
           cGlyph *g = ((cFreetypeFont*)font)->Glyph(sym, AntiAliased);

           if (!g)
              continue;
           int kerning = ((cFreetypeFont*)font)->Kerning(g, prevSym);
           prevSym = sym;
           uchar *buffer = g->Bitmap();
           int symWidth = g->Width();
           int symLeft = g->Left();
           int symTop = g->Top();
           int symPitch = g->Pitch();

           if (limit && ((x + symWidth + symLeft + kerning - 1) > limit)) {
              if ((x + symLeft) >= limit) {
                  if (m_debugmask & DEBUG_MASK_RB_OSD_DTSC)
                     DEBUG_RB_OSD_DT("skip char: c='%c' (%04x) x=%d symWidth=%d symLeft=%d kerning=%d AdvanceX=%d limit=%d\n", sym, sym, x, symWidth, symLeft, kerning, g->AdvanceX(), limit);
                  break; // we don't invisible characters
              };

              if (m_debugmask & DEBUG_MASK_RB_OSD_DTSC)
                 DEBUG_RB_OSD_DT("part char: c='%c' (%04x) x=%d symWidth=%d symLeft=%d kerning=%d AdvanceX=%d limit=%d\n", sym, sym, x, symWidth, symLeft, kerning, g->AdvanceX(), limit);
              // print partial chars by overwriting limit, looks like Width calculation on lowres OSD has issues
           } else {
              if (m_debugmask & DEBUG_MASK_RB_OSD_DTSC)
                 DEBUG_RB_OSD_DT("draw char: c='%c' (%04x) x=%d symWidth=%d symLeft=%d kerning=%d AdvanceX=%d\n", sym, sym, x, symWidth, symLeft, kerning, g->AdvanceX());
           };

           int px_tmp_sum = symLeft + kerning + x;
           //int py_tmp_sum = y + (font->Height() - ((cFreetypeFont*)font)->Bottom() - symTop);
           //int py_tmp_sum = y + (font->Height() - font->Height()/8 - symTop);
           int py_tmp_sum = y + (font->Height() - font->Height()/4 - symTop);

           if (x + symWidth + symLeft + kerning > 0) {
              for (int row = 0; row < g->Rows(); row++) {
                  for (int pitch = 0; pitch < symPitch; pitch++) {
                      uchar bt = *(buffer + (row * symPitch + pitch));
                      if (AntiAliased) {
                         if (bt > 0x00) {
                            int px = pitch + px_tmp_sum;
                            int py = row + py_tmp_sum;

                            uint32_t *dstPx = (uint32_t*)(osd->buffer + osd->width * (py + old_y + yOffset) * osd->bpp  + (px +old_x + xOffset) * osd->bpp);

                            if (bt == 0xFF)
                               *dstPx = colorFg;
                            else if (TransparentBackground)
                               *dstPx = palette.Blend(colorFg, *dstPx, bt);
                            else if (BlendLevelIndex[bt] >= 0)
                               *dstPx = palette.Blend(palette.Color(BlendLevelIndex[bt]), *dstPx, bt);
                            else
                               *dstPx = palette.Blend(colorFg, *dstPx, bt);
                            }
                      }
                      else
                      { //monochrome rendering
                         for (int col = 0; col < 8 && col + pitch * 8 <= symWidth; col++) {

                             if (bt & 0x80) {
//                                uint32_t *dstPx = (uint32_t*)(osd->buffer + osd->width * (old_y + y + row + (font->Height() - ((cFreetypeFont*)font)->Bottom() - symTop)) * osd->bpp  + (old_x + x + col + pitch * 8 + symLeft + kerning) * osd->bpp );
                                uint32_t *dstPx = (uint32_t*)(osd->buffer + osd->width * (old_y + y + yOffset + row + (font->Height() -font->Height()/8 - symTop)) * osd->bpp  + (old_x + x + xOffset + col + pitch * 8 + symLeft + kerning) * osd->bpp);
                                *dstPx = colorFg;
                             }
                             bt <<= 1;
                         }
                      }
                  }
              }
           } // if
           x += g->AdvanceX() + kerning;
           if (x > w - 1) {
              if (m_debugmask & DEBUG_MASK_RB_OSD_DTSC)
                 DEBUG_RB_OSD_DT("draw char: STOP because x=%d > w=%d + 1\n", x, w);
              break;
           };
        } // while

        // DEBUG_MASK_RB_OSD_DTRF: rectangle around background
        if (m_debugmask & DEBUG_MASK_RB_OSD_DTRF) {
            int xF;
            int yF;
            for (xF = xFs; xF < xFs + wFs; xF++) {
                //line  top
                uint32_t *dstPxFt = (uint32_t*)(osd->buffer + osd->width * yFs * osd->bpp  + xF * osd->bpp);
                *dstPxFt = clrRed;

                // line bottom
                uint32_t *dstPxFb = (uint32_t*)(osd->buffer + osd->width * (yFs + hFs - 1) * osd->bpp  + xF * osd->bpp);
                *dstPxFb = clrRed;
            };
            for (yF = yFs; yF < yFs + hFs; yF++) {
                // line left
                uint32_t *dstPxFl = (uint32_t*)(osd->buffer + osd->width * yF * osd->bpp  + xFs * osd->bpp);
                *dstPxFl = clrRed;

                // line right
                uint32_t *dstPxFr= (uint32_t*)(osd->buffer + osd->width * yF * osd->bpp  + (xFs + wFs - 1) * osd->bpp);
                *dstPxFr = clrRed;
            };
        };
#endif
    } // function
   
    //--------------------------------------------------------------------------------------------------------------

    static uint64_t get_now_time() {
        struct timespec spec;
        if (clock_gettime(1, &spec) == -1) { /* 1 is CLOCK_MONOTONIC */
            abort();
        }
        return spec.tv_sec * 1000 + spec.tv_nsec / 1e6;
    }

    /* override */ void HdFbTrueColorOsd::Flush()
    {
#if APIVERSNUM >= 10509 || defined(REELVDR)
        if (! Active()) return ;
#endif
        static uint64_t flush_last = 0;
        uint64_t flush_current, flush_delta;
        static const uint64_t flush_delta_max = 200; // ms
        flush_current = get_now_time();
        flush_delta = flush_current - flush_last;

        if (! dirty_) {
            if (flush_delta > flush_delta_max) {
                // auto-dirty
                DEBUG_RB_OSD_FL("called with dirty_=%d but delta > %lu => auto-dirty\n", dirty_, flush_delta);
                dirty_ = true;
            }
        } else {
            DEBUG_RB_OSD_FL("called with dirty_=%d\n", dirty_);
        };

        if (dirty_)
        {
            static int flushCount = 1;
            int pmCount = 0;

            flush_last = flush_current; // remember

            //DrawBitmap32(/*old_x, old_y*/ 0,0 /*bitmaps[0]->X0(), bitmaps[0]->Y0()*/, *bitmaps[0], old_colorFg, old_colorBg, false, false);

            LOCK_PIXMAPS;
            while (cPixmapMemory *pm = dynamic_cast<cPixmapMemory *>(RenderPixmaps())) {
                int w = pm->ViewPort().Width();
                int h = pm->ViewPort().Height();
                int d = w * sizeof(tColor);
                /*
                DEBUG_RB_OSD_PM(" call DrawPixmap X=%d Y=%d W=%d H=%d s=%d (drawPort X=%d Y=%d W=%d H=%d)\n"
                    , Left() + pm->ViewPort().X(), Top() + pm->ViewPort().Y(), w, h
                    , h * d
                    , pm->DrawPort().X(), pm->DrawPort().Y(), pm->DrawPort().Width(), pm->DrawPort().Height()
                );
                */
                DrawPixmap(Left() + pm->ViewPort().X(), Top() + pm->ViewPort().Y(), pm->Data(), w, h, h * d);
                DestroyPixmap(pm);
                pmCount++;
            }
            DEBUG_RB_OSD_FL("called DrawPixmap pmCount=%d\n", pmCount);

            hdcmd_osd_flush const bco = {HDCMD_OSD_FLUSH, flushCount};

            SendOsdCmd(bco);
            FlushOsd(osd);
            dirty_ = false;

#if 0
            int loop = 100;
            int osdFlushCount;
            do
            {
                ::usleep(10*1000);
                osdFlushCount = HdCommChannel::hda->osd_flush_count;
            }
            while (--loop && flushCount != osdFlushCount /*&& flushCount != osdFlushCount + 1*/);

            ++ flushCount;
#endif

        }
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ cBitmap *HdFbTrueColorOsd::GetBitmap(int area)
    {
#if 1
        return bitmaps[area];
#endif
        esyslog_rb("not supported\n");
        return NULL;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    bool HdFbTrueColorOsd::ImageIdInRange(UInt imageId)
    {

        if (imageId > MaxImageId) {
            esyslog_rb("HdFbTrueColorOsd Image id %u: Out of range.\n", imageId);
            return false;
        } else {
            return true;
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    bool HdFbTrueColorOsd::OpenPngFile(char const         *path,
                                      Byte **&rows,
                                      int                &width,
                                      int                &height)
    {

        Byte header[8];

        FILE *fp = fopen(path, "rb");
        if (!fp || fread(header, 1, 8, fp) != 8) {
            if(fp)
                fclose(fp);
            return false;
        }

        if (png_sig_cmp(header, 0, 8)) {
            fclose(fp);
            return false;
        }

        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png_ptr) {
            fclose(fp);
            return false;
        }
    
        info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr) {
            png_destroy_read_struct(&png_ptr,
                                    NULL, NULL);
            fclose(fp);
            return false;
        }
    
        png_infop end_info = png_create_info_struct(png_ptr);
        if (!end_info) {
            png_destroy_read_struct(&png_ptr, &info_ptr,
                                    (png_infopp)NULL);
            fclose(fp);
            return false;
        }

        png_init_io(png_ptr, fp);

        png_set_sig_bytes(png_ptr, 8);

        if (setjmp(png_jmpbuf(png_ptr))) {
            dsyslog_rb("setjmp err\n");
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            fclose(fp);
            return false;
        }

        png_read_info(png_ptr, info_ptr);
#if PNG_LIBPNG_VER < 10400
        png_byte h = info_ptr->height;
        png_byte color_type = info_ptr->color_type;
#else
        png_byte h = png_get_image_height(png_ptr, info_ptr);
        png_byte color_type = png_get_color_type(png_ptr, info_ptr);
#endif
//#define PRINT_COLOR_TYPE 1
#ifdef PRINT_COLOR_TYPE
#if PNG_LIBPNG_VER < 10400
        png_byte w = info_ptr->width;
        png_byte bit_depth = info_ptr->bit_depth;
#else
        png_byte w = png_get_image_width(png_ptr, info_ptr);
        png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
#endif
        printf("bit depth: %i - ", bit_depth);
        switch (color_type) {
                case PNG_COLOR_TYPE_GRAY: puts("color_type: PNG_COLOR_TYPE_GRAY"); break;
                case PNG_COLOR_TYPE_PALETTE: puts("color_type: PNG_COLOR_TYPE_PALETTE"); break;
                case PNG_COLOR_TYPE_RGB: puts("color_type: PNG_COLOR_TYPE_RGB"); break;
                case PNG_COLOR_TYPE_RGB_ALPHA: puts("color_type: PNG_COLOR_TYPE_RGB_ALPHA"); break;
                case PNG_COLOR_TYPE_GRAY_ALPHA: puts("color_type: PNG_COLOR_TYPE_GRAY_ALPHA"); break;
                default: puts("ERROR: unknown color_type"); break;
        }
#endif

        if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
          png_set_gray_to_rgb(png_ptr);

        if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY)
          png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

        png_read_update_info(png_ptr, info_ptr);

        rows = (png_bytep*) malloc(sizeof(png_bytep) * h);
        int y;
        for (y=0; y<h; y++) {
#if PNG_LIBPNG_VER < 10400
            rows[y] = (png_byte*) malloc(info_ptr->rowbytes);
#else
            rows[y] = (png_byte*) malloc(png_get_rowbytes(png_ptr, info_ptr));
#endif
        }

        png_read_image(png_ptr, rows);

        width = png_get_image_width(png_ptr, info_ptr);
        height = png_get_image_height(png_ptr, info_ptr);

        fclose(fp);
        return true;

    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::RestoreRegion()
    {
        int x1 = savedRegion_x0; int y1 = savedRegion_y0; int x2 = savedRegion_x1; int y2 = savedRegion_y1+1;
        int lines = y2 - y1+1;    
        int pixels = x2 - x1;
        int i;
        if (pixels > 0)
            for(i = 0; i < lines; i++)
                memcpy(osd->buffer + (i+y1)*osd->width*osd->bpp + x1*osd->bpp, mySavedRegion + (i+y1)*osd->width*osd->bpp + x1*osd->bpp, pixels*osd->bpp);
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdFbTrueColorOsd::SaveRegion(int x1, int y1, int x2, int y2)
    {
        x1 += Left(); y1 += Top(); x2 += Left(); y2 += Top();
        savedRegion_x0 = x1; savedRegion_y0 = y1; savedRegion_x1 = x2; savedRegion_y1 = y2;
        int lines = y2 - y1;    
        int pixels = x2 - x1;
        int i;
        if (pixels > 0)
            for(i = 0; i < lines; i++)
                memcpy(mySavedRegion + (i+y1)*osd->width*osd->bpp + x1*osd->bpp, osd->buffer + (i+y1)*osd->width*osd->bpp + x1*osd->bpp, pixels*osd->bpp);
    }

    //--------------------------------------------------------------------------------------------------------------

    bool HdFbTrueColorOsd::LoadImage(UInt imageId)
    {
        //printf("LoadImage() ID: %i\n", imageId);
        if (cachedImages_[imageId]) {
	    if(cachedImages_[imageId]->data && !imageDirty_[imageId])
                return false; /* Already cached. */
	    if(cachedImages_[imageId]->data) free(cachedImages_[imageId]->data);
	    free(cachedImages_[imageId]);
	    cachedImages_[imageId]=0;
        }
        /* Not cached, load it. */
        std::string const &path_ = imagePaths_[imageId];
        if (path_.empty()) {
            printf("ERROR: Image id %u: No path set.\n", imageId);
            return false;
        }
        const char *path = path_.c_str();

        unsigned char **rows;
        int  width, height;

        if (OpenPngFile(path, rows, width, height)) {
            void *buffer = malloc(width * height * sizeof(u_int));
            CachedImage *img = (CachedImage*)malloc(sizeof(CachedImage));
            img->data = (unsigned int*)buffer;
            img->height = height;
            img->width = width;
    
            cachedImages_[imageId] = img;
            unsigned char *p = static_cast<unsigned char *>(buffer);
    
            for (int y = 0; y < height; ++y) {
                unsigned char *r = rows[y];
                for (int x = 0; x < width; ++x) {
                    p[0] = r[2];
                    p[1] = r[1];
                    p[2] = r[0];
                    p[3] = r[3];
                    r += 4;
                    p += 4;
                }
                free(rows[y]);
            }
            free(rows);
    
            ClosePngFile();
            //free(buffer);

            if (imageId == 119) {  /* thumbnails */
                lastThumbWidth[0] = width;
                lastThumbHeight[0] = height;
            } else if (imageId == 120) {
                lastThumbWidth[1] = width;
                lastThumbHeight[1] = height;
            }
            imageDirty_[imageId] = false;
        } else
            printf("ERROR: failed opening %s\n", path);

        return true;
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ eOsdError HdFbTrueColorOsd::SetAreas(tArea const *areas, int numAreas)
    {
        DEBUG_RB_OSD_AR("called (and forward to cOsd) with numAreas=%i\n", numAreas);
        DEBUG_DISPLAY_Areas()
        eOsdError ret = cOsd::SetAreas(areas, numAreas);

#if 0
        eOsdError ret = CanHandleAreas(areas, numAreas);
        if (ret == oeOk)
        {
            int l = areas->x1;
            int t = areas->y1;
            int r = areas->x2 + 1;
            int b = areas->y2 + 1;

            l = std::max(0, l);
            t = std::max(0, l);
            width = r - l;
            height = b - t;
            width = std::max(1, width);
            height = std::max(1, height);
        }
#endif
        return ret;
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ eOsdError HdFbTrueColorOsd::SetPalette(const cPalette &palette, int area)
    {
        dsyslog_rb("TrueColor OSD does not need to set palette for area=%d\n", area);
        return oeOk;
    }

    //--------------------------------------------------------------------------------------------------------------

    void HdFbTrueColorOsd::SetImagePath(UInt imageId, char const *path)
    {

        if (ImageIdInRange(imageId) && strcmp(imagePaths_[imageId].c_str(), path)) {
            imagePaths_[imageId] = path;
            imageDirty_[imageId] = true;
        }

    }

    //--------------------------------------------------------------------------------------------------------------
}

// vim: ts=4 sw=4 et
