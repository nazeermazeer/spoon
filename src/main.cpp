#include <iostream>
#include <unistd.h>
#include <Carbon/Carbon.h>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
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
        std::cerr << "avfoundation unavaliable";
        return 1;
    }

    AVDictionary* options = nullptr;

    ret = av_dict_set(&options, "framerate", "60", 0);
    if (ret < 0) {
        av_dict_free(&options);
        std::cerr << "framerate configuration failed; " << ffmpeg_error_string(ret);
        return 1;
    }

    ret = av_dict_set(&options, "pixel_format", "uyvy422", 0);
    if (ret < 0) {
        av_dict_free(&options);
        std::cerr << "pixel format configuration failed; " << ffmpeg_error_string(ret);
        return 1;
    }

    ret = av_dict_set(&options, "capture_cursor", "1", 0); 
    if (ret < 0) {
        av_dict_free(&options);
        std::cerr << "mouse cursor capture configuration failed; " << ffmpeg_error_string(ret);
        return 1;
    }

    
    AVFormatContext* format_input_context = nullptr;
    ret = avformat_open_input(&format_input_context, "4", input_format, &options);
    if (ret < 0) {
        av_dict_free(&options);
        avformat_free_context(format_input_context);
        std::cerr << "opening input device failed; " << ffmpeg_error_string(ret);
        return 1;
    }
    av_dict_free(&options);

    ret = avformat_find_stream_info(format_input_context, &options);
    if (ret < 0) {
        avformat_free_context(format_input_context);
        std::cerr << "could not read stream info";
        return 1;
    }
    av_dump_format(format_input_context, 0, "4", 0);


    AVFormatContext* format_output_context = nullptr;    
    ret = avformat_alloc_output_context2(&format_output_context, NULL, NULL, "output.mp4");
    if (ret < 0) {
        av_dict_free(&options);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "opening output device failed; " << ffmpeg_error_string(ret);
        return 1;
    }


    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "packet allocation failed";
        return -1;
    }

    int input_stream_idx = av_find_best_stream(format_input_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (input_stream_idx < 0) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);

        std::cerr << "screen capture stream could not be found; " << ffmpeg_error_string(input_stream_idx);
        return -1;
    }

    AVStream* input_stream = format_input_context->streams[input_stream_idx];
    if (!input_stream) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);

        std::cerr << "input screen capture stream could not be opened";
        return -1;
    }

    AVStream* output_stream = avformat_new_stream(format_output_context, nullptr);
    if (!output_stream) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);

        std::cerr << "output screen capture stream could not be opened";
        return -1;
    }


    const AVCodec* codec = avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!codec) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        std::cerr << "could not find decoder";
        return -1;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avcodec_free_context(&codec_context);
        avformat_free_context(format_output_context);
        std::cerr << "codec retrival failed";
        return -1;
    }

    codec_context->width = 1920;
    codec_context->height = 1080;
    codec_context->bit_rate = 4000000;
    codec_context->time_base = AVRational{1, 30};
    codec_context->framerate = AVRational{30, 1};
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;  
    codec_context->gop_size = 12;
    codec_context->max_b_frames = 2; 

    ret = avcodec_open2(codec_context, codec, &options);
    if (ret < 0) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&codec_context);
        std::cerr << "context opening failed";
        return -1;
    }

    ret = avformat_write_header(format_input_context, &options);
    if (ret < 0) {
        av_packet_free(&packet);
        avformat_free_context(format_input_context);
        avformat_free_context(format_output_context);
        avcodec_free_context(&codec_context);
        std::cerr << "header write failed; " << ffmpeg_error_string(ret);
        return -1;
    }

    
    while (true) {
        ret = av_read_frame(format_input_context, packet);

        if (ret == AVERROR_EOF) {
            std::cout << "reached end of file\n";
            break;
        }

        if (ret < 0) {
            std::cerr << "frame reading failed; " << ffmpeg_error_string(ret) << "\n";
            break;
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            av_frame_free(&frame);
            std::cerr << "failed to read packet with size " << packet->size << "\n";
            break;
        } else {
            std::cout << "read packet with size: " << packet->size << "\n";
        }

        if (packet->stream_index == input_stream_idx) {
            ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                av_frame_free(&frame);
                std::cerr << "packet sending failed with error code " << ret << "\n";
                break;
            }

            while (true) {
                ret = avcodec_receive_frame(codec_context, frame);
                av_frame_unref(frame);

                av_packet_unref(packet);
                av_interleaved_write_frame(format_input_context, packet);

                if (ret == AVERROR_EOF) {
                    std::cout << "end of stream\n";
                    break;
                } else if (ret == AVERROR(EAGAIN)) {
                    std::cout << "error in stream\n";
                    break;
                } else if (ret < 0) {
                    std::cout << "decoding error: " << ffmpeg_error_string(ret) << "\n";
                    break;
                }
            }
        }

        av_packet_unref(packet);
        av_frame_free(&frame);
    }

    // char error_buf[AV_ERROR_MAX_STRING_SIZE];
    // av_make_error_string(error_buf, AV_ERROR_MAX_STRING_SIZE, av_read_frame(context, packet));
    // std::cout << "error: " << error_buf << "\n";

    av_write_trailer(format_input_context);

    avformat_close_input(&format_input_context);
    avformat_free_context(format_input_context);
    avformat_free_context(format_output_context);
    avcodec_free_context(&codec_context);
    av_packet_free(&packet);

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