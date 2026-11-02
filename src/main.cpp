#include <iostream>
#include <unistd.h>
#include <Carbon/Carbon.h>

extern "C" {
#include <libavdevice/avdevice.h>
}

int main() {
    std::cout << "Press C to clip, or Q to quit...\n";

    avdevice_register_all();

    const AVInputFormat* input_format = av_find_input_format("avfoundation");

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "60", 0);
    av_dict_set(&options, "pixel_format", "uyvy422", 0);
    av_dict_set(&options, "capture_cursor", "1", 0); 

    AVFormatContext* context = avformat_alloc_context();
    avformat_open_input(&context, "1", input_format, &options);

    while (true) {
        // macOS Virtual Key Code 12 = Q
        if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 12)) {
            break;
        }

        // macOS Virtual Key Code 8 = C
        if (CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 8)) {
            std::cout << "Clipped!\n";
            usleep(200000);
        }

        usleep(10000);
    }
    avformat_close_input(&context);
    return 0;
}
