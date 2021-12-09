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

// HdTrueColorOsd.c

#include "HdOsdProviderSupport.h"
#include "HdTrueColorOsd.h"
#include "logging.h"

#include <vdr/tools.h>

#include <cstring>
#include <algorithm>

#include <png.h>


namespace Reel
{
    //--------------------------------------------------------------------------------------------------------------
    static cBitmap *cacheBitmap;

    int          HdTrueColorOsd::fontGeneration_ = 0;
    cFont const *HdTrueColorOsd::cachedFonts_[HDOSD_MAX_CACHED_FONTS];

    int volatile *HdTrueColorOsd::hdCachedFonts_;

    int volatile  *HdTrueColorOsd::cachedImages_;
    std::string    HdTrueColorOsd::imagePaths_[HdTrueColorOsd::MaxImageId + 1];
    bool           HdTrueColorOsd::imageDirty_[HdTrueColorOsd::MaxImageId + 1];

    static png_structp png_ptr;
    static png_infop   info_ptr;
    int lastThumbWidth[2];
    int lastThumbHeight[2];

    //--------------------------------------------------------------------------------------------------------------

#if APIVERSNUM >= 10509 || defined(REELVDR)
    HdTrueColorOsd::HdTrueColorOsd(int left, int top, uint level)
    :   cOsd(left, top, level),
#else
    HdTrueColorOsd::HdTrueColorOsd(int left, int top)
    :   cOsd(left, top),
#endif
        /*osdChannel_(Hd::HdCommChannel::Instance().bsc_osd),*/
        dirty_(false)
    {
        hdCachedFonts_ = HdCommChannel::hda->osd_cached_fonts;
        cachedImages_ = HdCommChannel::hda->osd_cached_images;
        
#if 0
        //TB: mark all images as dirty as they are not cached at startup
        for(int i=0; i<=HdTrueColorOsd::MaxImageId; i++)
            imageDirty_[i] = true; 
#endif
        numBitmaps = 0;

        cacheBitmap = new cBitmap(720, 576, 8, 0, 0);

//        for (int i = 0; i < MAXOSDAREAS; i++) bitmaps[i] = new cBitmap(720,576, 32, 0, 0); // TEST to avoid crash in DrawBitmap32

//HdCommChannel::hda->plane[0].mode = 0x41;
//HdCommChannel::hda->plane[0].changed++;

//        if(255 != HdCommChannel::hda->plane[2].alpha) {
//            HdCommChannel::hda->plane[2].alpha = 255;
//            HdCommChannel::hda->plane[2].changed++;................
//        }

    }

    //--------------------------------------------------------------------------------------------------------------

    HdTrueColorOsd::~HdTrueColorOsd()
    {
        DEBUG_RB_OSD("called\n");

#if APIVERSNUM >= 10509 || defined(REELVDR)
        SetActive(false);
        
#else

        hdcmd_osd_clear_t const bco = {HDCMD_OSD_CLEAR, 0, 0, 720, 576};

        SendOsdCmd(bco);
#endif

        // delete cacheBitmap; // FIXED: avoid crash by not deleting
    }
    
    //--------------------------------------------------------------------------------------------------------------
    

#if APIVERSNUM >= 10509 || defined(REELVDR)
    void HdTrueColorOsd::SetActive(bool On)
    {
        DEBUG_RB_OSD_AC("called with On=%i\n", On);
        if (On != Active())
        {
            cOsd::SetActive(On);

            // clear 

            hdcmd_osd_clear_t const bco = {HDCMD_OSD_CLEAR, 0, 0, 720, 576};

            SendOsdCmd(bco);
            // if Active draw
            if (On)
            {
                // should flush only if something is already drawn
                dirty_ =  true;
                Flush();
            }
        }// if

    }
#endif
    //--------------------------------------------------------------------------------------------------------------

