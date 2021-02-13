/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavcodec API use example.
 *
 * Note that libavcodec only handles codecs (mpeg, mpeg4, etc...),
 * not file formats (avi, vob, mp4, mov, mkv, mxf, flv, mpegts, mpegps, etc...). See library 'libavformat' for the
 * format handling
 */

#include <math.h>

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audioconvert.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

//uint8_t *inbuf[AUDIO_INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];

/*
 * Audio decoding.
 */
static void soft_decode(unsigned char *outbuf,int out_len, unsigned char *inbuf, int in_len, AVCodecID codec_id)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    int len;
    out_len=0;

    AVPacket avpkt;
    AVFrame *decoded_frame = NULL;

    av_init_packet(&avpkt);

    /* find the audio decoder */

//codec_id=AV_CODEC_ID_EAC3;

    codec = avcodec_find_decoder(codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);

    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }


    avpkt.data = inbuf;
    avpkt.size = in_len;

    while (avpkt.size > 0) {
        int got_frame = 0;

        if (!decoded_frame) {
            if (!(decoded_frame = avcodec_alloc_frame())) {
                fprintf(stderr, "Could not allocate audio frame\n");
                exit(1);
            }
        } else
            avcodec_get_frame_defaults(decoded_frame);

        len = avcodec_decode_audio4(c, decoded_frame, &got_frame, &avpkt);
        if (len < 0) {
            fprintf(stderr, "Error while decoding\n");
            exit(1);
        }
        if (got_frame) {
            /* if a frame has been decoded, output it */
            int data_size = av_samples_get_buffer_size(NULL, c->channels,
                                                       decoded_frame->nb_samples,
                                                       c->sample_fmt, 1);
            memcpy(outbuf+out_len, decoded_frame->data[0],data_size);
            out_len+=data_size;

        }
        avpkt.size -= len;
        avpkt.data += len;
        avpkt.dts =
        avpkt.pts = AV_NOPTS_VALUE;
//        if (avpkt.size < AUDIO_REFILL_THRESH) {
            /* Refill the input buffer, to avoid trying to decode
             * incomplete frames. Instead of this, one could also use
             * a parser, or use a proper container format through
             * libavformat. */
//            memmove(inbuf, avpkt.data, avpkt.size);
//            avpkt.data = inbuf;
//            len = fread(avpkt.data + avpkt.size, 1,
//                        AUDIO_INBUF_SIZE - avpkt.size, f);
//            if (len > 0)
//                avpkt.size += len;
//        }
    }


    avcodec_close(c);
    av_free(c);

    avcodec_free_frame(&decoded_frame);
}

/*
int main(int argc, char **argv)
{
    const char *output_type;

    // register all the codecs //FIXED: "/*" within comment
//    avcodec_register_all();

    if (argc < 2) {
        printf("usage: %s output_type\n"
               "API example program to decode/encode a media stream with libavcodec.\n"
               "This program generates a synthetic stream and encodes it to a file\n"
               "named test.h264, test.mp2 or test.mpg depending on output_type.\n"
               "The encoded stream is then decoded and written to a raw data output\n."
               "output_type must be choosen between 'h264', 'mp2', 'mpg'\n",
               argv[0]);
        return 1;
    }

    return 0;
}
*/
