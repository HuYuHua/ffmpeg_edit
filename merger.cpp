/*
 * Copyright (c) 2013 Stefano Sabatini
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
 * libavformat/libavcodec demuxing and muxing API example.
 *
 * Remux streams from one container format to another.
 * @example remuxing.c
 */
#ifdef __cplusplus
extern "C"
{
#endif

#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavutil/time.h>

#ifdef __cplusplus
};
#endif
#include <pthread.h>
#include <unistd.h>
#include <list>
#include <mutex>
#include <string>
#include <vector>


#define MAX_INPUT_NUM   10

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

int main(int argc, char **argv)
{
    AVOutputFormat *ofmt = NULL;
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVFormatContext *ifmt_ctxs[MAX_INPUT_NUM];
    AVPacket pkt;
    const char *in_filename[MAX_INPUT_NUM], *out_filename;
    int ret, i;
    int infile_num = 0;
    uint32_t next_vpts = 0, next_apts = 0;

    if (argc < 4) {
        printf("usage: %s if0 if1 [...] output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n", argv[0]);
        return 1;
    }

    infile_num = argc - 2;
    for (i = 0; i < infile_num; i++) {
        in_filename[i] = argv[i+1];
        ifmt_ctxs[i] = NULL;
    }
    out_filename = argv[argc-1];

    av_register_all();
    
    for (i = 0; i < infile_num; i++) {
        if ((ret = avformat_open_input(&ifmt_ctxs[i], in_filename[i], 0, 0)) < 0) {
            fprintf(stderr, "Could not open input file '%s'", in_filename[i]);
            goto end;
        }

        if ((ret = avformat_find_stream_info(ifmt_ctxs[i], 0)) < 0) {
            fprintf(stderr, "Failed to retrieve input stream information");
            goto end;
        }

        printf("\n====================== %s ========================\n", in_filename[i]);
        av_dump_format(ifmt_ctxs[i], 0, in_filename[i], 0);
    }
    
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    ifmt_ctx = ifmt_ctxs[0];
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        /* will copy codec parameters and extradata */
        //if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
        //    fprintf(stderr, "Failed to copy codec param from input to output stream codec param\n");
        //}

        //if (avcodec_parameters_to_context(out_stream->codec, in_stream->codecpar) < 0) {
        if (avcodec_copy_context(out_stream->codec, in_stream->codec) < 0) {
            fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
            goto end;
        }

        out_stream->codec->codec_tag = 0;
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }
   
    for (i = 0; i < infile_num; i++) { 
        ifmt_ctx = ifmt_ctxs[i];
        while (1) {
            AVStream *in_stream, *out_stream;
        
            ret = av_read_frame(ifmt_ctx, &pkt);
            if (ret < 0)
                break;
        
            in_stream  = ifmt_ctx->streams[pkt.stream_index];
            out_stream = ofmt_ctx->streams[pkt.stream_index];
         
            if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
               pkt.pts = next_vpts;
               pkt.dts = next_vpts;
               next_vpts += pkt.duration;
            } else {
               pkt.pts = next_apts;
               pkt.dts = next_apts;
               next_apts += pkt.duration;
            }

            log_packet(ifmt_ctx, &pkt, "in");

            /* copy packet */
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            //log_packet(ofmt_ctx, &pkt, "out");
        
            ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
            if (ret < 0) {
                fprintf(stderr, "Error muxing packet\n");
                break;
            }
            av_packet_unref(&pkt);
        }
    }
    av_write_trailer(ofmt_ctx);
end:
    for (i = 0; i < infile_num; i++) {
        avformat_close_input(&ifmt_ctxs[i]);
    }

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}
