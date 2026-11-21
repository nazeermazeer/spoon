#include <iostream>
#include <unistd.h>
#include <Carbon/Carbon.h>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

std::string ffmpeg_error_string(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

int main() {
    avdevice_register_all();
    int ret;

    const AVInputFormat* input_format = av_find_input_format("avfoundation");
    if (!input_format) {
        std::cerr << "avfoundation unavaliable\n";
        return 1;
    }

    AVDictionary* options = nullptr;

    ret = av_dict_set(&options, "framerate", "60", 0);
    if (ret < 0) {
        av_dict_free(&options);
        std::cerr << "framerate configuration failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    ret = av_dict_set(&options, "pixel_format", "uyvy422", 0);
    if (ret < 0) {
        av_dict_free(&options);
        std::cerr << "pixel format configuration failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    ret = av_dict_set(&options, "capture_cursor", "1", 0); 
    if (ret < 0) {
        av_dict_free(&options);
        std::cerr << "mouse cursor capture configuration failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    
    AVFormatContext* format_input_context = nullptr;
    ret = avformat_open_input(&format_input_context, "4", input_format, &options);
    if (ret < 0) {
        av_dict_free(&options);
        avformat_free_context(format_input_context);
        std::cerr << "opening input device failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }
    av_dict_free(&options);

    ret = avformat_find_stream_info(format_input_context, &options);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        std::cerr << "could not read stream info\n";
        return 1;
    }
    av_dump_format(format_input_context, 0, "4", 0);


    AVFormatContext* format_output_context = nullptr;    
    ret = avformat_alloc_output_context2(&format_output_context, NULL, NULL, "output.mp4");
    if (ret < 0) {
        av_dict_free(&options);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "opening output device failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    int input_stream_idx = av_find_best_stream(format_input_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (input_stream_idx < 0) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);

        std::cerr << "screen capture stream could not be found; " << ffmpeg_error_string(input_stream_idx) << "\n";
        return -1;
    }

    AVStream* input_stream = format_input_context->streams[input_stream_idx];
    if (!input_stream) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);

        std::cerr << "input screen capture stream could not be opened\n";
        return -1;
    }

    AVStream* output_stream = avformat_new_stream(format_output_context, nullptr);
    if (!output_stream) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);

        std::cerr << "output screen capture stream could not be opened\n";
        return -1;
    }


    const AVCodec* input_codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!input_codec) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "could not find input decoder\n";
        return -1;
    }

    const AVCodec* output_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!output_codec) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "could not find output decoder\n";
        return -1;
    }

    AVCodecContext* input_codec_context = avcodec_alloc_context3(input_codec);
    if (!input_codec_context) {
        avformat_free_context(format_input_context);
        avcodec_free_context(&input_codec_context);
        avformat_free_context(format_output_context);
        std::cerr << "input codec retrival failed\n";
        return -1;
    }

    AVCodecContext* output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        avformat_free_context(format_input_context);
        avcodec_free_context(&input_codec_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "output codec retrival failed\n";
        return -1;
    }

    input_codec_context->width = 1920;
    input_codec_context->height = 1080;
    input_codec_context->bit_rate = 4000000;
    input_codec_context->time_base = AVRational{1, 30};
    input_codec_context->framerate = AVRational{30, 1};
    input_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;  
    input_codec_context->gop_size = 12;
    input_codec_context->max_b_frames = 2; 

    output_codec_context->width = input_stream->codecpar->width;
    output_codec_context->height = input_stream->codecpar->height;
    output_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    output_codec_context->time_base = AVRational{1, 60};
    output_codec_context->framerate = AVRational{60, 1};
    output_codec_context->bit_rate = 4'000'000;
    output_codec_context->gop_size = 12;

    // produce global headers if required
    if (format_output_context->oformat->flags & AVFMT_GLOBALHEADER) {
        output_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_parameters_to_context(input_codec_context, input_stream->codecpar);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "input parameters failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avcodec_open2(input_codec_context, input_codec, &options);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "input context opening failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avcodec_open2(output_codec_context, output_codec, &options);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "output context opening failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avcodec_parameters_from_context(output_stream->codecpar, output_codec_context);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        avcodec_free_context(&input_codec_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "could not set output codec parameters" << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avio_open(&format_output_context->pb, "output.mp4", AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "output file open failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    output_stream->time_base = output_codec_context->time_base;
    ret = avformat_write_header(format_output_context, &options);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&input_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "header write failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    std::cout << "header write succeeded\n";


    AVPacket* input_packet = av_packet_alloc();
    if (!input_packet) {
        av_packet_free(&input_packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "packet allocation failed\n";
        return -1;
    }

    AVPacket* output_packet = av_packet_alloc();
    if (!output_packet) {
        av_packet_free(&input_packet);
        av_packet_free(&output_packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "packet allocation failed\n";
        return -1;
    }

    AVFrame* input_frame = av_frame_alloc();
    AVFrame* output_frame = av_frame_alloc();
    output_frame->format = output_codec_context->pix_fmt;
    output_frame->width = output_codec_context->width;
    output_frame->height = output_codec_context->height;
    av_frame_get_buffer(output_frame, 32);

    SwsContext* sws_context = sws_getContext(
        input_codec_context->width, input_codec_context->height, input_codec_context->pix_fmt,
        output_codec_context->width, output_codec_context->height, output_codec_context->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    while (true) {  
        // macOS Virtual Key Code 12 = Q
        if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 12)) {
            std::cout << "Quitting...\n";
            break;
        }

        ret = av_read_frame(format_input_context, input_packet);
        if (ret == AVERROR(EAGAIN)) {
            usleep(1000);
            continue;
        }
        if (ret == AVERROR_EOF) {
            std::cout << "reached end of file\n";
            break;
        }
        if (ret < 0) {
            std::cerr << "frame reading failed; " << ffmpeg_error_string(ret) << "\n";
            break;
        }

        // make sure we are copying the correct video stream and not something like captions
        if (input_packet->stream_index == input_stream_idx) {
            ret = avcodec_send_packet(input_codec_context, input_packet);
            av_packet_unref(input_packet);
            if (ret < 0) {
                std::cerr << "frame sending failed; " << ffmpeg_error_string(ret) << "\n";
                break;
            }

            while (true) {
                ret = avcodec_receive_frame(input_codec_context, input_frame);
                if (ret == AVERROR(EAGAIN)) {
                    break;
                }
                if (ret < 0) {
                    av_frame_free(&input_frame);
                    av_packet_unref(input_packet);
                    std::cerr << "frame recieve failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                ret = sws_scale(
                    sws_context, input_frame->data, input_frame->linesize, 0, 
                    input_frame->height, output_frame->data, output_frame->linesize
                );
                if (ret < 0) {
                    av_frame_free(&input_frame);
                    av_packet_unref(input_packet);
                    std::cerr << "frame conversion failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                static int64_t first_pts = AV_NOPTS_VALUE;
                if (input_frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                    if (first_pts == AV_NOPTS_VALUE) {
                        first_pts = input_frame->best_effort_timestamp;
                    }
                    output_frame->pts = av_rescale_q(input_frame->best_effort_timestamp - first_pts, input_stream->time_base, output_codec_context->time_base);
                }

                ret = avcodec_send_frame(output_codec_context, output_frame);
                if (ret < 0) {
                    av_frame_free(&input_frame);
                    av_packet_unref(input_packet);
                    std::cerr << "frame send failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                ret = avcodec_receive_packet(output_codec_context, output_packet);
                if (ret == AVERROR(EAGAIN)) {
                    av_packet_unref(input_packet);
                    break;
                }
                if (ret < 0) {
                    av_frame_free(&input_frame);
                    av_packet_unref(input_packet);
                    std::cerr << "packet recieve failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                output_packet->stream_index=output_stream->index;
                av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_stream->time_base);

                ret = av_interleaved_write_frame(format_output_context, output_packet);
                if (ret < 0) {
                    av_frame_free(&input_frame);
                    av_packet_unref(input_packet);
                    std::cerr << "write frame failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                av_packet_unref(input_packet);
            }
        }

        av_packet_unref(input_packet);
    }

    ret = av_write_trailer(format_output_context);
    if (ret < 0) {
        std::cerr << "trailer write failed; " << ffmpeg_error_string(ret) << "\n";
    }

    avformat_close_input(&format_input_context);
    avformat_free_context(format_input_context);
    avformat_free_context(format_output_context);
    avcodec_free_context(&input_codec_context);
    avcodec_free_context(&output_codec_context);
    av_packet_free(&input_packet);
    av_packet_free(&output_packet);
    av_frame_free(&input_frame);

    std::cout << "Press C to clip, or Q to quit...\n";

    while (true) {
        // macOS Virtual Key Code 12 = Q
        if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 12)) {
            std::cout << "Quitting...\n";
            break;
        }

        // macOS Virtual Key Code 8 = C
        if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 8)) {
            std::cout << "Clipped!\n";
            usleep(200000);
        }

        usleep(10000);
    }

    return 0;
}