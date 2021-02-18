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

// ReelboxDevice.c

#include "ReelBoxDevice.h"
#include "ReelBoxMenu.h"

#include "AudioPlayerBsp.h"
#include "VideoPlayerBsp.h"
#include "VideoPlayerPipBsp.h"
#include "AudioPlayerHd.h"
#include "VideoPlayerHd.h"
#include "VideoPlayerPipHd.h"

#include "config.h"

#include <vdr/dvbspu.h>
#include <vdr/channels.h>

#include <alsa/asoundlib.h>
#include <iostream>
#include <vector>
//#include <a52dec/a52.h>
//#include "ac3.h"
#include "BspOsdProvider.h"
#include "HdOsdProvider.h"
#include "fs453settings.h"
#include "logging.h"

#define BP  //
//#define BP

// setting the volume on Lite + AVG
#ifdef RBLITE
       #define SET_VOLUME(volume) SetVolume(volume, "Master")
       #define MUTE(switch)       SetVolume(0, switch)
       #define UNMUTE(switch)     SetVolume(23, switch)
#else
       #define SET_VOLUME(volume) SetVolume(volume, "Master")
       #define MUTE(switch)
       #define UNMUTE(switch)
#endif

#define AC3_HEADER_SIZE 7
#define DTS_HEADER_SIZE 20

namespace Reel
{
#define CHECK_CONCURRENCY ((void)0)

    ReelBoxDevice *ReelBoxDevice::instance_;

    uint32_t Delta = 0;
    uint32_t ptsValues[150];
    int numPtsValues=0;
    uchar ac3buffer[25*1024];
    int ac3bufferlen =0;
    uchar pesbuffer;

    ReelBoxDevice::ReelBoxDevice()
    :   digitalAudio_(false), pipActive_(false), audioChannel_(0),
#if VDRVERSNUM < 10716
        audioPlayback_(0), videoPlayback_(0),
#endif
        playMode_(pmNone),
        bkgPicPlayer_(*this),
        useHDExtension_(RBSetup.usehdext),
        audioOverHDMI_(0),//RBSetup.audio_over_hdmi)!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        audioOverHd_(RBSetup.audio_over_hd),
        needRestart(false),
        normalPlay(false)
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        instance_ = this;

#ifndef HDMI_ONLY
        dsyslog_rb("%s EXEC 'iecset audio on'\n", __PRETTY_FUNCTION__);
	printf("--- output is caused by SystemExec begin ---\n");
        SystemExec("iecset audio on");
	printf("--- output is caused by SystemExec end   ---\n");
        dsyslog_rb("%s EXEC 'iecset audio on' DONE\n", __PRETTY_FUNCTION__);
#endif

        if (useHDExtension_)
        {
            // HD
            dsyslog("use hdextension AudioOverHDMI ? %s \n", audioOverHDMI_?"YES":"NO" );
            VideoPlayerHd::Create();
            VideoPlayerPipHd::Create();
            tmpHDaspect = -1;
#ifdef RBLITE
            if (!audioOverHDMI_ && digitalAudio_ )
#else
            if (!audioOverHd_ && !digitalAudio_)
#endif
            {
                audioPlayerHd_ = 0;
                AudioPlayerBsp::Create(); // audio over bsp
                audioPlayerBsp_ = &AudioPlayer::InstanceBsp();
            }
            else
            {
                audioPlayerBsp_ = 0;
                AudioPlayerHd::Create(); // audio over hd
                audioPlayerHd_ = &AudioPlayer::InstanceHd();
            }
        }
        else
        {
            // BSP
            dsyslog("%s BSP only\n", __PRETTY_FUNCTION__);
            audioOverHDMI_ = false; // for save
            VideoPlayerBsp::Create();
            AudioPlayerBsp::Create();
            VideoPlayerPipBsp::Create();
            audioPlayerBsp_ = &AudioPlayer::InstanceBsp();
            audioPlayerHd_ = 0;
        }
        videoPlayer_ = &VideoPlayer::Instance();

        if(audioPlayerBsp_)
        {
            audioPlayerBsp_->SetAVSyncListener(videoPlayer_);
        }
        else
        {
            videoPlayer_->SetStc(false, 0);
        }

        videoPlayerPip_ = &VideoPlayerPip::Instance();
	    audioBackgroundPics_ = true;

#if VDRVERSNUM >= 10716
        bkgPicPlayer_.Start();
#endif

    }


    ReelBoxDevice::~ReelBoxDevice()
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        instance_ = 0;

#if VDRVERSNUM >= 10716
        bkgPicPlayer_.Stop();
