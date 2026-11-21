#include <iostream>
#include <unistd.h>
#include <Carbon/Carbon.h>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

std::string ffmpeg_error_string(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

int main() {
    avdevice_register_all();
    bool quit = false;
    int ret;

    const AVInputFormat* in_format = av_find_input_format("avfoundation");
    if (!in_format) {
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

    
    AVFormatContext* format_in_context = nullptr;
    ret = avformat_open_input(&format_in_context, "4", in_format, &options);
    if (ret < 0) {
        av_dict_free(&options);
        avformat_free_context(format_in_context);
        std::cerr << "opening in device failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }
    av_dict_free(&options);

    ret = avformat_find_stream_info(format_in_context, &options);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        std::cerr << "could not read stream info\n";
        return 1;
    }
    av_dump_format(format_in_context, 0, "4", 0);

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        std::cout << "writing video to directory; " << cwd << "\n";
    }

    AVFormatContext* format_output_context = nullptr;    
    ret = avformat_alloc_output_context2(&format_output_context, NULL, NULL, "output.mp4");
    if (ret < 0) {
        av_dict_free(&options);
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        std::cerr << "opening output device failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    int in_video_stream_idx = av_find_best_stream(format_in_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (in_video_stream_idx < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);

        std::cerr << "screen capture video stream could not be found; " << ffmpeg_error_string(in_video_stream_idx) << "\n";
        return -1;
    }

    int in_audio_stream_idx = av_find_best_stream(format_in_context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (in_audio_stream_idx < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);

        std::cerr << "screen capture audio stream could not be found; " << ffmpeg_error_string(in_video_stream_idx) << "\n";
        return -1;
    }

    AVStream* in_video_stream = format_in_context->streams[in_video_stream_idx];
    if (!in_video_stream) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);

        std::cerr << "in screen capture video stream could not be opened\n";
        return -1;
    }

    AVStream* in_audio_stream = format_in_context->streams[in_audio_stream_idx];
    if (!in_audio_stream) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);

        std::cerr << "in screen capture audio stream could not be opened\n";
        return -1;
    }

    AVStream* output_video_stream = avformat_new_stream(format_output_context, nullptr);
    if (!output_video_stream) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);

        std::cerr << "output screen capture stream could not be opened\n";
        return -1;
    }


    const AVCodec* in_codec = avcodec_find_decoder(in_video_stream->codecpar->codec_id);
    if (!in_codec) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        std::cerr << "could not find in decoder\n";
        return -1;
    }

    const AVCodec* output_codec = avcodec_find_encoder_by_name("libx264");
    if (!output_codec) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        std::cerr << "could not find output decoder\n";
        return -1;
    }

    AVCodecContext* in_codec_context = avcodec_alloc_context3(in_codec);
    if (!in_codec_context) {
        avformat_free_context(format_in_context);
        avcodec_free_context(&in_codec_context);
        avformat_free_context(format_output_context);
        std::cerr << "in codec retrival failed\n";
        return -1;
    }

    AVCodecContext* output_codec_context = avcodec_alloc_context3(output_codec);
    if (!output_codec_context) {
        avformat_free_context(format_in_context);
        avcodec_free_context(&in_codec_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "output codec retrival failed\n";
        return -1;
    }

    in_codec_context->width = 1920;
    in_codec_context->height = 1080;
    in_codec_context->bit_rate = 4000000;
    in_codec_context->time_base = AVRational{1, 60};
    in_codec_context->framerate = AVRational{60, 1};
    in_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;  
    in_codec_context->gop_size = 12;
    in_codec_context->max_b_frames = 2;
    in_codec_context->pkt_timebase = in_video_stream->time_base;

    output_codec_context->width = in_video_stream->codecpar->width;
    output_codec_context->height = in_video_stream->codecpar->height;
    output_codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    output_codec_context->time_base = AVRational{1, 60};
    output_codec_context->framerate = AVRational{60, 1};
    output_codec_context->bit_rate = 4'000'000;
    output_codec_context->gop_size = 12;

    ret = av_opt_set(output_codec_context->priv_data, "preset", "medium", 0);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "medium preset configuration failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    ret = av_opt_set(output_codec_context->priv_data, "crf", "18", 0);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "crf configuration failed; " << ffmpeg_error_string(ret) << "\n";
        return 1;
    }

    // produce global headers if required
    if (format_output_context->oformat->flags & AVFMT_GLOBALHEADER) {
        output_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_parameters_to_context(in_codec_context, in_video_stream->codecpar);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "in parameters failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avcodec_open2(in_codec_context, in_codec, &options);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "in context opening failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avcodec_open2(output_codec_context, output_codec, &options);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "output context opening failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avcodec_parameters_from_context(output_video_stream->codecpar, output_codec_context);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avcodec_free_context(&in_codec_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "could not set output codec parameters" << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    ret = avio_open(&format_output_context->pb, "output.mp4", AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "output file open failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    output_video_stream->time_base = output_codec_context->time_base;
    ret = avformat_write_header(format_output_context, &options);
    if (ret < 0) {
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&in_codec_context);
        avcodec_free_context(&output_codec_context);
        std::cerr << "header write failed; " << ffmpeg_error_string(ret) << "\n";
        return -1;
    }

    std::cout << "header write succeeded\n";


    AVPacket* in_packet = av_packet_alloc();
    if (!in_packet) {
        av_packet_free(&in_packet);
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        std::cerr << "packet allocation failed\n";
        return -1;
    }

    AVPacket* output_packet = av_packet_alloc();
    if (!output_packet) {
        av_packet_free(&in_packet);
        av_packet_free(&output_packet);
        avformat_free_context(format_in_context);
        avformat_free_context(format_output_context);
        std::cerr << "packet allocation failed\n";
        return -1;
    }

    AVFrame* in_frame = av_frame_alloc();
    AVFrame* output_frame = av_frame_alloc();
    output_frame->format = output_codec_context->pix_fmt;
    output_frame->width = output_codec_context->width;
    output_frame->height = output_codec_context->height;
    av_frame_get_buffer(output_frame, 32);

    SwsContext* sws_context = sws_getContext(
        in_codec_context->width, in_codec_context->height, in_codec_context->pix_fmt,
        output_codec_context->width, output_codec_context->height, output_codec_context->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    std::cout << "Press C to clip, or Q to quit...\n";

    while (true) {  
        // macOS Virtual Key Code 12 = Q
        if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 12) && !quit) {
            std::cout << "Quitting...\n";
            quit = true;
        }

        ret = av_read_frame(format_in_context, in_packet);
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
        if (in_packet->stream_index == in_video_stream_idx) {
            ret = avcodec_send_packet(in_codec_context, in_packet);
            av_packet_unref(in_packet);
            if (ret < 0) {
                std::cerr << "frame sending failed; " << ffmpeg_error_string(ret) << "\n";
                break;
            }

            if (quit) {
                ret = avcodec_send_frame(output_codec_context, nullptr);
                if (ret == AVERROR_EOF) {
                    av_frame_free(&in_frame);
                    av_packet_unref(in_packet);
                    break;
                } else if (ret < 0) {
                    av_frame_free(&in_frame);
                    av_packet_unref(in_packet);
                    std::cerr << "frame flush failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }
            }

            while (true) {
                if (!quit) {
                    ret = avcodec_receive_frame(in_codec_context, in_frame);
                    if (ret == AVERROR(EAGAIN)) {
                        break;
                    }
                    if (ret < 0) {
                        av_frame_free(&in_frame);
                        av_packet_unref(in_packet);
                        std::cerr << "frame recieve failed; " << ffmpeg_error_string(ret) << "\n";
                        break;
                    }

                    ret = sws_scale(
                        sws_context, in_frame->data, in_frame->linesize, 0, 
                        in_frame->height, output_frame->data, output_frame->linesize
                    );
                    if (ret < 0) {
                        av_frame_free(&in_frame);
                        av_packet_unref(in_packet);
                        std::cerr << "frame conversion failed; " << ffmpeg_error_string(ret) << "\n";
                        break;
                    }

                    static int64_t first_pts = AV_NOPTS_VALUE;
                    if (in_frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                        if (first_pts == AV_NOPTS_VALUE) {
                            first_pts = in_frame->best_effort_timestamp;
                        }
                        output_frame->pts = av_rescale_q(in_frame->best_effort_timestamp - first_pts, in_video_stream->time_base, output_codec_context->time_base);
                    }

                    ret = avcodec_send_frame(output_codec_context, output_frame);
                    if (ret < 0) {
                        av_frame_free(&in_frame);
                        av_packet_unref(in_packet);
                        std::cerr << "frame send failed; " << ffmpeg_error_string(ret) << "\n";
                        break;
                    }
                }

                ret = avcodec_receive_packet(output_codec_context, output_packet);
                if (ret == AVERROR(EAGAIN)) {
                    av_packet_unref(in_packet);
                    break;
                } else if (ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_frame_free(&in_frame);
                    av_packet_unref(in_packet);
                    std::cerr << "packet recieve failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                output_packet->stream_index=output_video_stream->index;
                av_packet_rescale_ts(output_packet, output_codec_context->time_base, output_video_stream->time_base);

                ret = av_interleaved_write_frame(format_output_context, output_packet);
                if (ret < 0) {
                    av_frame_free(&in_frame);
                    av_packet_unref(in_packet);
                    std::cerr << "write frame failed; " << ffmpeg_error_string(ret) << "\n";
                    break;
                }

                av_packet_unref(in_packet);
            }
        }

        av_packet_unref(in_packet);
    }

    ret = av_interleaved_write_frame(format_output_context, nullptr);
    if (ret < 0) {
        std::cerr << "flush write frame failed; " << ffmpeg_error_string(ret) << "\n";
    }

    ret = av_write_trailer(format_output_context);
    if (ret < 0) {
        std::cerr << "trailer write failed; " << ffmpeg_error_string(ret) << "\n";
    }

    avformat_close_input(&format_in_context);
    avformat_free_context(format_in_context);
    avformat_free_context(format_output_context);
    avcodec_free_context(&in_codec_context);
    avcodec_free_context(&output_codec_context);
    av_packet_free(&in_packet);
    av_packet_free(&output_packet);
    av_frame_free(&in_frame);

    return 0;
}