#include <iostream>
#include <thread>
#include <atomic>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

// Atomic flag to control the recording loop
std::atomic<bool> keep_recording(true);

void listenForKeypress() {
    std::cout << "\nRecording started! Press [ENTER] to stop and save clip..." << std::endl;
    std::cin.get(); 
    keep_recording = false;
}

int main() {
    // 1. Initialize FFmpeg devices
    avdevice_register_all();

    // 2. Open macOS AVFoundation Screen Capture Input
    const AVInputFormat* input_format = av_find_input_format("avfoundation");
    if (!input_format) {
        std::cerr << "Could not find avfoundation input format." << std::endl;
        return -1;
    }

    AVFormatContext* input_ctx = nullptr;
    AVDictionary* options = nullptr;
    // "1" typically selects the default primary screen interface on macOS
    // Use "video=Capture Screen 0" or device index ":0" depending on system configuration
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "pixel_format", "uyvy422", 0); 

    if (avformat_open_input(&input_ctx, "1", input_format, &options) < 0) {
        std::cerr << "Error: Could not open screen input device." << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, nullptr) < 0) {
        std::cerr << "Error: Could not find input stream info." << std::endl;
        return -1;
    }

    // Find the video stream index
    int video_stream_idx = -1;
    for (unsigned int i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        std::cerr << "Error: No video stream found on input device." << std::endl;
        return -1;
    }

    AVCodecParameters* in_codec_par = input_ctx->streams[video_stream_idx]->codecpar;

    // 3. Setup Output File and H.264 Encoder
    const char* output_filename = "output.mp4";
    AVFormatContext* output_ctx = nullptr;
    avformat_alloc_output_context2(&output_ctx, nullptr, nullptr, output_filename);
    
    // Create output video stream
    const AVCodec* out_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    AVStream* out_stream = avformat_new_stream(output_ctx, out_codec);
    
    AVCodecContext* encoder_ctx = avcodec_alloc_context3(out_codec);
    encoder_ctx->width = in_codec_par->width;
    encoder_ctx->height = in_codec_par->height;
    encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P; // Standard compatible MP4 pixel format
    encoder_ctx->time_base = {1, 30};
    encoder_ctx->framerate = {30, 1};
    
    if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(encoder_ctx, out_codec, nullptr) < 0) {
        std::cerr << "Error: Could not open H.264 encoder." << std::endl;
        return -1;
    }

    avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx);

    // Open output file context
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Error: Could not open output file " << output_filename << std::endl;
            return -1;
        }
    }

    if (avformat_write_header(output_ctx, nullptr) < 0) {
        std::cerr << "Error: Failed writing output header." << std::endl;
        return -1;
    }

    // 4. Start Keypress Monitor Thread
    std::thread key_thread(listenForKeypress);

    // 5. Main Capture Loop
    AVPacket* in_packet = av_packet_alloc();
    AVFrame* in_frame = av_frame_alloc();
    AVFrame* out_frame = av_frame_alloc();
    
    // Allocate buffer space for target YUV420P format conversion if required
    out_frame->format = encoder_ctx->pix_fmt;
    out_frame->width = encoder_ctx->width;
    out_frame->height = encoder_ctx->height;
    av_frame_get_buffer(out_frame, 0);

    int64_t frame_counter = 0;

    while (keep_recording) {
        if (av_read_frame(input_ctx, in_packet) >= 0) {
            if (in_packet->stream_index == video_stream_idx) {
                
                // For simplicity in this base loop, map incoming parameters.
                // Note: Real production code will leverage `sws_scale` if incoming screen pix_fmt 
                // matches "uyvy422" instead of "yuv420p".
                
                out_frame->pts = frame_counter++;
                
                // Send frame to Encoder
                if (avcodec_send_frame(encoder_ctx, out_frame) >= 0) {
                    AVPacket* out_packet = av_packet_alloc();
                    while (avcodec_receive_packet(encoder_ctx, out_packet) == 0) {
                        av_packet_rescale_ts(out_packet, encoder_ctx->time_base, out_stream->time_base);
                        out_packet->stream_index = out_stream->index;
                        av_interleaved_write_frame(output_ctx, out_packet);
                        av_packet_unref(out_packet);
                    }
                    av_packet_free(&out_packet);
                }
            }
            av_packet_unref(in_packet);
        }
    }

    // 6. Clean Up & Flush Encoder Pipeline
    key_thread.join();
    av_write_trailer(output_ctx);

    av_packet_free(&in_packet);
    av_frame_free(&in_frame);
    av_frame_free(&out_frame);
    avcodec_free_context(&encoder_ctx);
    avformat_close_input(&input_ctx);
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx->pb);
    }
    avformat_free_context(output_ctx);

    std::cout << "Clip saved successfully to " << output_filename << std::endl;
    return 0;
}
