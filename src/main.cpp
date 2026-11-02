#include <iostream>
#include <unistd.h>
#include <Carbon/Carbon.h>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

int main() {
    avdevice_register_all();

    const AVInputFormat* input_format = av_find_input_format("avfoundation");

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "60", 0);
    av_dict_set(&options, "pixel_format", "uyvy422", 0);
    av_dict_set(&options, "capture_cursor", "1", 0); 

    AVFormatContext* format_context = avformat_alloc_context();
    avformat_open_input(&format_context, "4", input_format, &options);
    av_dict_free(&options);

    avformat_find_stream_info(format_context, &options);
    av_dump_format(format_context, 0, "4", 0);

    // avformat_alloc_output_context2(&context, NULL, NULL, "output.mp4");
    AVPacket* packet = av_packet_alloc();
    int stream_idx = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVStream* stream = format_context->streams[stream_idx];

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);

    codec_context->width = 1920;
    codec_context->height = 1080;
    codec_context->bit_rate = 4000000;
    codec_context->time_base = AVRational{1, 30};
    codec_context->framerate = AVRational{30, 1};
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;  
    codec_context->gop_size = 12;
    codec_context->max_b_frames = 2; 

    avcodec_open2(codec_context, codec, &options);

    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == stream_idx) {
            std::cout << "Read packet with size: " << packet->size << "\n";

            AVFrame* frame = av_frame_alloc();
            
            int response = avcodec_send_packet(codec_context, packet);
            while (response >= 0) {
                response = avcodec_receive_frame(codec_context, frame);
                av_frame_unref(frame);

                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    std::cout << "End of stream\n";
                    break;
                } else if (response < 0) {
                    std::cout << "Decoding error: " << response << "\n";
                    break;
                }
            }

            av_frame_free(&frame);
        }

        av_packet_unref(packet);
    }

    // char error_buf[AV_ERROR_MAX_STRING_SIZE];
    // av_make_error_string(error_buf, AV_ERROR_MAX_STRING_SIZE, av_read_frame(context, packet));
    // std::cout << "error: " << error_buf << "\n";

    avformat_close_input(&format_context);
    avformat_free_context(format_context);
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
