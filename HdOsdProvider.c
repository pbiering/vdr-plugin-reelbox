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

// HdOsdProvider.c

#include "HdOsdProvider.h"
#include "ReelBoxMenu.h"
#include "HdOsd.h"
#include "HdTrueColorOsd.h"
#include "HdFbTrueColorOsd.h"
#include "logging.h"

namespace Reel
{

    extern int useFb;

#if APIVERSNUM >= 10509 || defined(REELVDR)
    cOsd *HdOsdProvider::CreateOsd(int left, int top, uint level)
    {
        if (RBSetup.TRCLosd == 0){
            DEBUG_RB_OSD_CO("create 8-bit color OSD on HDE (left=%d top=%d)\n", left, top);
            HdOsd *hdOsd = new HdOsd(left, top, level);
#else
    cOsd *HdOsdProvider::CreateOsd(int left, int top)
    {
        if (RBSetup.TRCLosd == 0){
            DEBUG_RB_OSD_CO("create 8-bit color OSD on HDE (left=%d top=%d)\n", left, top);
            HdOsd *hdOsd = new HdOsd(left, top);
#endif
#if 0 
            if (scaleMode_)
            {
                hdOsd->ScaleToVideoPlane();
            }
#endif

            return hdOsd;
        }
        else
#if APIVERSNUM >= 10509 || defined(REELVDR)
//    cOsd *HdOsdProvider::CreateTrueColorOsd(int left, int top, uint level)
        {

            if(useFb)
            {
                DEBUG_RB_OSD_CO("create TrueColor OSD on HDE with framebuffer device (left=%d top=%d)\n", left, top);
                return new HdFbTrueColorOsd(left, top, level);
            }
            else
            {
                DEBUG_RB_OSD_CO("create TrueColor OSD on HDE without framebuffer device (left=%d top=%d)\n", left, top);
                return new HdTrueColorOsd(left, top, level);
            };
        }
#else
//    cOsd *HdOsdProvider::CreateTrueColorOsd(int left, int top)
        {
            if(useFb)
            {
                DEBUG_RB_OSD_CO("create TrueColor OSD on HDE with framebuffer device (left=%d top=%d)\n", left, top);
                return new HdFbTrueColorOsd(left, top);
            }
            else
            {
                DEBUG_RB_OSD_CO("create TrueColor OSD on HDE without framebuffer device (left=%d top=%d)\n", left, top);
                return new HdTrueColorOsd(left, top);
            };
        }
#endif
    }

    HdOsdProvider::~HdOsdProvider() {
        if(0 && useFb) {  //TB: causes crash when shutting down???
            close(osd->fd);
            osd->fd = -1;
            free(osd->buffer);
            free(osd);
            if(mySavedRegion)
                free(mySavedRegion);
            mySavedRegion = NULL;
        }
    }

    bool HdOsdProvider::scaleMode_ = false;


bool HdOsdProvider::ProvidesTrueColor(void)
{
    return RBSetup.TRCLosd;
}

} // end of namespace

// vim: ts=4 sw=4 et