    int HdTrueColorOsd::CacheFont(cFont const &font)
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
            esyslog_rb("HdTrueColorOsd font height > 32\n");
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
/*
        int bufferSize = 0;

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
        hd_packet_es_data_t packet;

        //printf("DDDD: size: %i\n",  sizeof(hdcmd_osd_cache_font) + payloadSize);
	HdCommChannel::chOsd.SendPacket(HD_PACKET_OSD, packet, (Reel::Byte*)buffer, sizeof(hdcmd_osd_cache_font) + payloadSize);
//        ::hd_channel_write_finish(osdChannel_, bufferSize);
        free(buffer);
        return ind;
        return 0;
    }

    //--------------------------------------------------------------------------------------------------------------

    void HdTrueColorOsd::CacheImage(UInt imageId)
    {
        DEBUG_RB_OSD("HdTrueColorOsd: CacheImage()\n");

        if (cachedImages_[imageId] && !imageDirty_[imageId])
        {
            // Already cached.
            return;
        }

        // Not cached, load it.
        std::string const &path = imagePaths_[imageId];

        if (path.empty())
        {
            esyslog_rb("HdTrueColorOsd Image id %u: No path set.\n", imageId);
            return;
        }

        if (SendImage(imageId, path.c_str()))
        {
            // Wait a limited time for the HD to receive the image.
            // (The HD may uncache other cached images in order to free memory,
            //  If we don't wait we may subsequently draw one of those, without uploading it before)
#if 0 
            for (int n = 0; n < 400 && !cachedImages_[imageId]; ++n)
            {
                ::usleep(5000);
            }
#endif
        }
        else
        {
            esyslog_rb("HdTrueColorOsd Image id %u: Unable to load image '%s'.\n", imageId, path.c_str());
        }
        imageDirty_[imageId] = false;


    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ eOsdError HdTrueColorOsd::CanHandleAreas(tArea const *areas, int numAreas)
    {
        SUPPORT_CanHandleAreas(areas[i].bpp != 1 && areas[i].bpp != 2 && areas[i].bpp != 4 && areas[i].bpp != 8 && areas[i].bpp != 32 || !RBSetup.TRCLosd, 720, 576)

#if 0
        DEBUG_RB_OSD_AR("called with numAreas=%i\n", numAreas);
        for (int i = 0; i < numAreas; i++)
        {
            DEBUG_RB_OSD_AR("area i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height());
        }

        eOsdError Result = cOsd::CanHandleAreas(areas, numAreas);
        if (Result == oeOk)
        {
            for (int i = 0; i < numAreas; i++)
            {
                DEBUG_RB_OSD_AR("check: area=%i bpp=%d x1=%d x2=%d y1=%d y2=%d Width=%d Height=%d\n", i, areas[i].bpp, areas[i].x1, areas[i].x2, areas[i].y1, areas[i].y2, areas[i].Width(), areas[i].Height());
                if (areas[i].bpp != 1 && areas[i].bpp != 2 && areas[i].bpp != 4 && areas[i].bpp != 8
                    && (areas[i].bpp != 32 || !RBSetup.TRCLosd))
                {
                    esyslog_rb("area color depth not supported: i=%d bpp=%d x1=%d x2=%d y1=%d y2=%d W=%d H=%d\n", i, areas[i].bpp);
                    Result = oeBppNotSupported;
                    break;
                };
                if (areas[i].Width() < 1 || areas[i].Height() < 1 || areas[i].Width() > 720 || areas[i].Height() > 576)
                {
                    esyslog_rb("area size not supported: i=%d w=%d h=%d\n", i, areas[i].Width(), areas[i].Height());
                    Result = oeWrongAreaSize;
                    break;
                };
            }
        }
        else
        {
            esyslog_rb("cOsd::CanHandleAreas already returned Result != oeOk: %d\n", Result);
        }
        DEBUG_RB_OSD_AR("Result=%d (%s)\n", Result, OsdErrorTexts[Result]);
#endif
        return Result;

/*
        if (numAreas != 1)
        {
            esyslog("ERROR: HdTrueColorOsd::CanHandleAreas numAreas = %d\n", numAreas);
            return oeTooManyAreas;
        }

        if (areas[0].bpp != 32)
        {
            esyslog("ERROR: HdTrueColorOsd::CanHandleAreas bpp = %d\n", areas[0].bpp);
            return oeBppNotSupported;
        }

        return oeOk;
*/
    }
    
    //--------------------------------------------------------------------------------------------------------------

    void HdTrueColorOsd::ClearFontCache()
    {
        for (int n = 0; n < HDOSD_MAX_CACHED_FONTS; ++n)
        {
            cachedFonts_[n] = 0;
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    void HdTrueColorOsd::ClosePngFile()
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawBitmap(int x,
                                                    int y,
                                                    const cBitmap &bitmap,
                                                    tColor colorFg,
                                                    tColor colorBg,
                                                    bool replacePalette,
                                                    bool blend)
    {
        DEBUG_RB_OSD_BM("called with x=%d y=%d w=%d h=%d colorFg=0x%08x colorBg=0x%08x replacePalette=%d blend=%d\n", x, y, bitmap.Width(), bitmap.Height(), colorFg, colorBg, replacePalette, blend);

        REEL_ASSERT(!overlay);

        // Send the palette.
        int numColors;
        tColor const *colors = bitmap.Colors(numColors);

        UInt const payloadSize = numColors * sizeof(UInt);

        // int bufferSize = 0;
        void *buffer = malloc(sizeof(hdcmd_osd_palette_t) + payloadSize);
/*        while (!bufferSize)
        {
            bufferSize = ::hd_channel_write_start(osdChannel_,
                                                   &buffer,
                                                   sizeof(hdcmd_osd_palette_t) + payloadSize,
                                                   1000);
        }*/

        hdcmd_osd_palette_t *bco = (hdcmd_osd_palette_t *)(buffer);
        bco->cmd   = HDCMD_OSD_PALETTE;
        bco->count = numColors;

        for (int n = 0; n < numColors; ++n)
        {
            bco->palette[n] = colors[n];
            if(colorFg || colorBg)
            {
                if (n == 0)
                    bco->palette[n] = colorBg;
                if (n == 1)
                    bco->palette[n] = colorFg;
            }
        }

//        ::hd_channel_write_finish(osdChannel_, bufferSize);

        // Send the palette indexes.
        SendOsdCmd(bco, sizeof(hdcmd_osd_palette_t) + payloadSize);

//        hdcmd_osd_draw8_t bco2 = {blend ? HDCMD_OSD_DRAW8_OVERLAY : HDCMD_OSD_DRAW8, Left() + x, Top() + y, bitmap.Width(), bitmap.Height()};
//        SendOsdCmd(&bco2, sizeof(hdcmd_osd_draw8_t), bitmap.Data(0, 0), bitmap.Width() * bitmap.Height());

        hdcmd_osd_draw8_t bco2;

        UInt bcosize = sizeof(hdcmd_osd_draw8_t);
        // UInt payloadsize = bitmap.Width() * bitmap.Height(); // FIXED: unused variable 'payloadsize'
        UInt maxpayloadsize = HD_MAX_DGRAM_SIZE - bcosize;
        int maxheight = maxpayloadsize / bitmap.Width(); // FIXED: narrowing conversion
        if (maxheight > bitmap.Height())
            maxheight = bitmap.Height();

        int height = 0; // FIXED: narrowing conversion

        while (maxheight)
        {
            bco2 = {blend ? HDCMD_OSD_DRAW8_OVERLAY : HDCMD_OSD_DRAW8, Left() + x, Top() + y + height, bitmap.Width(), maxheight};
            SendOsdCmd(&bco2, sizeof(hdcmd_osd_draw8_t), bitmap.Data(0, height), bitmap.Width() * maxheight);
//esyslog("bitmap %x %x %x %x\n",bitmap.Data(0, 0),bitmap.Data(0, 1),bitmap.Data(0, 2),bitmap.Data(0,3));
            if (bitmap.Height() == maxheight )
                break;

            height +=maxheight;
            if (bitmap.Height() < height + maxheight)
                maxheight = bitmap.Height() - height;
        }
        free(buffer);
        dirty_ = true;

    }

    /* override */ void HdTrueColorOsd::DrawBitmap32(int x,
                                                    int y,
                                                    const cBitmap &bitmap,
                                                    tColor colorFg,
                                                    tColor colorBg,
                                                    bool replacePalette,
                                                    bool blend, int width, int height)
    {
	    DEBUG_RB_OSD("called with x=%d y=%d w=%d h=%d\n", x, y, width, height);
        esyslog_rb("HdTrueColorOsd::DrawBitmap32 not supported\n");
        return;

#if 0

        // Send the palette.
        int numColors;
        tColor const *colors = bitmap.Colors(numColors);

        UInt const payloadSize = numColors * sizeof(UInt);

        // int bufferSize = 0;
        void *buffer = malloc(sizeof(hdcmd_osd_palette_t) + payloadSize);
/*        while (!bufferSize)
        {
            bufferSize = ::hd_channel_write_start(osdChannel_,
                                                   &buffer,
                                                   sizeof(hdcmd_osd_palette_t) + payloadSize,
                                                   1000);
        }*/

        hdcmd_osd_palette_t *bco = (hdcmd_osd_palette_t *)(buffer);
        bco->cmd   = HDCMD_OSD_PALETTE;
        bco->count = numColors;

        for (int n = 0; n < numColors; ++n)
        {
            bco->palette[n] = colors[n];
        if (colorFg || colorBg)
        {
            if (n == 0)
                bco->palette[n] = colorBg;
            else if (n == 1)
                bco->palette[n] = colorFg;
        }

        }

//        ::hd_channel_write_finish(osdChannel_, bufferSize);

        // Send the palette indexes.
        SendOsdCmd(bco, sizeof(hdcmd_osd_palette_t) + payloadSize);

        hdcmd_osd_draw8_t bco2 = {HDCMD_OSD_DRAW8_OVERLAY, Left() + x, Top() + y, width, height, blend};

        SendOsdCmd(&bco2, sizeof(hdcmd_osd_draw8_t), bitmap.Data(0, 0), width * height);
        free(buffer);
        dirty_ = true;
#endif
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::HdTrueColorOsd::DrawBitmapHor(int x, int y, int w, const cBitmap &bitmap)
    {
        esyslog_rb("HdTrueColorOsd::DrawBitmapHor not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawBitmapVert(int x, int y, int h, const cBitmap &bitmap)
    {
        esyslog_rb("HdTrueColorOsd::DrawBitmapVert not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawEllipse(int x1, int y1, int x2, int y2, tColor color, int quadrants)
    {
        DEBUG_RB_OSD("HdTrueColorOsd: DrawEllipse x1=%d y1=%d x2=%d y2=%d color=0x%08x quadrants=%d\n", x1, y1, x2, y2, color, quadrants);

        hdcmd_osd_draw_ellipse const bco = {HDCMD_OSD_DRAW_ELLIPSE,
                                             (unsigned int) Left() + x1,
                                             (unsigned int) Top() + y1,
                                             (unsigned int) Left() + x2 + 1,
                                             (unsigned int) Top() + y2 + 1,
                                             color,
                                             quadrants};

        SendOsdCmd(bco);

        dirty_ = true;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawHdImage(UInt imageId, int x, int y, bool blend,
                                                   int horRepeat, int vertRepeat)
    {
        DEBUG_RB_OSD("called\n");

        if (ImageIdInRange(imageId))
        {
            CacheImage(imageId);
            hdcmd_osd_draw_image const bco = {HDCMD_OSD_DRAW_IMAGE,
                                               imageId,
                                               Left() + x, Top() + y,
                                               blend,
                                               horRepeat, vertRepeat};

            SendOsdCmd(bco);
            dirty_ = true;
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawCropImage(UInt imageId, int x, int y, int x0, int y0, int x1, int y1, bool blend)
    {
	    DEBUG_RB_OSD("called\n");

        if (ImageIdInRange(imageId))
        {
            CacheImage(imageId);
            hdcmd_osd_draw_crop_image const bco = {HDCMD_OSD_DRAW_CROP_IMAGE,
                                               imageId,
                                               Left() + x, Top() + y,
                                               Left() + x0, Top() + y0,
                                               Left() + x1, Top() + y1,
                                               blend};

            SendOsdCmd(bco);
            dirty_ = true;
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawPixel(int x, int y, tColor color)
    {
        esyslog_rb("HdTrueColorOsd::DrawPixel not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawRectangle(int x1, int y1, int x2, int y2, tColor color)
    {
        DEBUG_RB_OSD_DR("called with x1=%d y1=%d x2=%d y2=%d color=0x%08x\n", x1, y1, x2, y2, color);

        hdcmd_osd_draw_rect const bco = {HDCMD_OSD_DRAW_RECT,
                                          (unsigned int) Left() + x1,
                                          (unsigned int) Top() + y1,
                                          (unsigned int) Left() + x2 + 1,
                                          (unsigned int) Top() + y2 + 1,
                                          color};

        SendOsdCmd(bco);
        dirty_ = true;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawRectangle(int x1, int y1, int x2, int y2, tColor color, int alphaGradH, int alphaGradV, int alphaGradStepH, int alphaGradStepV)
    {
	DEBUG_RB_OSD_DR("called with x1=%d y1=%d x2=%d y2=%d color=0x%08x alphaGradH=%d alphaGradV=%d alphaGradStepH=%d alphaGradStepV=%d\n", x1, y1, x2, y2, color, alphaGradH, alphaGradV, alphaGradStepH, alphaGradStepV);

        hdcmd_osd_draw_rect2 const bco = {HDCMD_OSD_DRAW_RECT2,
                                          (unsigned int) Left() + x1,
                                          (unsigned int) Top() + y1,
                                          (unsigned int) Left() + x2 + 1,
                                          (unsigned int) Top() + y2 + 1,
                                          color,
                                          alphaGradH,
                                          alphaGradV,
                                          alphaGradStepH,
                                          alphaGradStepV};

        SendOsdCmd(bco);
        dirty_ = true;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawSlope(int x1, int y1, int x2, int y2, tColor color, int type)
    {
        esyslog_rb("HdTrueColorOsd::DrawSlope not supported\n");
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::DrawText(int x,
                                                  int y,
                                                  const char *s_in,
                                                  tColor colorFg,
                                                  tColor colorBg,
                                                  const cFont *font,
                                                  int width,
                                                  int height,
                                                  int alignment)
    {
        if (!s_in) return;

        DEBUG_RB_OSD_DT("called with: colorFg=%08x colorBg=%08x x=%i y=%i w=%i h=%i '%s'\n", colorFg, colorBg, x, y, width, height, s_in);

        if (x < 0) {
            esyslog_rb("HdTrueColorOsd::DrawText: PROBLEM(x<0 => x=0) colorFg=%08x colorBg=%08x x=%i y=%i w=%i h=%i '%s'\n", colorFg, colorBg, x, y, width, height, s_in);
            x = 0;
        };
        if (y < 0) {
            esyslog_rb("HdTrueColorOsd::DrawText: PROBLEM(y<0 => y=0) colorFg=%08x colorBg=%08x x=%i y=%i w=%i h=%i '%s'\n", colorFg, colorBg, x, y, width, height, s_in);
            y = 0;
        };

        /* check for empty string */
        unsigned int i;

        for(i=0; i<strlen(s_in); i++){
            if(s_in[i] == ' ')
                continue;
            else
                break;
        }

        if(i == strlen(s_in))
            return;

        if(width==0) {
            width = font->Width(s_in);
        }

        if(height == 0)
            height=font->Height();

        cacheBitmap->SetSize(width, height);

//        if((colorBg >> 24 == 0) || ((colorBg&0x00ffffff) == 0x00000000)){ /* TB: transparent as bgcolor is evil */
//		colorBg = colorFg&0x01ffffff; 
//        }
        if(colorBg != clrTransparent) /* not transparent */
            DrawRectangle(Left()+x, Top()+y, width, height, colorBg); /* clear the background */

        DEBUG_RB_OSD_DT("draw text into bitmap: colorFg=%08x colorBg=%08x w=%i h=%i '%s'\n", colorFg, colorBg, width, height, s_in);
        cacheBitmap->DrawText(0, 0, s_in, colorFg, colorBg /*clrTransparent*/, font, width, height, alignment);

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
        DrawBitmap(x, y, *cacheBitmap, colorFg, colorBg/*clrTransparent*/, false, false);
//        DrawBitmap32(x, y, *cacheBitmap, colorFg, /*colorBg*/ clrTransparent, false, false, width, height);
    }
   
//    /* override */ void HdTrueColorOsd::DrawText32(int x,
//                                                  int y,
//                                                  const char *s_in,
//                                                  tColor colorFg,
//                                                  tColor colorBg,
//                                                  const cFont *font,
//                                                  int width,
//                                                  int height,
//                                                  int alignment)
//    {

//      if (s_in)
//      {
        /* check for empty string */
//        unsigned int i;
//	for(i=0; i<strlen(s_in); i++){
//		if(s_in[i] == ' ')
//			continue;
//		else
//			break;
//	}
//	if(i == strlen(s_in))
//		return;
//        if(width==0) {
//		width = font->Width(s_in);
//	}
//        if(height == 0)
//	     height=font->Height();
//
//        cacheBitmap->SetSize(width, height);

//        if((colorBg >> 24 == 0) || ((colorBg&0x00ffffff) == 0x00000000)){ /* TB: transparent as bgcolor is evil */
//		colorBg = colorFg&0x01ffffff; 
//        }

//        cacheBitmap->DrawText(0, 0, s_in, colorFg, colorBg /*clrTransparent*/, font, width, height, alignment);
//        DrawBitmap32(x, y, *cacheBitmap, colorFg, colorBg/*clrTransparent*/, false, false, width, height);
//        DrawBitmap(x, y, *cacheBitmap, colorFg, /*colorBg*/ clrTransparent, false, false);
        //printf("DrawText: %s colorFg: %#08x colorBg: %#08x x: %i y: %i w: %i h: %i\n", s, colorFg, colorBg, x, y, width, height);
//      }
//    }
//

    //--------------------------------------------------------------------------------------------------------------

    cPixmap *HdTrueColorOsd::CreatePixmap(int Layer, const cRect &ViewPort, const cRect &DrawPort) {
        static int flag_display_warning = 0;
        if (flag_display_warning == 0)
            esyslog_rb("Pixmap support currently not implemented in TrueColor OSD without framebuffer on HDE\n");
        flag_display_warning = 1;
        return NULL;
    };


    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::Flush()
    {
        DEBUG_RB_OSD_FL("called\n");

#if APIVERSNUM >= 10509 || defined(REELVDR)
        if (! Active()) return ;
#endif
#if 0
        int numColors = 256;
        // tColor const *colors = bitmap.Colors(numColors);

        UInt const payloadSize = numColors * sizeof(UInt);

        // int bufferSize = 0;
        void *buffer = malloc(sizeof(hdcmd_osd_palette_t) + payloadSize);

        hdcmd_osd_palette_t *bco = (hdcmd_osd_palette_t *)(buffer);
        bco->cmd   = HDCMD_OSD_PALETTE;
        bco->count = numColors;

        for (int n = 0; n < numColors; ++n)
        {
            bco->palette[n] = n<<24|n<<16|n<<8|n;
            // esyslog("palette %x\n",bco->palette[n]);
            // if (n == 0)
            //    bco->palette[n] = clrBlack;
            // else if (n == 1)
            //    bco->palette[n] = clrBlack;
        }

        // ::hd_channel_write_finish(osdChannel_, bufferSize);

        // Send the palette indexes.
        SendOsdCmd(bco, sizeof(hdcmd_osd_palette_t) + payloadSize);

        cPixmapMemory *pm;
        // cRect *rect; // FIXED: unused variable 'rect'
        LOCK_PIXMAPS;
        while (pm = (dynamic_cast < cPixmapMemory * >(RenderPixmaps()))) {
        int x;
        int y;
        int w;
        int h;

        x = Left() + pm->ViewPort().X();
        y = Top() + pm->ViewPort().Y();
        w = pm->ViewPort().Width();
        h = pm->ViewPort().Height();
        int d = w * sizeof(tColor);
        //OsdDrawARGB(x, y, w, h, pm->Data());
        DEBUG_RB_OSD("x %d y %d w %d h %d\n",x,y,w,h);
        hdcmd_osd_draw8_t bco2;

        UInt bcosize = sizeof(hdcmd_osd_draw8_t);
        // UInt payloadsize = w * h *sizeof(tColor); // FIXED: unused variable 'payloadsize'
        UInt maxpayloadsize = HD_MAX_DGRAM_SIZE - bcosize;
        int maxheight = maxpayloadsize / w / sizeof(tColor); // FIXED: narrowing conversion
        if (maxheight > h)
            maxheight = h;

        int height = 0; // FIXED: narrowing conversion

        while (maxheight)
        {
            bco2 = {/*HDCMD_OSD_DRAW8_OVERLAY :*/ HDCMD_OSD_DRAW8, x, y+height ,w, maxheight};


            SendOsdCmd(&bco2, sizeof(hdcmd_osd_draw8_t), pm->Data()+height*d, d * maxheight);
            // SendOsdCmd(&bco2, sizeof(hdcmd_osd_draw8_t), (uint32_t *)bitmapData, w * maxheight * 4);

            if (h == maxheight )
                break;

            height +=maxheight;
            if (h < height + maxheight)
                maxheight = h - height;
        }

        // hdcmd_osd_draw8_t bco2 = {HDCMD_OSD_DRAW8_OVERLAY, x, y, w, h, false};
        // SendOsdCmd(&bco2, sizeof(hdcmd_osd_draw8_t), pm->Data(), w * h);

        DestroyPixmap(pm);
#endif

        dirty_ = true;
// }

        if (dirty_)
        {
            static int flushCount = 1;

            //DrawBitmap32(/*old_x, old_y*/ 0,0 /*bitmaps[0]->X0(), bitmaps[0]->Y0()*/, *bitmaps[0], old_colorFg, old_colorBg, false, false);
            // not working HD_MAX_DGRAM_SIZE 414744 > 16384 DrawBitmap32(0, 0, *bitmaps[0], 0, 0, false, false, 720, 576); // TODO replace hardcoded w/h

            hdcmd_osd_flush const bco = {HDCMD_OSD_FLUSH, flushCount};

            SendOsdCmd(bco);
            dirty_ = false;

#if 1
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

    /* override */ cBitmap *HdTrueColorOsd::GetBitmap(int area)
    {
#if 1
        return bitmaps[area];
#endif
        esyslog_rb("HdTrueColorOsd::GetBitmap not supported\n");
        return 0;
    }
    
    //--------------------------------------------------------------------------------------------------------------

    bool HdTrueColorOsd::ImageIdInRange(UInt imageId)
    {

        if (imageId > MaxImageId)
        {
            esyslog_rb("HdTrueColorOsd Image id %u: Out of range.\n", imageId);
            return false;
        }
        else

        {
            return true;
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    bool HdTrueColorOsd::OpenPngFile(char const         *path,
                                      Byte **&rows,
                                      int                &width,
                                      int                &height)
    {

        Byte header[8];

        FILE *fp = fopen(path, "rb");
        if (!fp || fread(header, 1, 8, fp) != 8)
        {
            if(fp)
                fclose(fp);
            return false;
        }

        if (png_sig_cmp(header, 0, 8))
        {
            fclose(fp);
            return false;
        }

        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                     NULL, NULL, NULL);
        if (!png_ptr)
        {
            fclose(fp);
            return false;
        }
    
        info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr)
        {
            png_destroy_read_struct(&png_ptr,
                                    NULL, NULL);
            fclose(fp);
            return false;
        }
    
        png_infop end_info = png_create_info_struct(png_ptr);
        if (!end_info)
        {
            png_destroy_read_struct(&png_ptr, &info_ptr,
                                    (png_infopp)NULL);
            fclose(fp);
            return false;
        }

        png_init_io(png_ptr, fp);

        png_set_sig_bytes(png_ptr, 8);

        if (setjmp(png_jmpbuf(png_ptr)))
        {
            printf("setjmp err\n");
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

    /* override */ void HdTrueColorOsd::RestoreRegion()
    {
        hdcmd_osd_clear_t const bco = {HDCMD_OSD_RESTORE_REGION, 0, 0, 0, 0};
        SendOsdCmd(bco);
    }
    
    //--------------------------------------------------------------------------------------------------------------

    /* override */ void HdTrueColorOsd::SaveRegion(int x1, int y1, int x2, int y2)
    {
        hdcmd_osd_clear_t const bco = {HDCMD_OSD_SAVE_REGION, x1+Left(), y1+Top(), x2+Left(), y2+Top()};
        SendOsdCmd(bco);
    }

    //--------------------------------------------------------------------------------------------------------------

    bool HdTrueColorOsd::SendImage(UInt imageId, char const *path)
    {

        // Only 32-Bit ARGB-PNGs are supported.
        Byte **rows;
        Int  width, height;

        if (OpenPngFile(path, rows, width, height))
        {
            // Send the image to the HD.
            // int bufferSize = 0;

            hdcmd_osd_cache_image bco = {HDCMD_OSD_CACHE_IMAGE,
                                          imageId,
                                          width, height};

            UInt const payloadSize = width * height * sizeof(UInt);

            void *buffer = malloc(sizeof(hdcmd_osd_cache_image) + payloadSize);
	    /*
            while (!bufferSize)
            {
                bufferSize = ::hd_channel_write_start(osdChannel_,
                                                       &buffer,
                                                       sizeof(hdcmd_osd_cache_image) + payloadSize,
                                                       1000);
            }*/
    
            std::memcpy(buffer, &bco, sizeof(hdcmd_osd_cache_image));
    
            Byte *p = static_cast<Byte *>(buffer) + sizeof(hdcmd_osd_cache_image);
    
            for (int y = 0; y < height; ++y)
            {
                Byte *r = rows[y];
                for (int x = 0; x < width; ++x)
                {
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
    
            //::hd_channel_write_finish(osdChannel_, bufferSize);

            hd_packet_es_data_t packet;
            HdCommChannel::chOsd.SendPacket(HD_PACKET_OSD, packet, (Reel::Byte*)buffer, sizeof(hdcmd_osd_cache_image) + payloadSize);

            ClosePngFile();
            free(buffer);

	    if (imageId == 119) {  /* thumbnails */
		lastThumbWidth[0] = width;
		lastThumbHeight[0] = height;
	    } else if (imageId == 120) {
                lastThumbWidth[1] = width;
                lastThumbHeight[1] = height;
            }

            return true;
        }


        return false;
    }

    //--------------------------------------------------------------------------------------------------------------

    void HdTrueColorOsd::SendOsdCmd(void const *bco, UInt bcoSize, void const *payload, UInt payloadSize)
    {
        static char buffer[HD_MAX_DGRAM_SIZE];

        if(bcoSize + payloadSize > HD_MAX_DGRAM_SIZE){
            esyslog_rb("SendOsdCmd exceed HD_MAX_DGRAM_SIZE %d > %d\n", bcoSize + payloadSize, HD_MAX_DGRAM_SIZE);
            return;
        }
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

    /* override */ /* NO-LONGER-REQUIRED eOsdError HdTrueColorOsd::SetAreas(tArea const *areas, int numAreas)
    {

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
            DEBUG_RB_OSD("w=%d h=%d\n", width, height);
        }
        return ret;
    }
    */

    //--------------------------------------------------------------------------------------------------------------

    /* override */ eOsdError HdTrueColorOsd::SetPalette(const cPalette &palette, int area)
    {
        esyslog_rb("HdTrueColorOsd::SetPalette not supported\n");
        return oeOk;
    }

    //--------------------------------------------------------------------------------------------------------------

    void HdTrueColorOsd::SetImagePath(UInt imageId, char const *path)
    {

        if (ImageIdInRange(imageId))
        {
            imagePaths_[imageId] = path;
            imageDirty_[imageId] = true;
        }

    }

    //--------------------------------------------------------------------------------------------------------------
}

// vim: ts=4 sw=4 et
