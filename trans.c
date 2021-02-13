/* Decode -encode */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avstring.h>

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

AVFormatContext *pFormatCtxIn1;
AVFormatContext *pFormatCtxOut;
const char *filein1, *fileout;
AVOutputFormat *fmt;

int i, j;
AVCodecContext *pCodecCtxIn1;
AVCodecContext *pCodecCtxOut;
uint8_t *outbuf;
int outbuf_size = 0;
AVCodec *input_codecs1[MAX_STREAMS];
AVCodec *output_codecs[MAX_STREAMS];
AVFrame *pFrame, *pFrameRGB;
AVPacket packet, pkt;
int got_picture;
int out_size;
int numBytes;
uint8_t *buffer;
int videoStream1;

typedef struct AVOutputStream {
	int file_index; /* file index */
	int index; /* stream index in the output file */
	int source_index; /* AVInputStream index */
	AVStream *st; /* stream in the output file */
	int encoding_needed; /* true if encoding needed for this stream */
	int frame_number;
	/* input pts and corresponding output pts
	 for A/V sync */
	struct AVInputStream *sync_ist; /* input stream to sync against */
	int64_t sync_opts;
	/* video only */
	struct SwsContext *img_resample_ctx; /* for image resampling */
	int resample_height;
	int resample_width;
	int resample_pix_fmt;

	/* full frame size of first frame */
	int original_height;
	int original_width;

} AVOutputStream;

typedef struct AVInputStream {
	int file_index;
	int index;
	AVStream *st;
	int decoding_needed; /* true if the packets must be decoded in 'raw_fifo' */
	int64_t sample_index; /* current sample */

	int64_t next_pts; /* synthetic pts for cases where pkt.pts
	 is not defined */
	int64_t pts; /* current pts */
	int is_start; /* is 1 at the start and after a discontinuity */
} AVInputStream;

AVOutputStream *ost, **ost_table = NULL;
AVInputStream *ist1, **ist_table1 = NULL;

int new_video_stream(AVFormatContext *oc, AVStream *ist) {
	AVStream *st;
	AVCodecContext *video_enc;
	float frame_aspect_ratio = 0;
	AVCodec *codec;
	enum CodecID codec_id;

	st = av_new_stream(oc, oc->nb_streams);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		return -1;
	}

	avcodec_get_context_defaults2(st->codec, AVMEDIA_TYPE_VIDEO);

	video_enc = st->codec;

	codec_id = av_guess_codec(oc->oformat, NULL, oc->filename, NULL,
			AVMEDIA_TYPE_VIDEO);
	codec = avcodec_find_encoder(codec_id);

	if (ist->sample_aspect_ratio.num)
		frame_aspect_ratio = av_q2d(ist->sample_aspect_ratio);
	else
		frame_aspect_ratio *= (float) ist->codec->width / ist->codec->height;

	video_enc->codec_id = codec_id;

	video_enc->codec_type = AVMEDIA_TYPE_VIDEO;
	video_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;

	video_enc->time_base.den = ist->codec->time_base.den;
	video_enc->time_base.num = ist->codec->time_base.num;

	video_enc->bit_rate = 512000;
	video_enc->width = ist->codec->width;
	video_enc->height = ist->codec->height;

	video_enc->sample_aspect_ratio = av_d2q(frame_aspect_ratio
			* video_enc->height / video_enc->width, 255);
	video_enc->pix_fmt = ist->codec->pix_fmt;
	video_enc->gop_size = ist->codec->gop_size;
	st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

	st->avg_frame_rate.den = ist->avg_frame_rate.den;
	st->avg_frame_rate.num = ist->avg_frame_rate.num;

	video_enc->max_b_frames = ist->codec->max_b_frames;

	if (codec && codec->pix_fmts) {
		const enum PixelFormat *p = codec->pix_fmts;
		for (; *p != -1; p++) {
			if (*p == video_enc->pix_fmt)
				break;
		}
		if (*p == -1)
			video_enc->pix_fmt = codec->pix_fmts[0];
	}

	video_enc->rc_override_count = 0;
	video_enc->me_threshold = 0;
	video_enc->intra_dc_precision = 0;

	return 0;
}