#endif

        if (audioPlayerBsp_)
        {
           audioPlayerBsp_->SetAVSyncListener(0);
        }

        VideoPlayerPip::Destroy();
        AudioPlayerBsp::Destroy();
        AudioPlayerHd::Destroy();
        VideoPlayer::Destroy();
	if(ringBuffer) {
	    ringBuffer->Clear();
    	    delete ringBuffer;
	};
    }

    Int ReelBoxDevice::AudioDelay() const
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        if (audioPlayerHd_)
        {
            return audioPlayerHd_->Delay();
        }
        else if(audioPlayerBsp_)
        {
            return audioPlayerBsp_->Delay();
        }
        return 0;
    }

    void ReelBoxDevice::RestartAudio()
    {
        audioOverHDMI_ = RBSetup.audio_over_hdmi;
        audioOverHd_ = RBSetup.audio_over_hd;
        dsyslog_rb("RestartAudio, audioOverHDMI_ = %d, digitalAudio_ = %d, audioOverHd = %d\n", (int) audioOverHDMI_, (int)digitalAudio_, (int)audioOverHd_ );
        bool switchToBspAudio = false;
        bool switchToHdAudio = false;

#ifdef RBLITE
        if(!audioOverHDMI_ && digitalAudio_ )
#else
        if(!audioOverHd_ && !digitalAudio_)
#endif
        {
            if(audioPlayerHd_)
            {
                switchToBspAudio = true;
            }
        }
#ifdef RBLITE
        else if(useHDExtension_ && audioPlayerBsp_)
#else
        else if(audioOverHd_ || digitalAudio_)
#endif
        {
            if(audioPlayerBsp_)
            {
                switchToHdAudio = true;
            }
        }

        if(switchToHdAudio)
        {
            audioPlayerBsp_->Stop();
            audioPlayerBsp_->SetAVSyncListener(0);
            AudioPlayer::Destroy();
            audioPlayerBsp_ = 0;

            AudioPlayerHd::Create();
            audioPlayerHd_ = &AudioPlayer::InstanceHd();
            audioPlayerHd_->Start();

            videoPlayer_->SetStc(false, 0);  //clean?
        }

        if(switchToBspAudio)
        {
            audioPlayerHd_->Stop();
            AudioPlayer::Destroy();
            audioPlayerHd_ = 0;

            AudioPlayerBsp::Create();
            audioPlayerBsp_ = &AudioPlayer::InstanceBsp();
            audioPlayerBsp_->SetAVSyncListener(videoPlayer_);
            audioPlayerBsp_->Start();
        }
    }

    enum { aspectnone, aspect43, aspect169 };
    int ReelBoxDevice::GetAspectRatio()
    {
        int width=0, height=0;

        if (useHDExtension_)
        {
            width = HdCommChannel::hda->player_status[0].asp_x;
            height = HdCommChannel::hda->player_status[0].asp_y;
        }
        else
        {
            bspd_data_t volatile *bspd = Bsp::BspCommChannel::Instance().bspd;

            width = bspd->video_attributes[0].imgAspectRatioX;
            height = bspd->video_attributes[0].imgAspectRatioY;
        }

        if (width > 0 || height > 0)
            return (width == 16 && height == 9)?aspect169:aspect43;
        else
            return aspectnone;
    }

#if VDRVERSNUM >= 10716
    void ReelBoxDevice::GetVideoSize (int &Width, int &Height, double &Aspect) {
      Width=0;
      Height=0;
      if (useHDExtension_) {
        Width = HdCommChannel::hda->player_status[0].w;
        Height = HdCommChannel::hda->player_status[0].h;
      } else {
        esyslog_rb("ERROR: Not implement yet: ReelBoxDevice::GetVideoSize for bspd\n");
      }
      Aspect = 1.0;
      switch(GetAspectRatio()) {
        case aspect43 : Aspect =  4.0/3.0; break;
        case aspect169: Aspect = 16.0/9.0; break;
        default       : break;
      } 
    }

    void ReelBoxDevice::GetOsdSize(int &Width, int &Height, double &PixelAspect)
    {

        Width = 720;
        Height = 576;//HdCommChannel::hda->video_mode.framerate == 50 ? 576 : 480;

        if (Setup.VideoFormat == 1) // 16:9 Format
            PixelAspect = 16.0 /9.0;
        else
            PixelAspect = 4.0 /3.0;
        PixelAspect /= double(Width) / Height;

    }

