#include "h264_mpegts_streamer.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

bool stream_video(char* input_file, char* output) {
	av_register_all();
	AVFormatContext *pFormatCtx = NULL;
	if(avformat_open_input(&pFormatCtx, input_file, NULL, NULL) < 0) {
		perror("Cannot open file");
		return false;
	}
	if(avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		perror("Cannot find stream information");
		return false;
	}
	// Find the first video stream
	int videoStream = -1;
	for(int i=0; i<pFormatCtx->nb_streams; i++) {
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
			//std::cerr << "Found video stream " << i << std::endl;
			videoStream=i;
			break;
		}
	}
	if(videoStream==-1) {
		perror("Cannot find video stream in file");
		return false;
	}
	AVCodecContext *pCodecCtxOrig = NULL;
	AVCodecContext *pCodecCtx = NULL;
	// Get a pointer to the codec context for the video stream
	pCodecCtxOrig=pFormatCtx->streams[videoStream]->codec;
	AVCodec *pCodec = NULL;

	// Find the decoder for the video stream
	pCodec=avcodec_find_decoder(pCodecCtxOrig->codec_id);
	if(pCodec==NULL) {
	  fprintf(stderr, "Unsupported codec!\n");
	  return -1; // Codec not found
	}
	// Copy context
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if(avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
	  fprintf(stderr, "Couldn't copy codec context");
	  return -1; // Error copying codec context
	}
	// Open codec
	if(avcodec_open2(pCodecCtx, pCodec, NULL)<0)
	  return -1; // Could not open codec

	int frameFinished = 0;
	AVFrame *pFrame = av_frame_alloc();
	multimedia_tools::H264MpegtsStreamer streamer(output, pCodecCtx->width, pCodecCtx->height, 30);
	streamer.Open();
	streamer.InitializeCodec();
	AVPacket packet;
	while(av_read_frame(pFormatCtx, &packet)>=0) {
		// Is this a packet from the video stream?
		if(packet.stream_index==videoStream) {
			// Stream encoded packet.
			// streamer.StreamEncodedData(packet.data, packet.size, packet.pts);
			// Stream reencode packet.
			// streamer.StreamReencoddData(packet.data, packet.size, packet.pts);
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
			if(frameFinished) {
				// Stream raw frame.
				streamer.StreamFrame(pFrame);
			}
		}
		av_free_packet(&packet);
	}
	return true;
}

int main(int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "Usage: stream_test input_file output" << std::endl;
		return -1;
	}
	std::cout << "Streaming from file: " << argv[1] << std::endl;
	std::cout << "Streaming to: " << argv[2] << std::endl;
	stream_video(argv[1], argv[2]);
	return 0;
}