int new_audio_stream(AVFormatContext *oc, AVStream *ist) {
	AVStream *st;
	AVCodecContext *audio_enc;

	st = av_new_stream(oc, oc->nb_streams);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		return -1;
	}
	avcodec_get_context_defaults2(st->codec, AVMEDIA_TYPE_AUDIO);

	audio_enc = st->codec;
	audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

	audio_enc->flags |= CODEC_FLAG_GLOBAL_HEADER;

	audio_enc->codec_id = CODEC_ID_MP3;
	st->stream_copy = 1;
	audio_enc->bit_rate = 56000;
	audio_enc->extradata = ist->codec->extradata;
	audio_enc->extradata_size = ist->codec->extradata_size;
	audio_enc->channels = ist->codec->channels;

	audio_enc->sample_rate = 22050;
	audio_enc->time_base.den = 1;
	audio_enc->time_base.num = audio_enc->sample_rate;

	return 0;
}
int init_input_context(int argc, char *argv[]) {
	if (argc != 3) {
		printf("use: %s input_file1 output_file\n", argv[0]);
		return -1;
	}

	avcodec_register_all();
	av_register_all();

	filein1 = argv[1];

	pFormatCtxIn1 = avformat_alloc_context();
	if (!pFormatCtxIn1) {
		fprintf(stderr, "Memory error\n");
		return -1;
	}

	if (av_open_input_file(&pFormatCtxIn1, filein1, NULL, 0, NULL) != 0) {
		printf("Cannot open the file: %s\n", filein1);
		return -1;
	}

	fileout = argv[2];

	return 0;
}

int init_output_context() {
	fmt = av_guess_format(NULL, fileout, NULL);
	if (!fmt) {
		printf("Cannot guess output format: using MPEG.\n");
		fmt = av_guess_format("mpeg", NULL, NULL);
	}
	if (!fmt) {
		fprintf(stderr, "Cannot find any output format\n");
		return -1;
	}

	pFormatCtxOut = avformat_alloc_context();
	if (!pFormatCtxOut) {
		fprintf(stderr, "Memory \n");
		return -1;
	}
	pFormatCtxOut->oformat = fmt;
	av_strlcpy(pFormatCtxOut->filename, fileout,
			sizeof(pFormatCtxOut->filename));
	pFormatCtxOut->timestamp = 0;
	if (url_fopen(&pFormatCtxOut->pb, fileout, URL_WRONLY) < 0) {
		fprintf(stderr, "Cannot open '%s'\n", fileout);
		return -1;
	}
	return 0;
}

int init_input_stream() {
	if (av_find_stream_info(pFormatCtxIn1) < 0)
		return -1;
	printf(
			"\n ---------------------------------------------------------------------- \n");
	dump_format(pFormatCtxIn1, 0, filein1, 0);
	printf(
			"\n ---------------------------------------------------------------------- \n");

	// Init input streams
	ist_table1 = (AVInputStream **) av_mallocz(pFormatCtxIn1->nb_streams
			* sizeof(AVInputStream *));
	if (!ist_table1)
		return -1;
	// first allocate memory for av input stream
	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		ist1 = (AVInputStream *) av_mallocz(sizeof(AVInputStream));
		if (!ist1)
			return -1;
		ist_table1[i] = ist1;
	}
	// allocate input video stream to input stream
	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		ist1 = ist_table1[i];
		ist1->st = pFormatCtxIn1->streams[i];
		ist1->file_index = 0;
		ist1->index = i;
	}

	return 0;
}