#endif

    //void ReelBoxDevice::Stop()
    void ReelBoxDevice::Restart()
    {
        needRestart = false;
        dsyslog("%s\n", __PRETTY_FUNCTION__);
        digitalAudio_ = false; pipActive_ = false; audioChannel_ = 0;
#if VDRVERSNUM < 10716
        audioPlayback_ = 0; videoPlayback_ = 0;
#endif

        int restartVideo = useHDExtension_ != RBSetup.usehdext;
        useHDExtension_ = RBSetup.usehdext;
        audioOverHDMI_ = RBSetup.audio_over_hdmi;

        //bkgPicPlayer_ = *this;
        //Clear();
        SetPlayModeOff();

        if (audioPlayerBsp_)
        {
           audioPlayerBsp_->SetAVSyncListener(0);
        }

        if (restartVideo)
        {
            VideoPlayerPip::Destroy();
            VideoPlayerBsp::Destroy();
            VideoPlayerHd::Destroy();
            videoPlayer_ = 0;
#if VDRVERSNUM < 10716
            videoPlayback_ = 0;
#endif
        }

        AudioPlayer::Destroy();

        audioPlayerHd_ = 0;
        audioPlayerBsp_ = 0;
#if VDRVERSNUM < 10716
        audioPlayback_ = 0;
#endif
        instance_ = 0; //? dangerous ?

        SystemExec("iecset audio on");
        dsyslog_rb("%s RBSetup.usehdext=%d\n", __PRETTY_FUNCTION__,RBSetup.usehdext);
        // }
        //
        //void ReelBoxDevice::Start() {

        if (useHDExtension_)
        {
            dsyslog_rb("USE HD-Extension\n");
            // HD
            //usleep(1000*1000);
            if (restartVideo)
            {
                Bsp::BspCommChannel::Destroy();
                dsyslog_rb("%s :BspCommChannel::Destroy()\n", __PRETTY_FUNCTION__);
                usleep(1000*1000);
                Reel::HdCommChannel::Init();
                dsyslog_rb("%s :HdCommChannel::Init()\n", __PRETTY_FUNCTION__);
                //Reel::HdCommChannel::InitHda();
	            usleep(1000*1000);
            }

            //audioPlayerBsp_ = &AudioPlayer::InstanceBsp();
            if (!audioOverHd_ && !digitalAudio_ )
            {
                dsyslog_rb("%s NO Audio Over HDMI_\n", __PRETTY_FUNCTION__);
                AudioPlayerBsp::Create();
                audioPlayerBsp_ = &AudioPlayer::InstanceBsp();
            }
            else
            {
                dsyslog_rb("%s AudioOverHDMI_\n", __PRETTY_FUNCTION__);
                AudioPlayerHd::Create();
                audioPlayerHd_ = &AudioPlayer::InstanceHd();
            }

            if (restartVideo)
            {
                VideoPlayerHd::Create();
                VideoPlayerPipHd::Create();
            }
        }
        else
        {
            // BSP
            if (restartVideo)
            {
                dsyslog_rb("%s USE BSP\n", __PRETTY_FUNCTION__);
                HdCommChannel::Exit();
                usleep(1000*1000);
                Bsp::BspCommChannel::Create();
                usleep(1000*1000);

                VideoPlayerBsp::Create();
                VideoPlayerPipBsp::Create();
            }

            AudioPlayerBsp::Create();
        }

        instance_ = this;

        if (restartVideo)
        {
            videoPlayer_ = &VideoPlayer::Instance();
        }

        if (audioPlayerBsp_)
        {
            dsyslog_rb("%s audioPlayerBsp_->SetAVSyncListener(videoPlayer_)\n", __PRETTY_FUNCTION__);
            audioPlayerBsp_->SetAVSyncListener(videoPlayer_);
        }

        if (restartVideo)
        {
            videoPlayerPip_ = &VideoPlayerPip::Instance();
            audioBackgroundPics_ = true;
        }
        SetPlayModeOn();

	/* restore default values*/
	if(restartVideo){
	  if(!useHDExtension_){
            RBSetup.brightness = fs453_defaultval_tab0_BSP;
            RBSetup.contrast   = fs453_defaultval_tab1_BSP;
            RBSetup.colour     = fs453_defaultval_tab2_BSP;
            RBSetup.sharpness  = fs453_defaultval_tab3_BSP;
            RBSetup.gamma      = fs453_defaultval_tab4_BSP;
            RBSetup.flicker    = fs453_defaultval_tab5_BSP;
            Reel::Bsp::BspCommChannel::SetPicture();
	  } else {
            RBSetup.brightness = fs453_defaultval_tab0_HD;
            RBSetup.contrast   = fs453_defaultval_tab1_HD;
            RBSetup.colour     = fs453_defaultval_tab2_HD;
            RBSetup.sharpness  = fs453_defaultval_tab3_HD;
            RBSetup.gamma      = fs453_defaultval_tab4_HD;
            RBSetup.flicker    = fs453_defaultval_tab5_HD;
            Reel::HdCommChannel::SetPicture(&RBSetup);
	  }
	}

        dsyslog_rb("%s SUCCESS\n",__PRETTY_FUNCTION__);
    }

    void ReelBoxDevice::AudioUnderflow()
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
    }

    void ReelBoxDevice::Clear()
    {
        CHECK_CONCURRENCY;
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        try
        {
            cDevice::Clear();
	    Reel::HdCommChannel::SetAspect();
            if (audioPlayerHd_) audioPlayerHd_->Clear();
            if (audioPlayerBsp_) audioPlayerBsp_->Clear();
            if (videoPlayer_) videoPlayer_->Clear();
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            // Restart();
        }
    }

    void ReelBoxDevice::Freeze()
    {
        CHECK_CONCURRENCY;
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        try
        {
            cDevice::Freeze();
            videoPlayer_->Freeze();
            if (audioPlayerHd_)  audioPlayerHd_->Freeze();
            if (audioPlayerBsp_) audioPlayerBsp_->Freeze();
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            // Restart();
        }
    }

    cSpuDecoder *ReelBoxDevice::GetSpuDecoder()
    {
        static bool useHDExtension =  useHDExtension_;
        useHDExtension = useHDExtension_; //if value of useHDExtension_ chanches

        if (!spuDecoder_.get())
        {
            try
            {
                class ReelSpuDecoder : public cDvbSpuDecoder
                {
                    /* override */ cSpuDecoder::eScaleMode getScaleMode()
                    {
                        return eSpuNormal;
                    }

                    /* override */ void Draw(void)
                    {
                        if (useHDExtension)
                        {
                            cDvbSpuDecoder::Draw();
                        }
                        else
                        {
                           BspOsdProvider::SetOsdScaleMode(true);
                           cDvbSpuDecoder::Draw();
                           BspOsdProvider::SetOsdScaleMode(false);
                        }
                    }
                };

                spuDecoder_.reset(new ReelSpuDecoder);
            }
            catch (std::exception const &e)
            {
                REEL_LOG_EXCEPTION(e);
                Restart();
            }
        }
        return spuDecoder_.get();
    }

    bool ReelBoxDevice::Flush(Int timeoutMs)
    {
        // This function will be called by the vdr concurrently to the other functions.
        dsyslog_rb("[reelbox] %s \n", __PRETTY_FUNCTION__);
        bool ret = false;
        for (;;)
        {
            if (audioPlayerHd_)
            {
               ret = audioPlayerHd_ && audioPlayerHd_->Flush() && videoPlayer_->Flush();
            }
            if (audioPlayerBsp_)
            {
               ret = audioPlayerBsp_ && audioPlayerBsp_->Flush() && videoPlayer_->Flush();
            }

            if ((ret) || timeoutMs <= 0)
            {
                break;
            }
            ::usleep(5000); // sleep 5 ms
            timeoutMs -= 5;
        }
        return ret;
    }

    void ReelBoxDevice::FlushAudio()
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        if (audioPlayerHd_)
        {
          while (audioPlayerHd_ && !audioPlayerHd_->Flush())
              ; // noop
        }
        if (audioPlayerBsp_)
        {
          while (audioPlayerBsp_ && !audioPlayerBsp_->Flush())
              ; // noop
        }
    }

    int ReelBoxDevice::GetAudioChannelDevice()
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        return audioChannel_;
    }

    void ReelBoxDevice::Mute()
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        cDevice::Mute();
    }

    void ReelBoxDevice::Play()
    {
        CHECK_CONCURRENCY;
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        try
        {
            cDevice::Play();
            normalPlay = true;

            if (audioPlayerHd_)
            {
                audioPlayerHd_->Play();
            }
            if (audioPlayerBsp_)
            {
                audioPlayerBsp_->Play();
            }

            dsyslog_rb("%s --- videoPlayer_->Play(() \n", __PRETTY_FUNCTION__);
            videoPlayer_->Play();
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            Restart();
        }
    }

