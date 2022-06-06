/***************************************************************************
 *   Copyright (C) 2005-2008 by Reel Multimedia                            *
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

// VideoPlayerPipHd.c

#include "VideoPlayerPipHd.h"

#include "HdCommChannel.h"

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <vdr/thread.h>
#include <stdlib.h>

extern "C" {
#if defined(REELVDR) && !defined(NEW_FFMPEG)
#include <ffmpeg/swscale.h>
#include <ffmpeg/avcodec.h>
#else
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#endif
}

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,18,102)
#define CODEC_ID_MPEG2VIDEO AV_CODEC_ID_MPEG2VIDEO
#define PIX_FMT_YUV420P AV_PIX_FMT_YUV420P
#define PIX_FMT_RGB32 AV_PIX_FMT_RGB32
#endif

#ifndef PIX_FMT_RGBA32
// #warning "Using new ffmpeg..." // FIXED: disable message
#define PIX_FMT_RGBA32  PIX_FMT_RGB32
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58,7,100)
#define CODEC_FLAG_TRUNCATED AV_CODEC_FLAG_TRUNCATED

#endif

#if LIBSWSCALE_VERSION_MAJOR >= 6
#define SWS_CPU_CAPS_MMX AV_CODEC_FLAG_4MV
#else // >= 6
#if LIBSWSCALE_VERSION_MAJOR >= 3
#define SWS_CPU_CAPS_MMX AV_CPU_FLAG_MMX
#endif // >= 3
#endif // >= 6

// I like standards...
typedef unsigned char uchar;

#define PICS_BUF 16
//#define HDFB_DEVICE "/dev/fb0"
#define ES_BUFFER_SIZE (262144)
#define FB_MMAP_SIZE (2*1024*1024)

// Avoid OSD interference
#define FBPIP_USAGE 0x100

#define BORDER 2

namespace Reel
{
	class SWDecoder: public ::cThread {
	public:
		SWDecoder(uint x=DEFAULT_X, uint y=DEFAULT_Y, uint w=DEFAULT_WIDTH, uint h=DEFAULT_HEIGHT);
		~SWDecoder();
		void SetPosition(uint x=DEFAULT_X, uint y=DEFAULT_Y, uint w=DEFAULT_WIDTH, uint h=DEFAULT_HEIGHT);
		int StoreData(unsigned char* data, int len, uint pts);
		void Clear(void);

	private:

		void EmptyFrame(void);
		void ClearBox(uint x, uint y, uint w, uint h);
		void ClearFrame(uint x1, uint y1, uint x2, uint y2);
		int Show();
		int Decode();
		int Convert();
		void MoveWindow(void);
		virtual void Action(void);  // derived from cThread

		// State
		int enabled;
		uint xpos,ypos;
		uint nxpos,nypos;
		uint oxpos,oypos;
		uint width,height, height_ar;  // ar: height after aspect ratio correction

		// ES-Assembly
		uchar * esbuf;
		int esdec, eslen;    // esdec: read position, eslen: write position

		// Decoder
		AVCodec *av_codec;
		AVCodecContext *av_context;
		AVFrame *decoded_frame;
		int dec_width, dec_height; // FIXED: comparison of integer expressions of different signedness
		uint dec_num, dec_den;

		AVFrame *rgb_frame[PICS_BUF];
		uchar *rgb_buffer[PICS_BUF];
		LLong start_stc;
		struct SwsContext *img_convert_ctx;
		int show_enabled;

		int fdfb;
		uint *framebuffer;
		uint fbwidth;
		uint qin,qout;
		int show_enable;

		// Special effects
		int alpha;
		int alphadir;
		int is_moving;
	};

    //--------------------------------------------------------------------------------------------------------------

	SWDecoder::SWDecoder(uint x, uint y, uint w, uint h)
	{
		extern const char *fbdev;

		printf("SWDecoder construct\n");

		enabled = 0;
		av_codec = NULL;
		av_context = NULL;
		framebuffer = (uint*)-1;
		fdfb = -1;

		for(int n=0;n<PICS_BUF;n++) {
			rgb_frame[n] = NULL;
			rgb_buffer[n] = NULL;
		}

		av_codec = avcodec_find_decoder(CODEC_ID_MPEG2VIDEO);
		if (!av_codec)
		{
			    printf("codec not found\n");
			    return;
		}

#if LIBAVCODEC_VERSION_INT >= ((54<<16)+(51<<8)+100)
		av_context = avcodec_alloc_context3(NULL);
#else
		av_context = avcodec_alloc_context();
#endif
		av_context->flags|=CODEC_FLAG_TRUNCATED;
		av_context->error_concealment=0;

#if LIBAVCODEC_VERSION_INT >= ((54<<16)+(51<<8)+100)
		if (avcodec_open2(av_context, av_codec, NULL) < 0)
#else
		if (avcodec_open(av_context, av_codec) < 0)
#endif
		{
			printf("could not open codec\n");
			return;
		}
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1) // FIXED: 'AVFrame* avcodec_alloc_frame()' is deprecated
		decoded_frame = av_frame_alloc();
#else
		decoded_frame = avcodec_alloc_frame();
#endif

		for(int n=0;n<PICS_BUF;n++) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55,28,1) // FIXED: 'AVFrame* avcodec_alloc_frame()' is deprecated
			rgb_frame[n] = av_frame_alloc();
#else
			rgb_frame[n] = avcodec_alloc_frame();
#endif
			rgb_buffer[n] = (uchar*)malloc(720*576*4);
		}
		    
		enabled=1;

		fdfb = open(fbdev, O_RDWR);
		if (fdfb == -1)
			fdfb = open(FB_DEFAULT_DEVICE, O_RDWR);

		framebuffer=(uint*)-1;
		if (fdfb>=0) {
			framebuffer=(uint*)mmap(0, FB_MMAP_SIZE, PROT_WRITE, MAP_SHARED, fdfb, 0);
			printf("############ Framebuffer: %p\n",framebuffer);
		}

		fbwidth=720; // FIXME, get from fbinfo

		esbuf=(uchar*)malloc(ES_BUFFER_SIZE);
		img_convert_ctx=NULL;

		eslen=0;
		esdec=0;

		qin=qout=0;

		xpos=nxpos=oxpos=x;
		ypos=nypos=oypos=y;
		width=w;
		height=height_ar=h;
		show_enabled=0;
		start_stc=0;

		alpha=0;
		alphadir=50;
		is_moving=0;
		dec_width=dec_height=0;
		dec_num=dec_den=1;
		Start();		
	}
	//----------------------------------------------------------------------------------------------------------
	// Cleanup
	SWDecoder::~SWDecoder(void)
	{
		alphadir=-50;
		usleep(250*1000);
		Cancel(2);
		if (img_convert_ctx)
			sws_freeContext(img_convert_ctx);
		if (framebuffer!=(uint*)-1)
			munmap(framebuffer, FB_MMAP_SIZE);
		if (fdfb>=0)
			close(fdfb);
		for(int n=0;n<PICS_BUF;n++) {
			if (rgb_frame[n]) 
				av_free(rgb_frame[n]);
			if (rgb_buffer[n])
				free(rgb_buffer[n]);
		}
		if (decoded_frame)
			av_free(decoded_frame);
		if (av_context) {
			avcodec_close(av_context);
			av_free(av_context);
		}
		printf("SWDecoder deleted\n");
	}
	//----------------------------------------------------------------------------------------------------------
	void SWDecoder::Clear(void)
	{
		Cancel(2);
		show_enabled=0;
		qin=0;
		qout=0;
	}
	//----------------------------------------------------------------------------------------------------------
	void SWDecoder::SetPosition(uint x, uint y, uint w, uint h)
	{
		nxpos=x;
		nypos=y;
//		width=w;
//		height=h;
	}
	//----------------------------------------------------------------------------------------------------------
	void SWDecoder::MoveWindow(void)
	{
		int speed=2;
		is_moving=0;
		if (xpos!=nxpos) {
			oxpos=xpos;
			is_moving=1;

			if (xpos<nxpos)
				xpos+=1+(nxpos-xpos)/speed;
			else
				xpos-=1+(xpos-nxpos)/speed;
		}

		if (ypos!=nypos) {
			oypos=ypos;
			is_moving=1;

			if (ypos<nypos)
				ypos+=1+(nypos-ypos)/speed;
			else
				ypos-=1+(ypos-nypos)/speed;
		}

		if (is_moving)
			HdCommChannel::hda->osd_dont_touch|=FBPIP_USAGE;
		else
			HdCommChannel::hda->osd_dont_touch&=~FBPIP_USAGE;
				
	}
	//----------------------------------------------------------------------------------------------------------
	void SWDecoder::ClearBox(uint x, uint y, uint w, uint h)
	{
		uint n;
		int *z;
		for(n=0;n<h;n++) {
			z=(int*)(framebuffer+fbwidth*(n+y)+x);
			memset(z,0x0,w*4);
		}
	}
	//----------------------------------------------------------------------------------------------------------
	// x1/y1: old, x2,y2: new
	// Not really optimized, just good enough to avoid flickering
	void SWDecoder::ClearFrame(uint x1, uint y1, uint x2, uint y2)
	{
		uint w=width,h=height_ar;
		w+=2+4*BORDER;		h+=2+4*BORDER;
		x1-=2*BORDER;		y1-=2*BORDER;
		x2-=2*BORDER;		y2-=2*BORDER;

		// upper/lower bar
		if (y1<y2)
			ClearBox(x1,y1,w,y2-y1+2*BORDER);
		else if (y1>y2)
			ClearBox(x1,y2+height_ar,w,y1-y2+2*BORDER+2);
		
		// left/right bar
		if (x1<x2)
			ClearBox(x1,y2,x2-x1+2*BORDER,h);
		else if (x1>x2)
			ClearBox(x2+width,y1,x1-x2+2*BORDER+2,h);
	}
	//----------------------------------------------------------------------------------------------------------
	void SWDecoder::EmptyFrame(void)
	{
		uint n,m;
		int *z;
		int aval=60+alpha/4;
		int bval=(aval<<24)|(aval<<16)|(aval<<8)|aval;
		
		for(n=0;n<BORDER;n++) {
			z=(int*)(framebuffer+fbwidth*(n+ypos-BORDER)+xpos-BORDER);
			memset(z,bval,(width+BORDER*2)*4);
			z=(int*)(framebuffer+fbwidth*(n+ypos+height_ar)+xpos-BORDER);
			memset(z,bval,(width+BORDER*2)*4);
		}

		for(n=0;n<height_ar;n++) {
			z=(int*)(framebuffer+fbwidth*(n+ypos)+xpos-BORDER);
			for(m=0;m<BORDER;m++)
				*z++=bval;

			z=(int*)(framebuffer+fbwidth*(n+ypos)+xpos+width);
			for(m=0;m<BORDER;m++)
				*z++=bval;
		}
	}
	//----------------------------------------------------------------------------------------------------------
	int SWDecoder::Show(void)
	{
		uint qo=qout%PICS_BUF;
		struct timeval tv;
		uint* pic;
		LLong now,diff,offs;
		uint n,m;

		if (!show_enabled || (qin%PICS_BUF)==qo || qin==qout || (HdCommChannel::hda->osd_dont_touch&~FBPIP_USAGE) )
			return -1;

		gettimeofday(&tv, 0);
		now=LLong(tv.tv_sec) * 1000000 + tv.tv_usec;
		diff=now-start_stc;

		// Hack: Fixme for other framerates
		if (start_stc && diff<40*1000)
			return -1;

		offs=diff-40*1000;

		start_stc=now;

		if (offs>0 && offs<60*1000)
			start_stc-=offs;

		pic=(uint*)rgb_buffer[qo];
		MoveWindow();
		
		if (is_moving)
			ClearFrame(oxpos,oypos,xpos,ypos);

		EmptyFrame();

// Native swscale unfortunately clears the alpha channel,
// so memcpy works only with libavcodec's sws_scale emulation
		if (alpha==255 && 1==0) {
			for(n=0;n<height_ar;n++) {
				int *z;
				z=(int*)(framebuffer+fbwidth*(n+ypos)+xpos);
				memcpy(z,pic,width*4);
				pic+=width;
			}
		}
		else 
		{
#if 1
			for(n=0;n<height_ar;n++) {
				int *z;
                                int al=(alpha<<24);
				z=(int*)(framebuffer+fbwidth*(n+ypos)+xpos);
				for(m=0;m<width/8;m++) {
					// Still does not look very optimized in disassembly...
					*z++=al|*pic++;
					*z++=al|*pic++;
					*z++=al|*pic++;
					*z++=al|*pic++;
					*z++=al|*pic++;
					*z++=al|*pic++;
					*z++=al|*pic++;
					*z++=al|*pic++;
				}
			}
#endif
		}

		alpha+=alphadir;
		if (alpha>255)
			alpha=255;
		if (alpha<0)
			alpha=0;
		
		qout++;
		return 0;
	}
	//----------------------------------------------------------------------------------------------------------
	// Actual output thread
	void SWDecoder::Action(void) 
	{
		int retry=0;
		printf("PiP Thread start\n");
		usleep(100*1000);
		EmptyFrame();
		while(Running()) {
			if (Show()) {
				retry++;
			}
			else
				retry=0;

			if (retry>50*5) { // Indicate PiP-Area even without frames
				EmptyFrame();
				retry=0;
			}
			usleep(2*1000);
		}
		printf("PiP Thread end\n");
	}
	//----------------------------------------------------------------------------------------------------------
	int SWDecoder::Convert(void)
	{	    
		uint qi=qin%PICS_BUF;
		uint dar1,dar2;  // Aspect of decoded Frame
		uint var1,var2;  // Aspect of video display

		dar1=av_context->sample_aspect_ratio.num;
		dar2=av_context->sample_aspect_ratio.den;

		var1=HdCommChannel::hda->aspect.w;
		var2=HdCommChannel::hda->aspect.h;

		// Adjust height depending on aspect ratio

		if ((!img_convert_ctx || av_context->width!=dec_width || av_context->height!=dec_height ||
		     dar1!=dec_num || dar2!=dec_den) && av_context->width && av_context->height && var2 && dar1) {

			if (img_convert_ctx)
				sws_freeContext(img_convert_ctx);

			// Cleanup old area
			ClearBox(xpos-BORDER,ypos-BORDER,width+2*BORDER,height_ar+2*BORDER);
		       
			height_ar=(height*var1*av_context->height*dar2)/(var2*av_context->width*dar1);

			EmptyFrame();

			img_convert_ctx = sws_getContext(av_context->width, av_context->height,
							 PIX_FMT_YUV420P,
							 width, height_ar, PIX_FMT_RGBA32, SWS_BILINEAR | SWS_CPU_CAPS_MMX,
							 NULL, NULL, NULL);

			dec_width=av_context->width;
			dec_height=av_context->height;
			dec_num=dar1;
			dec_den=dar2;			
		}

		if (!img_convert_ctx) {
//			printf("PiP Error initializing swscale context.\n");
			return -1;
		}
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(51,63,100)
		avpicture_fill((AVPicture *) rgb_frame[qi], rgb_buffer[qi],
			       PIX_FMT_RGBA32, width, height);
#else
		av_image_fill_arrays(rgb_frame[qi]->data, rgb_frame[qi]->linesize, rgb_buffer[qi], PIX_FMT_RGBA32, width, height, 1);
#endif

		// Security check...
		if (decoded_frame->data && decoded_frame->linesize && rgb_frame[qi]->data && rgb_frame[qi]->linesize)
			sws_scale(img_convert_ctx, decoded_frame->data, 
				  decoded_frame->linesize, 0, av_context->height,
				  rgb_frame[qi]->data, rgb_frame[qi]->linesize    );

		if (qi!=((qout-1)%PICS_BUF))
			qin++;
		else {
			// Throw away current frame, throttle burst decoding a bit
			printf("PiP Queue overflow %i %i\n",qin,qout);
			start_stc=0;
			usleep(5*1000);
		}

		if (!show_enabled && qin>8) {
			printf("Enable PiP out\n");
			show_enabled=1;
		}
		return 0;
	}
	//----------------------------------------------------------------------------------------------------------
	int SWDecoder::Decode()
	{
		int len,gotPicture;
#if LIBAVCODEC_VERSION_INT < ((53<<16)+0+0)
		len = avcodec_decode_video(av_context, decoded_frame, &gotPicture, (uint8_t*)esbuf+esdec, eslen-esdec);	    
#else
		AVPacket avpkt; // FIXED: 'avpkt' is used uninitialized in this function
		avpkt.data = (uint8_t*)esbuf+esdec;
		avpkt.size = eslen-esdec;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57,106,102)
		len = avcodec_decode_video2(av_context, decoded_frame, &gotPicture, &avpkt);
#else
		len = avcodec_receive_frame(av_context, decoded_frame);
		if (len == 0)
			gotPicture = 1;
		if (len == AVERROR(EAGAIN))
			len = 0;
		if (len == 0)
			len = avcodec_send_packet(av_context, &avpkt);
#endif
#endif
		if (len>0)
			esdec+=len;

		if (gotPicture)		
			Convert();

		return 0;
	}
	//----------------------------------------------------------------------------------------------------------
	// len is typically about 2K
	int SWDecoder::StoreData(uchar* data, int len, uint pts)
	{
		if (!(HdCommChannel::hda->osd_dont_touch&~FBPIP_USAGE) && enabled && len && esbuf) {
			int count=5;
			
			while(eslen-esdec>20000 && --count)
				Decode();
			
			if (eslen+len < 3*ES_BUFFER_SIZE/4) {
				memcpy(esbuf+eslen,data,len);
				eslen+=len;
			}
			else {
				if (eslen+len > ES_BUFFER_SIZE-8192) {
					printf("PiP ES Buffer Reset\n");
					eslen=esdec=0;
				}
				else {
					memmove(esbuf,esbuf+esdec,eslen-esdec);
					eslen-=esdec;
					esdec=0;

					memcpy(esbuf+eslen,data,len);
					eslen+=len;
				}
			}
		}
		return 0;
	}
	//----------------------------------------------------------------------------------------------------------
    //--------------------------------------------------------------------------------------------------------------

    void VideoPlayerPipHd::Create()
    {
        if (!instance_)
        {
            instance_ = new VideoPlayerPipHd;
#if LIBAVCODEC_VERSION_INT < ((54<<16)+0+0)
	    avcodec_init();
#endif
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58,10,100)
	    // av_register_all() has been deprecated in ffmpeg 4.0 
	    avcodec_register_all();
#endif
        }
    }

    //--------------------------------------------------------------------------------------------------------------

    void VideoPlayerPipHd::Clear()
    {
        ::printf("VideoPlayerPipHd::Clear()\n");
	    if (!shutdown && decoder)
		    decoder->Clear();
    }

    //--------------------------------------------------------------------------------------------------------------
    void VideoPlayerPipHd::PlayPacket(Mpeg::EsPacket const &esPacket, bool still)
    {
	    int elen=esPacket.GetDataLength();
	    uchar* ed=(uchar*)esPacket.GetData();
	    uint epts=esPacket.GetPts();
	    if (!shutdown && decoder)
		    decoder->StoreData(ed,elen,epts);
    }

    //--------------------------------------------------------------------------------------------------------------

    void VideoPlayerPipHd::SetDimensions(uint x, uint y, uint w, uint h)
    {
	    ::printf("VideoPlayerPipHd::SetDimensions()\n");
	    xpos=x;
	    ypos=y;

	    if (!shutdown && decoder)
		    decoder->SetPosition(xpos,ypos,width,height);
    }

    //--------------------------------------------------------------------------------------------------------------

    void VideoPlayerPipHd::Start()
    {
	    ::printf("VideoPlayerPipHd::Start()\n");
	    shutdown=0;	    
	    decoder=new SWDecoder(xpos,ypos,width,height);
    }

    //--------------------------------------------------------------------------------------------------------------

    void VideoPlayerPipHd::Stop()
    {
	    ::printf("VideoPlayerPipHd::Stop()\n");
	    shutdown=1;
	    if (decoder)
		    delete decoder;
	    decoder=NULL;
    }
}