int init_output_stream() {
	// Init output streams
	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		AVCodecContext *enc = pFormatCtxIn1->streams[i]->codec;
		switch (enc->codec_type) {
		case AVMEDIA_TYPE_AUDIO:
			if (new_audio_stream(pFormatCtxOut, pFormatCtxIn1->streams[i]) != 0)
				return -1;
			break;
		case AVMEDIA_TYPE_VIDEO:
			if (new_video_stream(pFormatCtxOut, pFormatCtxIn1->streams[i]) != 0)
				return -1;
			break;
		default:
			break;
		}
	}

	if (!pFormatCtxOut->nb_streams) {
		fprintf(stderr, "Output file %s doesn't contain any stream\n", fileout);
		return -1;
	}

	ost_table = (AVOutputStream **) av_mallocz(pFormatCtxOut->nb_streams
			* sizeof(AVOutputStream *));
	if (!ost_table)
		return -1;

	for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
		ost = (AVOutputStream *) av_mallocz(sizeof(AVOutputStream));
		if (!ost)
			return -1;
		ost_table[i] = ost;
	}

	return 0;
}
int verify_streams_coreleation() {
	// verify input and output streams co relation.
	for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
		ost = ost_table[i];
		ost->st = pFormatCtxOut->streams[i];
		ost->file_index = 0;
		ost->index = i;
		int found = 0;
		for (j = 0; j < pFormatCtxIn1->nb_streams; j++) {
			ist1 = ist_table1[j];
			if (ist1->st->codec->codec_type == ost->st->codec->codec_type) {
				ost->source_index = j;
				found = 1;
				break;
			}
		}

		if (!found) {
			fprintf(stderr,
					"Some output stream doesn't fit with any input stream\n");
			return -1;
		}
		ist1 = ist_table1[ost->source_index];
		ost->sync_ist = ist1;
	}

	return 0;
}
int verify_input_videostream() {
	// verify if video stream is present in input file.
	videoStream1 = -1;
	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		if (pFormatCtxIn1->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStream1 = i;
			break;
		}
	}
	if (videoStream1 == -1) {
		fprintf(stderr, "There is no video stream in the input file");
		return -1;
	}
	return 0;
}
int set_metadata() {
	for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
		ost = ost_table[i];
		ist1 = ist_table1[ost->source_index];

		pCodecCtxOut = ost->st->codec;
		pCodecCtxIn1 = ist1->st->codec;

		AVMetadataTag *lang;
		if ((lang = av_metadata_get(ist1->st->metadata, "language", NULL, 0))
				&& !av_metadata_get(ost->st->metadata, "language", NULL, 0))
			av_metadata_set2(&ost->st->metadata, "language", lang->value, 0);

		ost->st->disposition = ist1->st->disposition;
		pCodecCtxOut->bits_per_raw_sample = pCodecCtxIn1->bits_per_raw_sample;
		pCodecCtxOut->chroma_sample_location
				= pCodecCtxIn1->chroma_sample_location;

		if (pCodecCtxOut->codec_type == AVMEDIA_TYPE_VIDEO) {
			if (pCodecCtxIn1->pix_fmt == PIX_FMT_NONE) {
				fprintf(stderr,
						"Pixel format unknown. The stream can't be decoded\n");
				return -1;
			}
			ost->encoding_needed = 1;
			ist1->decoding_needed = 1;
			int size = pCodecCtxOut->width * pCodecCtxOut->height;
			outbuf_size = FFMAX(outbuf_size, 6*size + 200);
		}
	}
	// set the output buffer size
	outbuf = NULL;
	outbuf = (uint8_t *) av_malloc(outbuf_size);
	return 0;
}

int show_output_parameters() {
	// Output parameters
	if (av_set_parameters(pFormatCtxOut, NULL) < 0) {
		fprintf(stderr, "Invalid output parameters\n");
		return -1;
	}

	dump_format(pFormatCtxOut, 0, fileout, 1);

	printf("\n\n\n ---------------------------------");
	return 0;
}
int open_encoders_decoders() {
	// Open encoders
	for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
		ost = ost_table[i];
		if (ost->encoding_needed) {
			AVCodec *codec = output_codecs[i];
			codec = avcodec_find_encoder(ost->st->codec->codec_id);
			printf("\n codec-id is %d \n", ost->st->codec->codec_id);
			if (!codec) {
				fprintf(stderr, "Cannot find any encoder for the output file\n");
				return -1;
			}
			if (avcodec_open(ost->st->codec, codec) < 0) {
				fprintf(stderr, "Error opening encoder\n");
				return -1;
			}
		}
	}

	// Open decoders
	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		ist1 = ist_table1[i];
		if (ist1->decoding_needed) {
			AVCodec *codec = input_codecs1[i];
			codec = avcodec_find_decoder(ist1->st->codec->codec_id);
			if (!codec) {
				fprintf(stderr, "Cannot find any decoder for the input file\n");
				return -1;
			}
			if (avcodec_open(ist1->st->codec, codec) < 0) {
				fprintf(stderr, "Error opening decoder\n");
				return -1;
			}
		}
	}
	return 0;
}