#if 0
#include <sys/time.h>
#include <time.h>

        ULLong GetTimeUsec()
        {
            struct timeval tv;
            gettimeofday(&tv, 0);
            return ULLong(tv.tv_sec) * 1000000 + tv.tv_usec;
        }
#endif

#if VDRVERSNUM < 10400
    Int ReelBoxDevice::PlayAudio(Byte const *data, Int length)
#else
    Int ReelBoxDevice::PlayAudio(Byte const *data, Int length, uchar id)
#endif
    {;
        CHECK_CONCURRENCY;

#if VDRVERSNUM < 10704
        const tTrackId *trackId = GetTrack(GetCurrentAudioTrack());
        
        if (!trackId || trackId->id != id)
        {
            return length;
        }
#endif

#if VDRVERSNUM < 10716
        audioPlayback_ = 200;

        if (-- videoPlayback_ < 0)
        {
            videoPlayback_ = 0;
#if 1
            bkgPicPlayer_.Start();
#else
            if (audioBackgroundPics_)
                bkgPicPlayer_.Start();
            else
                bkgPicPlayer_.Stop();
#endif
        }
#else
        bkgPicPlayer_.PlayedAudio();
#endif
/*
if(data[3] == 0xbd){
	const uchar *ac3stream = data + PesPayloadOffset(data);
//	int frmsizecod = ac3stream[4] & 0x3f;
	int bsid       = ac3stream[5];
	if ((bsid >>3) > 10){ //eac3
//esyslog("%x %x %x %x pes %d ac3 %d\n",ac3stream[0],ac3stream[1],ac3stream[2],ac3stream[3],PesLength(data),((((ac3stream[2] & 0x7) << 8) + ac3stream[3]) + 1) * 2);

const uchar *Data=data;
int len=length;
soft_decode((uchar*)data,length,(uchar*)Data,len,AV_CODEC_ID_EAC3);

	}
}
*/
	if (useHDExtension_) {

//if(length>20){
//int f = 0,off=0;;
//esyslog(" %d %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\n",length,data[0+off+f],data[1+off+f],data[2+off+f],data[3+off+f],data[4+off+f],data[5+off+f],data[6+off+f],data[7+off+f],data[8+off+f],data[9+off+f],data[10+off+f],data[11+off+f],data[12+off+f],data[13+off+f],data[14+off+f],data[15+off+f],data[16+off+f],data[17+off+f],data[18+off+f],data[19+off+f]);
//}

                if(!audioPlayerBsp_) //send packets to alsa too
                {
		    videoPlayer_->PlayPesPacket((void*)data, length, 0);
		    return length;
                }
	}

        try
        {
            // LogData(data, length, 0);

            UInt pesPacketLength = length;

            while (pesPacketLength > 0)
            {
		Mpeg::EsPacket esPacket(data, pesPacketLength);

                if (audioPlayerHd_)
                {
                    audioPlayerHd_->PlayPacket(esPacket);
                }
                if (audioPlayerBsp_)
                {
                    audioPlayerBsp_->PlayPacket(esPacket);
                }
            }
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            // Restart();
        }
        return length;
    }

    void ReelBoxDevice::PlayAudioRaw(AudioFrame const *frames, Int numFrames,
                                    SampleRate sampleRate, UInt pts)
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
#if VDRVERSNUM < 10716
        audioPlayback_ = 200;

        if (-- videoPlayback_ < 0)
        {
            videoPlayback_ = 0;
#if 1
            bkgPicPlayer_.Start();
#else
            if (audioBackgroundPics_)
                bkgPicPlayer_.Start();
            else
                bkgPicPlayer_.Stop();
#endif
        }
#else
        bkgPicPlayer_.PlayedAudio();
#endif

        if (audioPlayerHd_)
        {
            audioPlayerHd_->PlayFrames(frames, numFrames, sampleRate, pts, digitalAudio_);
        }
        if (audioPlayerBsp_)
        {
            audioPlayerBsp_->PlayFrames(frames, numFrames, sampleRate, pts, digitalAudio_);
        }
    }

   void ReelBoxDevice::SetAudioTrack(int index){
       if (index >=0)
          audioIndex = index;
       VideoPlayerHd *player = dynamic_cast<VideoPlayerHd*>(&VideoPlayer::Instance());
       if(player && oldAudioIndex != audioIndex)
		player->IncGen();
       oldAudioIndex = audioIndex;
       dsyslog_rb("SetAudioTrack: %i\n", index);
  }

#if VDRVERSNUM < 20301
eVideoSystem ReelBoxDevice::GetVideoSystem(void)
{
    return HdCommChannel::hda->video_mode.framerate == 50 ? vsPAL : vsNTSC;

}
//#else
//void GetVideoSize(int &Width, int &Height, double &VideoAspect)
//{
//    Width = HdCommChannel::hda->video_mode.width;
//    Height = HdCommChannel::hda->video_mode.height;
//    VideoAspect = (double)HdCommChannel::hda->aspect.w / (double)HdCommChannel::hda->aspect.h;
//}
#endif

