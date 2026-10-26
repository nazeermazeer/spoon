extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <iostream>

int main() {
    std::cout << "FFmpeg Version: " << av_version_info() << std::endl;
    return 0;
}
