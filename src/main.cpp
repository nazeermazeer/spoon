#include <iostream>
#include <unistd.h>
#include <Carbon/Carbon.h>

extern "C" {
#include <libavdevice/avdevice.h>
}

int main() {
    avdevice_register_all();

    const AVInputFormat* input_format = av_find_input_format("avfoundation");

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "60", 0);
    av_dict_set(&options, "pixel_format", "uyvy422", 0);
    av_dict_set(&options, "capture_cursor", "1", 0); 

    AVFormatContext* context = avformat_alloc_context();
    avformat_open_input(&context, "4", input_format, &options);
    av_dict_free(&options);

    avformat_find_stream_info(context, &options);
    av_dump_format(context, 0, "4", 0);

    // avformat_alloc_output_context2(&context, NULL, NULL, "output.mp4");
    AVStream *stream = avformat_new_stream(context, NULL);
    AVPacket* packet = av_packet_alloc();
    int stream_idx = av_find_best_stream(context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    while (av_read_frame(context, packet) >= 0) {
        if (packet->stream_index == stream_idx) {
            std::cout << "Read packet with size: " << packet->size << "\n";
        }

        av_packet_unref(packet);
    }

    avformat_close_input(&context);
    avformat_free_context(context);
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