int ReelBoxDevice::PlayTsVideo(const uchar *Data, int length)
       {
               CHECK_CONCURRENCY;
               if(needRestart) Restart();
               if(!TsHasPayload(Data)) return length;

               int pid = TsPid(Data);

               if(pid != playVideoPid_)
               {
                       dsyslog_rb("PlayTsVideo: new Vpid: %i\n", pid);
                       playVideoPid_ = pid;

                       bkgPicPlayer_.PlayedVideo();
                       if(ringBuffer)
                               ringBuffer->Clear();
               }

               PlayAudioVideoTS(Data,length);
//videoPlayer_->PlayTsPacket((void*)Data, length, playVideoPid_, playAudioPid_);
               return length;
       }

       int ReelBoxDevice::PlayTsAudio(const uchar *Data, int Length)
       {
               CHECK_CONCURRENCY;

               int pid = TsPid(Data);

               if(pid != playAudioPid_)
               {
                       dsyslog_rb("PlayTsAudio: new Apid: %i\n", pid);
                       playAudioPid_ = pid;

#if 1
                           bkgPicPlayer_.Start();
#else
                           if (audioBackgroundPics_)
                               bkgPicPlayer_.Start();
                           else
                               bkgPicPlayer_.Stop();
#endif

                       if(audioPlayerBsp_)
                       {
                               tsToPesConverter.Reset();
                               audioPlayerBsp_->Stop();
                       }
               }
//videoPlayer_->PlayTsPacket((void*)Data, Length, playVideoPid_, playAudioPid_);
               PlayAudioVideoTS(Data,Length);
//  return Length;
               if(!audioPlayerBsp_) //send packets to alsa too
                   return Length;

               try
        {
                       //first make PES and then ES for alsa output
                       UInt l;
                       if (const Reel::Byte *p = tsToPesConverter.GetPes((int&)l))
                       {
                               while (l > 0)
                               {
                                       Mpeg::EsPacket esPacket(p, l);
                                       audioPlayerBsp_->PlayPacket(esPacket);
                               }

                               tsToPesConverter.Reset();
                       }
                       tsToPesConverter.PutTs(Data, Length);
               }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            esyslog_rb("PlayTsAudio: exception caught\n");
        }
               return Length;
       }

       void ReelBoxDevice::PlayAudioVideoTS(const uchar *data, int length)
       {
               if(!ringBuffer)
                       ringBuffer = new cRingBufferLinear(RINGBUFSIZE, BUFFEREDTSPACKETSSIZE, false, "PlayTsBuffer");

               ringBuffer->Put(data, length);

               int count = ringBuffer->Available();
               if(count >= BUFFEREDTSPACKETSSIZE)
               {
                       uchar *b = ringBuffer->Get(count);

                       videoPlayer_->PlayTsPacket((void*)b, count, playVideoPid_, playAudioPid_);
                       ringBuffer->Del(count);
               }
       }

    Int ReelBoxDevice::PlayVideo(Byte const *data, Int length)
    {
        CHECK_CONCURRENCY;
        if(needRestart) Restart();

#if VDRVERSNUM >= 10716
        bkgPicPlayer_.PlayedVideo();
#endif

	if (useHDExtension_) {
#if VDRVERSNUM < 10716
		bkgPicPlayer_.Stop();
		videoPlayback_ = 3500;
#else
//		if((length > 7) && !data[0] && !data[1] && (data[2]==1) && (data[7]&0x20)) {
//			VideoPlayerHd *player = dynamic_cast<VideoPlayerHd*>(&VideoPlayer::Instance());
//			isyslog("ReelBoxDevice::PlayVideo: Broken Link %p", player);
//			if(player) player->IncGen();
//		} // if
#endif

if (length > 7 && !data[0] && !data[1] && data[2]==1 && (data[3]>=0xE0 && (data[3] <= 0xEF) )) //drop not video stream, ex 0xBE
		videoPlayer_->PlayPesPacket((void*)data, length, 1);
//else{
//if(length>30){
//int f = 0,off=0;
//esyslog(" !!! %X %X %X %X %X %X %X %X %X %X  %d\n",data[0+off+f],data[1+off+f],data[2+off+f],data[3+off+f],data[4+off+f],data[5+off+f],data[6+off+f],data[7+off+f],data[8+off+f],data[9+off+f],length);
//}
//}
		return length;
	}

        try
        {
            // LogData(data, length, 0);

#if VDRVERSNUM < 10716
            bkgPicPlayer_.Stop();
            videoPlayback_ = 3500;

            if (-- audioPlayback_ < 0)
            {
                audioPlayback_ = 0;
            }
#endif
            UInt pesPacketLength = length;

            // ::printf("PV\n");
            while (pesPacketLength > 0)
            {
                // ::printf(" *\n");
                Mpeg::EsPacket esPacket(data, pesPacketLength);
                if (videoPlayer_ && esPacket.GetMediaType() == Mpeg::MediaTypeVideo)
                {
                    videoPlayer_->PlayPacket(esPacket);
                }
            }
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);

            // PrintPes(data, length);
            // length = -1;
        }
        return length;
    }

    void ReelBoxDevice::PlayVideoEs(Byte const *data, Int length, UInt pts)
    {
        if(needRestart) Restart();
#if VDRVERSNUM < 10716
        bkgPicPlayer_.Stop();
        videoPlayback_ = 3500;

        if (-- audioPlayback_ < 0)
        {
            audioPlayback_ = 0;
        }
#else
        bkgPicPlayer_.PlayedVideo();
#endif

        Mpeg::EsPacket esPacket(data, length,
                                Mpeg::StreamIdVideoStream0,
                                Mpeg::SubStreamIdNone,
                                Mpeg::MediaTypeVideo,
                                pts);

        videoPlayer_->PlayPacket(esPacket);
    }

    void ReelBoxDevice::PlayPipVideo(Byte const *data, Int length)
    {
        CHECK_CONCURRENCY;

        try
        {
            UInt pesPacketLength = length;

            while (pesPacketLength > 0)
            {
                Mpeg::EsPacket esPacket(data, pesPacketLength);
                if (esPacket.GetMediaType() == Mpeg::MediaTypeVideo)
                {
                    videoPlayerPip_->PlayPacket(esPacket);
                }
            }
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
        }
    }

	int ReelBoxDevice::AproxFramesInQueue(void) {
		if(!videoPlayer_) return 0;
		return videoPlayer_->AproxFramesInQueue();
	} // ReelBoxDevice::AproxFramesInQueue

        bool ReelBoxDevice::ShowAudioBackgroundPics() { 
            if(!audioBackgroundPics_) return false;
            if((playMode_ == pmNone          ) ||
               (playMode_ == pmAudioOnlyBlack) ||
               (playMode_ == pmExtern_THIS_SHOULD_BE_AVOIDED)) return false;
            return true;
        };

	bool ReelBoxDevice::Poll(cPoller &Poller, Int timeoutMs)
    {
        // This function will be called by the vdr concurrently to the other functions.
        if (pipActive_)
        {
            return true;
        }

#if VDRVERSNUM < 10716
        if (!audioPlayback_ && !videoPlayback_)
        {
            return true;
        }
#else
        bool audioPlayback_ = true;
        bool videoPlayback_ = IsPlayingVideo();
#endif

        // We ingore the Poller, because we're not sure about threading issues.
        //poll only the player which sets STC ???
        bool ret = false;
        for (;;)
        {
	    if (audioPlayerHd_)
            {
                ret = audioPlayback_ &&  audioPlayerHd_ &&  audioPlayerHd_->Poll() || videoPlayback_ && videoPlayer_ && videoPlayer_->Poll();
            }
            if (audioPlayerBsp_)
            {
                ret = audioPlayback_ &&  audioPlayerBsp_ &&  audioPlayerBsp_->Poll() || videoPlayback_ && videoPlayer_ && videoPlayer_->Poll();
            }

            if (ret || timeoutMs <= 0)
            {
                break;
            }
            ::usleep(5000); // sleep 5 ms
            timeoutMs -= 5;
        }
        return ret;
    }

    bool ReelBoxDevice::PollAudio(int timeoutMs)
	{
	   bool ret = false;
#if VDRVERSNUM >= 10716
           bool audioPlayback_ = true;
#endif

	   for (;;)
	   {
            if (audioPlayerHd_)
            {
                ret = audioPlayback_ && audioPlayerHd_->PollAudio();
            }
            if (audioPlayerBsp_)
            {
                ret = audioPlayback_ && audioPlayerBsp_->PollAudio();
            }

	        if (ret || timeoutMs <= 0)
	        {
	            break;
	        }
	        ::usleep(500); // sleep 5 ms
	        timeoutMs -= 5;
	    }
	    return ret;
	}

    bool ReelBoxDevice::PollVideo(int timeoutMs)
    {
#if VDRVERSNUM >= 10716
           bool videoPlayback_ = IsPlayingVideo();
#endif
        bool ret = false;
        for (;;)
        {
            ret = videoPlayback_ && videoPlayer_ && videoPlayer_->PollVideo();
            if (ret || timeoutMs <= 0)
            {
                break;
            }
            ::usleep(500); // sleep 5 ms
            timeoutMs -= 5;
        }
        return ret;
    }
    //End by Klaus

    void ReelBoxDevice::SetAudioChannelDevice(int audioChannel)
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        audioChannel_ = audioChannel;
        AudioChannel channel = IsMute() ? AudioChannelMute : AudioChannel(audioChannel_);
        if (audioPlayerHd_)
        {
           audioPlayerHd_->SetChannel(channel);
        }
        if (audioPlayerBsp_)
        {
           audioPlayerBsp_->SetChannel(channel);
        }
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
    }

    void ReelBoxDevice::SetDigitalAudioDevice(bool on)
    {
        dsyslog_rb("%s ON? %s \n", __PRETTY_FUNCTION__, on?"YES":"NO");
        if (digitalAudio_ != on)
        {
            if (digitalAudio_)
            {
                ::usleep(1000000); // Wait until any leftover digital data has been flushed
            }
            digitalAudio_ = on;
            SystemExec(on ? "iecset audio on" : "iecset audio off");
            SetVolumeDevice(IsMute() ? 0 : CurrentVolume());
        }
        RestartAudio();
    }

    int64_t ReelBoxDevice::GetSTC()
    {
        int64_t stc;
	stc=HdCommChannel::hda->player[0].hde_last_pts; // needs hdplayer >SVN r12241
//        int64_t stc_base = ((((int64_t)HdCommChannel::hda->player[0].hde_stc_base_high)&0xFFFFFFFF)<<32)|(HdCommChannel::hda->player[0].hde_stc_base_low&0xFFFFFFFF);
//	printf("GET STC %llx\n",stc);
/*	if (audioPlayerHd_) {
		printf("GET STC %x\n",HdCommChannel::hda->player[0].stc+stc_);
		return HdCommChannel::hda->player[0].stc+stc_;
	}
*/
//        return (stc == 0) ? -1LL : stc;
        if(!stc) return -1LL; // just check if we have a valid stream
        if(!normalPlay) return stc; // Use pts if not playing normal speed
        // needs hdplayer >= SVN r17472
        int64_t stc_base   = ((((int64_t)HdCommChannel::hda->player[0].hde_stc_base_high)&0xFFFFFFFF)<<32)|(HdCommChannel::hda->player[0].hde_stc_base_low&0xFFFFFFFF);
        return stc_base;
    }

    void ReelBoxDevice::SetStc(bool stcValid, UInt stc)
    {
        stc  = (stc == 0) ? 1 : stc;
        stc_ = stcValid ? stc : 0;
    }

    void ReelBoxDevice::SetAudioBackgroundPics(bool active)
    {
	    audioBackgroundPics_ = active;
    }

    bool ReelBoxDevice::SetPlayMode(ePlayMode playMode)
    {
        CHECK_CONCURRENCY;
        dsyslog_rb("%s Playmode? %d \n", __PRETTY_FUNCTION__, playMode);
        bool ret = true;
        try
        {
            playMode_ = playMode;
            if (playMode == pmAudioVideo || playMode == pmAudioOnly || playMode == pmAudioOnlyBlack || playMode == pmVideoOnly)
            {
            SetPlayModeOn();
            }
	    else if (playMode == pmExtern_THIS_SHOULD_BE_AVOIDED)
	    {
	     SetPlayModeOff();
             needRestart=true;
//	     Reel::HdCommChannel::SetAspect();
	    }
            else
            {
                SetPlayModeOff();
//                Reel::HdCommChannel::SetAspect();
            }
#if VDRVERSNUM >= 10716
            bkgPicPlayer_.ResetTimer();
#endif
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            ret = false;
        }
        return ret;
    }

    void ReelBoxDevice::SetVideoFormat(bool videoFormat16_9)
    {
        if (useHDExtension_)
        {
            // HD
            if (tmpHDaspect == -1)
                tmpHDaspect = RBSetup.HDaspect;
            else
            {
                tmpHDaspect = (tmpHDaspect + 1) % 3;
                Reel::HdCommChannel::SetAspect(tmpHDaspect);
                switch(tmpHDaspect) {
                    case 0:
                        Skins.Message(mtInfo, tr("Fill to Screen"));
                        break;
                    case 1:
                        Skins.Message(mtInfo, tr("Fill to Aspect"));
                        break;
                    case 2:
                        Skins.Message(mtInfo, tr("Crop to Fill"));
                        break;
                }
            }
        }
        else
        {
            // BSP

            try
            {
                int pip=0;
                Bsp::BspCommChannel &bspCommChannel = Bsp::BspCommChannel::Instance();

                if (videoFormat16_9)
                {
                    bspCommChannel.bspd->video_player[pip].aspect[0]= 16;
                    bspCommChannel.bspd->video_player[pip].aspect[1]= 9;
                }
                else
                {
                    bspCommChannel.bspd->video_player[pip].aspect[0]= 4;
                    bspCommChannel.bspd->video_player[pip].aspect[1]= 3;
                }
                bspCommChannel.bspd->video_player[pip].changed++;
            }
            catch (std::exception const &e)
            {
                REEL_LOG_EXCEPTION(e);
                Restart();
            }
        }
    }

    void ReelBoxDevice::SetVolumeDevice(int volume)
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        AudioChannel channel = volume ? AudioChannel(audioChannel_) : AudioChannelMute;
        if(audioPlayerHd_)
        {
            audioPlayerHd_->SetChannel(channel);
            HdCommChannel::SetVolume(volume);
        }
        if(audioPlayerBsp_)
        {
            audioPlayerBsp_->SetChannel(channel);
        }

        if (digitalAudio_ )  // we have ac3
        {
            if (RBSetup.ac3 || !useHDExtension_)
            {
                // ac3 over spdif
                // mute all analog  outputs !
                MUTE("PCM");
                MUTE("CD");
                SET_VOLUME(0);
            }
            else if (!RBSetup.ac3)
            {
                // HD-Ext decode AC3 -> analog -> CD -> Realtek
                MUTE("PCM");
                UNMUTE("CD");
                SET_VOLUME(volume);
            }
        }
        else   // mp2
        {

            if (useHDExtension_) // bypass
            {
                MUTE("PCM");
                UNMUTE("CD");
                SET_VOLUME(volume);
            }
            else
            {
                MUTE("CD");
                UNMUTE("PCM");
                SET_VOLUME(volume);
            }

            if (volume == 0)
            {
                // be silent
                SET_VOLUME(0);
            }
        }

    }

    void ReelBoxDevice::SetVolume(int Volume, const char *device)
    {
        dsyslog_rb("%s\n", __PRETTY_FUNCTION__);
        int err;
        snd_mixer_t *handle;
        const char *card= "default";
        snd_mixer_elem_t *elem;
        snd_mixer_selem_id_t *sid;
        snd_mixer_selem_id_alloca(&sid);
#ifdef __x86_64
        unsigned int channels = ~0U;
#else
        unsigned int channels = ~0UL;
#endif
        snd_mixer_selem_channel_id_t chn;

        snd_mixer_selem_id_set_name(sid, device);
        if ((err = snd_mixer_open(&handle, 0)) < 0) {
            esyslog_rb("Mixer %s open error: %s\n", card, snd_strerror(err));
            return;
        }
        if ((err = snd_mixer_attach(handle, card)) < 0) {
            esyslog_rb("Mixer attach %s error: %s\n", card, snd_strerror(err));
            snd_mixer_close(handle);
            return;
        }
        if ((err = snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
            esyslog_rb("Mixer register error: %s\n", snd_strerror(err));
            snd_mixer_close(handle);
            return;
        }
        err = snd_mixer_load(handle);
        if (err < 0) {
            esyslog_rb("Mixer %s load error: %s\n", card, snd_strerror(err));
            snd_mixer_close(handle);
            return;
        }
        elem = snd_mixer_find_selem(handle, sid);
        if (!elem) {
            esyslog_rb("Unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
            snd_mixer_close(handle);
            return;
        }

        for (chn = static_cast<snd_mixer_selem_channel_id_t> (0); chn <= SND_MIXER_SCHN_LAST; chn = static_cast<snd_mixer_selem_channel_id_t>(chn + 1))
        {
            if (!(channels & (1 << chn)))
                continue;
            if (snd_mixer_selem_has_playback_channel(elem, chn)) {
                    if (snd_mixer_selem_has_playback_volume(elem)) {
                        long min, max;
                                               if(snd_mixer_selem_get_playback_volume_range(elem, &min, &max) >= 0)
                                               {
                                                       unsigned int percent = 100*Volume/MAXVOLUME;
                                                       long relVol = (max-min)*percent/100 + min;
                                                       snd_mixer_selem_set_playback_volume(elem, chn, relVol);
                                               }

                    }
            }
        }
        snd_mixer_close(handle);
    }

    void ReelBoxDevice::StartPip(bool on)
    {
        pipActive_ = on;
        if (on)
        {
            videoPlayerPip_->Start();
        }
        else
        {
            videoPlayerPip_->Stop();
        }
    }

    void ReelBoxDevice::StillPicture(Byte const *data, Int length)
    {
        CHECK_CONCURRENCY;
        needRestart=true;
        try
        {
	    tsMode_ = false;
            std::vector<Mpeg::EsPacket> videoPackets;
#if VDRVERSNUM >= 10716
            if (length && data[0] == 0x47) {
                return cDevice::StillPicture(data, length);
            } else 
#endif
            if (length >= 4 &&
                data[0] == 0x00 &&
                data[1] == 0x00 &&
                data[2] == 0x01 &&
                data[3] <= 0xB8)
            {
                // ES
                int const maxLen = 2000;
                while (length > 0)
                {
                    int const l = length > maxLen ? maxLen : length;
                    Mpeg::EsPacket const esPacket(data, l,
                                                  Mpeg::StreamIdVideoStream0,
                                                  Mpeg::SubStreamIdNone,
                                                  Mpeg::MediaTypeVideo);
                    videoPackets.push_back(esPacket);
                    data += l;
                    length -= l;
                }
            }
            else
            {
                // PES
                UInt pesLength = length;

                // Parse packets.
                while (pesLength)
                {
                    Mpeg::EsPacket const esPacket(data, pesLength);
                    if (esPacket.GetMediaType() == Mpeg::MediaTypeVideo)
                    {
                        videoPackets.push_back(esPacket);
                    }
                }
            }

            // Display them.
            if (videoPackets.size())
            {
                videoPlayer_->StillPicture(&videoPackets[0], videoPackets.size(), tsMode_);
            }
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            // Restart();
        }
    }

#if VDRVERSNUM < 20103
    void ReelBoxDevice::TrickSpeed(Int speed)
#else
    void ReelBoxDevice::TrickSpeed(Int speed, bool forward)
#endif
    {
        try
        {
            // printf("Trick speed %i\n",speed);
            normalPlay = false;
/*            if (audioPlayerHd_)
            {
                audioPlayerHd_->Freeze();
		}*/
            if (audioPlayerBsp_)
            {
                audioPlayerBsp_->Freeze();
            }

            videoPlayer_->Trickmode(speed);
        }
        catch (std::exception const &e)
        {
            REEL_LOG_EXCEPTION(e);
            Restart();
        }
    }

    void ReelBoxDevice::SetPipDimensions(uint x, uint y, uint width, uint height)
    {
        videoPlayerPip_->SetDimensions(x, y, width, height);
    }

    void ReelBoxDevice::SetPlayModeOff()
    {
//        printf ("[reelbox] SetPlayModeOff() \n");
        //esyslog ("[reelbox] SetPlayModeOff() \n");
#if VDRVERSNUM < 10716
        audioPlayback_ = 0;
        videoPlayback_ = 0;

        bkgPicPlayer_.Stop();
#endif
	normalPlay = false;
        if (audioPlayerHd_)
        {
            audioPlayerHd_->Stop();
        }
        if (audioPlayerBsp_)
        {
            audioPlayerBsp_->Stop();
        }

        videoPlayer_->Stop();
    }

    void ReelBoxDevice::SetPlayModeOn()
    {
	if(needRestart)
	{
		Restart();
		Reel::HdCommChannel::SetAspect();
	}

	dsyslog_rb("SetPlayModeOn needRestart\n");

	while(!HdCommChannel::hda->hdp_running){
		HdCommChannel::hda->hdp_enable = 1;
		//dsyslog_rb("hda->hdp_running: %d\n", hda->hdp_running);
		usleep(10000);
	}
//	if (!HdCommChannel::hda->hdp_running)
//	{
//dsyslog("===========hda->hdp_running \n");
//	    HdCommChannel::hda->hdp_enable = 1;
//	    Reel::HdCommChannel::SetAspect();
//        }
        //dsyslog("===========SetPlayModeOn \n");
//        printf ("[reelbox] SetPlayModeOn() \n");

#if VDRVERSNUM < 10716
        audioPlayback_ = 100;
        videoPlayback_ = 10;
#endif
	normalPlay = true;

        if (audioPlayerHd_)
        {
            dsyslog_rb("audioPlayerHd_->Start()\n");
            audioPlayerHd_->Start();
        }
        if (audioPlayerBsp_)
        {
            dsyslog_rb("NO audioPlayerHd\n");
            audioPlayerBsp_->Start();
        }

        if (tmpHDaspect != RBSetup.HDaspect && useHDExtension_)
        {
            tmpHDaspect = RBSetup.HDaspect;
//            Reel::HdCommChannel::SetVideomode();
        }

	dsyslog_rb("Call videoPlayer_->Start()\n");
        videoPlayer_->Start();
    }

    void ReelBoxDevice::Create()
    {
        if (!instance_)
        {
            instance_ = new ReelBoxDevice;
        }
    }

    void ReelBoxDevice::MakePrimaryDevice(bool On)
    {
       if (On)
       {
          HdOsdProvider::Create();
       }
    }
}