int init_pts() {
	// Init the pts
	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		ist1 = ist_table1[i];
		ist1->pts = 0;
		ist1->next_pts = AV_NOPTS_VALUE;
		ist1->is_start = 1;
	}
	return 0;
}
int allocate_frame_buffer() {
	pFrame = avcodec_alloc_frame();
	// With this var we will work with RGB
	pFrameRGB = avcodec_alloc_frame();
	if (pFrame == NULL || pFrameRGB == NULL)
		return -1;

	numBytes = avpicture_get_size(PIX_FMT_YUV420P,
			ist_table1[videoStream1]->st->codec->width,
			ist_table1[videoStream1]->st->codec->height);
	buffer = malloc(numBytes * sizeof(uint8_t));
	avpicture_fill((AVPicture *) pFrameRGB, buffer, PIX_FMT_YUV420P,
			ist_table1[videoStream1]->st->codec->width,
			ist_table1[videoStream1]->st->codec->height);

	return 0;
}

int write_header() {
	printf("\n Writing Header \n");

	if (av_write_header(pFormatCtxOut) < 0) {
		fprintf(stderr, "Error writing file header\n");
		return -1;
	}
	return 0;
}
int read_packet_loop() {
	while (av_read_frame(pFormatCtxIn1, &packet) >= 0) {
		//printf("%d \n", packet.stream_index);
		ist1 = ist_table1[packet.stream_index];
		int64_t offset = 0 - pFormatCtxIn1->start_time;
		AVRational avrat_base = { 1, AV_TIME_BASE };

		if (packet.dts != AV_NOPTS_VALUE)
			packet.dts += av_rescale_q(offset, avrat_base, ist1->st->time_base);
		if (packet.pts != AV_NOPTS_VALUE)
			packet.pts += av_rescale_q(offset, avrat_base, ist1->st->time_base);

		if (ist1->next_pts == AV_NOPTS_VALUE)
			ist1->next_pts = ist1->pts;
		if (packet.dts != AV_NOPTS_VALUE)
			ist1->next_pts = ist1->pts = av_rescale_q(packet.dts,
					ist1->st->time_base, avrat_base);
		ist1->pts = ist1->next_pts;

		if (ist1->st->codec->codec_type == AVMEDIA_TYPE_VIDEO
				&& ist1->decoding_needed) {

			avcodec_get_frame_defaults(pFrame);

			if (avcodec_decode_video2(ist1->st->codec, pFrame, &got_picture,
					&packet) < 0) {
				fprintf(stderr, "Error decoding video\n");
				return -1;
			}
			ist1->st->quality = pFrame->quality;

			if (got_picture) {

				if (ist1->st->codec->time_base.num != 0) {
					int ticks =
							ist1->st->parser ? ist1->st->parser->repeat_pict
									+ 1 : ist1->st->codec->ticks_per_frame;
					ist1->next_pts += ((int64_t) AV_TIME_BASE
							* ist1->st->codec->time_base.num * ticks)
							/ ist1->st->codec->time_base.den;
				}
				for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
					ost = ost_table[i];
					if (ost->source_index == packet.stream_index
							&& ost->encoding_needed
							&& ost->st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
						av_init_packet(&pkt);
						pkt.stream_index = ost->index;

						AVFrame big_picture;

						big_picture = *pFrame;

						big_picture.interlaced_frame = pFrame->interlaced_frame;
						big_picture.quality = ist1->st->quality;
						big_picture.pict_type = 0;
						big_picture.pts = ost->sync_opts;

						out_size = avcodec_encode_video(ost->st->codec, outbuf,
								outbuf_size, &big_picture);

						if (out_size < 0) {
							fprintf(stderr, "Error video encoding\n");
							return -1;
						} else {
							//printf("%d\n",out_size);
							pkt.data = outbuf;
							pkt.size = out_size;
							if (ost->st->codec->coded_frame->pts
									!= AV_NOPTS_VALUE)
								pkt.pts = av_rescale_q(
										ost->st->codec->coded_frame->pts,
										ost->st->codec->time_base,
										ost->st->time_base);
							//pkt.pts = pFrame->pts;
							if (ost->st->codec->coded_frame->key_frame)
								pkt.flags |= PKT_FLAG_KEY;
							if (av_interleaved_write_frame(pFormatCtxOut, &pkt)
									< 0) {
								fprintf(stderr,
										"Error writing frame in the output file\n");
								return -1;
							} else {
								//printf("\n written encoded video nframe %d \n",pFrame->coded_picture_number);
							}
						}

						av_free_packet(&pkt);
						ost->sync_opts++;
						ost->frame_number++;
					}
				}
			}
		} else {
			//printf(" \n audio encoding \n");

			ist1->next_pts += ((int64_t) AV_TIME_BASE
					* ist1->st->codec->frame_size)
					/ ist1->st->codec->sample_rate;
			for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
				ost = ost_table[i];
				if (ost->source_index == packet.stream_index) {
					AVFrame directframe;
					AVPacket opkt;
					int64_t ost_tb_start_time = av_rescale_q(0, avrat_base,
							ost->st->time_base);
					av_init_packet(&opkt);

					avcodec_get_frame_defaults(&directframe);
					ost->st->codec->coded_frame = &directframe;
					directframe.key_frame = packet.flags & PKT_FLAG_KEY;
					opkt.stream_index = packet.stream_index;

					if (packet.pts != AV_NOPTS_VALUE)
						opkt.pts = av_rescale_q(packet.pts,
								ist1->st->time_base, ost->st->time_base)
								- ost_tb_start_time;
					else
						opkt.pts = AV_NOPTS_VALUE;

					if (packet.dts == AV_NOPTS_VALUE)
						opkt.dts = av_rescale_q(ist1->pts, avrat_base,
								ost->st->time_base);
					else
						opkt.dts = av_rescale_q(packet.dts,
								ist1->st->time_base, ost->st->time_base);
					opkt.dts -= ost_tb_start_time;

					opkt.duration = av_rescale_q(packet.duration,
							ist1->st->time_base, ost->st->time_base);
					opkt.flags = packet.flags;

					if (ost->st->codec->codec_id != CODEC_ID_H264
							&& ost->st->codec->codec_id != CODEC_ID_MPEG1VIDEO
							&& ost->st->codec->codec_id != CODEC_ID_MPEG2VIDEO) {
						if (av_parser_change(ist1->st->parser, ost->st->codec,
								&opkt.data, &opkt.size, packet.data,
								packet.size, packet.flags & PKT_FLAG_KEY))
							opkt.destruct = av_destruct_packet;
					} else {
						opkt.data = packet.data;
						opkt.size = packet.size;n
					}

					if (av_interleaved_write_frame(pFormatCtxOut, &opkt) < 0) {
						fprintf(stderr,
								"Error writing frame in the output file\n");
						return -1;
					} else {
						//printf("\n written encoded audio  frame %d \n",ost->st->codec->frame_number);

					}
					ost->st->codec->frame_number++;
					ost->frame_number++;
					av_free_packet(&opkt);
				}
			}

		}

		av_free_packet(&packet);
	}
	return 0;
}
int av_cleanup() {
	for (i = 0; i < pFormatCtxOut->nb_streams; i++) {
		ost = ost_table[i];
		if (ost->encoding_needed) {
			av_freep(&ost->st->codec->stats_in);
			avcodec_close(ost->st->codec);
		}
		av_metadata_free(&pFormatCtxOut->streams[i]->metadata);
		av_free(pFormatCtxOut->streams[i]->codec);
		av_free(pFormatCtxOut->streams[i]);

		av_free(ost);
	}
	av_free(ost_table);

	for (i = 0; i < pFormatCtxIn1->nb_streams; i++) {
		ist1 = ist_table1[i];
		if (ist1->decoding_needed) {
			avcodec_close(ist1->st->codec);
		}
		av_free(ist1);
	}
	av_free(ist_table1);

	free(buffer);
	av_free(pFrameRGB);

	av_free(pFrame);

	av_freep(&outbuf);

	av_close_input_file(pFormatCtxIn1);

	if (!(fmt->flags & AVFMT_NOFILE)) {
		url_fclose(pFormatCtxOut->pb);
	}

	av_metadata_free(&pFormatCtxOut->metadata);
	av_free(pFormatCtxOut);
	return 0;
}

int main(int argc, char *argv[]) {

	init_input_context(argc, argv);
	init_output_context();
	init_input_stream();
	init_output_stream();
	verify_streams_coreleation();
	verify_input_videostream();
	set_metadata();
	show_output_parameters();
	open_encoders_decoders();
	init_pts();
	allocate_frame_buffer();
	write_header();
	read_packet_loop();
	av_write_trailer(pFormatCtxOut);
	av_cleanup();
	return 0;
}