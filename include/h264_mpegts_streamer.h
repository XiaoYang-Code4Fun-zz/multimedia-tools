#ifndef MULTIMEDIA_TOOLS_INCLUDE_H264_VIDEO_MPEGTS_STREAMER_H_
#define MULTIMEDIA_TOOLS_INCLUDE_H264_VIDEO_MPEGTS_STREAMER_H_

#include "streaming_helper.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
};

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>

namespace multimedia_tools {

namespace {
void LogError(const std::string &msg) {
	std::cout << msg << std::endl;
}
constexpr int64_t kBitRate = 2097152; /* 2M */
}

/*
 * Stream h264 video in the format of mpeg-ts. The output can be file or url.
 * In fact, there is nothing particular restricted to h264 and mpeg-ts in this class. With some
 * minor changes to parameter, this class can be refactored for other codec and output format.
 */
class H264MpegtsStreamer {
public:
	H264MpegtsStreamer() = delete;
	H264MpegtsStreamer(const std::string &dst,
			int width, int height, int fps) : dst_(dst),
			height_(height), width_(width), fps_(fps),
			ofmt_ctx_(nullptr), ostream_(nullptr), encode_ctx_(nullptr),
			decode_ctx_(nullptr), decode_frame_(nullptr), es_header_sent_(false) {
	}
	~H264MpegtsStreamer() { Close(); }
	/*
	 * Opens output context. The output context is in mpeg-ts format. If the destination is a URL,
	 * this method also open network connection.
	 */
	bool Open();
	/*
	 * Initializes h264 codec. Used only when sending raw data or data need to be re-encoded. If the
	 * data is already encoded, there is no need to call this method. Separating out from the Open()
	 * method since not all platform have the necessary codec installed. If your data is already encoded,
	 * there is no need to initialize the codec.
	 */
	bool InitializeCodec();
	/*
	 * Register header for elementary stream. Used only during sending already encoded data.
	 */
	void RegisterESHeader(uint8_t *header, int size);
	/*
	 * Streams encoded data. Used when you have video already encoded in h264 - from your own code
	 * or hardware encoder.
	 */
	bool StreamEncodedData(uint8_t *data, int size, int64_t timestamp = 0);
	/*
	 * Stream raw frame. The method will take care of the encoding to h264.
	 */
	bool StreamFrame(AVFrame *frame);
	/*
	 * Re-encode data before streaming out. Used for testing. The class can also be refactored to use
	 * different codec for encoding and decoding.
	 */
	bool StreamReencodeData(uint8_t *data, int size, int64_t timestamp = 0);
	void Close();
private:
	AVStream *AddVideoStream();
	AVFrame *Decode(AVPacket *packet);
	void WrapEncodedBuffer(AVPacket *pkt, uint8_t *data, int size, int64_t timestamp = 0);
	std::string dst_;
	int height_, width_, fps_;
	AVFormatContext *ofmt_ctx_;
	AVStream *ostream_;
	AVCodecContext *encode_ctx_; // Used only when data to be sent are not encoded.
	AVCodecContext *decode_ctx_; // Used only when re-encoding data.
	AVPacket encode_packet_; // Packet used to buffer data during encoding.
	AVFrame *decode_frame_; // Frame used to buffer data during decoding.
	std::vector<uint8_t> es_header_;
	bool es_header_sent_;
};

bool H264MpegtsStreamer::Open() {
	av_register_all();
	avformat_network_init();
	avformat_alloc_output_context2(&ofmt_ctx_, NULL, "mpegts", dst_.c_str());
	if (!ofmt_ctx_) {
		LogError("Unable to create output context");
		return false;
	}

	ostream_ = AddVideoStream();
	if (!ostream_) {
		LogError("Unable to create output stream");
		return false;
	}

	// Open output URL
	if (!(ofmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&ofmt_ctx_->pb, dst_.c_str(), AVIO_FLAG_WRITE) < 0) {
			LogError("Cannot open URL for output");
			return false;
		}
	}

	av_dump_format(ofmt_ctx_, 0, dst_.c_str(), 1);

	if (avformat_write_header(ofmt_ctx_, NULL) < 0) {
		LogError("Failed to write header");
		return false;
	}
	return true;
}

void H264MpegtsStreamer::RegisterESHeader(uint8_t *header, int size) {
	for (int i = 0; i < size; i ++) {
		es_header_.push_back(header[i]);
	}
}

bool H264MpegtsStreamer::StreamEncodedData(uint8_t *data, int size, int64_t timestamp) {
	uint8_t *buffer = data;
	int buffer_size = size;
	bool buffer_copied = false;
	if (!es_header_sent_ && !es_header_.empty()) {
		buffer_size = es_header_.size() + size;
		buffer = new uint8_t[buffer_size];
		std::copy(es_header_.begin(), es_header_.end(), buffer);
		memcpy(buffer + es_header_.size(), data, size);
		buffer_copied = true;
	}
	AVPacket pkt;
	WrapEncodedBuffer(&pkt, buffer, buffer_size, timestamp);
	av_interleaved_write_frame(ofmt_ctx_, &pkt);
	if (buffer_copied) {
		delete buffer;
	}
	es_header_sent_ = true;
	av_free_packet(&pkt);
	return true;
}

bool H264MpegtsStreamer::StreamFrame(AVFrame *frame) {
	if (!encode_ctx_) {
		LogError("Must initialize codec");
		return false;
	}
	int ret = avcodec_send_frame(encode_ctx_, frame);
	if (ret < 0) {
		LogError("Error sending a frame for encoding\n");
		return false;
	}
	while (ret >= 0) {
		ret = avcodec_receive_packet(encode_ctx_, &encode_packet_);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			return true;
		}
		else if (ret < 0) {
			LogError("Error during encoding");
			return false;
		}
		av_interleaved_write_frame(ofmt_ctx_, &encode_packet_);
		av_packet_unref(&encode_packet_);
	}
	return true;
}


bool H264MpegtsStreamer::StreamReencodeData(uint8_t *data, int size, int64_t timestamp) {
	if (!decode_ctx_) {
		LogError("Must initialize codec");
		return false;
	}
	AVPacket pkt;
	WrapEncodedBuffer(&pkt, data, size, timestamp);
	int ret = avcodec_send_packet(decode_ctx_, &pkt);
	while (ret >= 0) {
		ret = avcodec_receive_frame(decode_ctx_, decode_frame_);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			return true;
		}
		if (!StreamFrame(decode_frame_)) {
			return false;
		}
	}
	return true;
}

AVStream* H264MpegtsStreamer::AddVideoStream() {
	AVStream *st = avformat_new_stream(ofmt_ctx_, NULL);
	st->codecpar->codec_id = AV_CODEC_ID_H264;
	st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	st->codecpar->bit_rate = kBitRate;
	st->codecpar->width = width_;
	st->codecpar->height = height_;
	st->codecpar->extradata_size = FF_INPUT_BUFFER_PADDING_SIZE;
	st->codecpar->extradata = new uint8_t[FF_INPUT_BUFFER_PADDING_SIZE];
	memset(st->codecpar->extradata, 0, FF_INPUT_BUFFER_PADDING_SIZE);
	st->time_base.den = fps_;
	st->time_base.num = 1;
	return st;
}

void H264MpegtsStreamer::WrapEncodedBuffer(AVPacket *pkt, uint8_t *data, int size, int64_t timestamp) {
	av_init_packet(pkt);
	pkt->flags |= (IsKeyFrame(data, size))?AV_PKT_FLAG_KEY:0;
	//pkt->flags |= (is_key_frame)?AV_PKT_FLAG_KEY:0;
	pkt->stream_index = ostream_->index;
	pkt->data = data;
	pkt->size = size;
	pkt->dts = AV_NOPTS_VALUE;
	//pkt->pts = AV_NOPTS_VALUE;
	pkt->pts = av_rescale_q(timestamp, (AVRational){1, 1000000}, ostream_->time_base);
}

bool H264MpegtsStreamer::InitializeCodec() {
	if (!encode_ctx_) {
		AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!codec) {
			LogError("Cannot find encoder");
			return false;
		}
		encode_ctx_= avcodec_alloc_context3(codec);
		if (!encode_ctx_) {
			LogError("Cannot create encoder context");
			return false;
		}
		encode_ctx_->codec_id = AV_CODEC_ID_H264;
		encode_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
		encode_ctx_->bit_rate = kBitRate;
		encode_ctx_->width = width_;
		encode_ctx_->height = height_;
		encode_ctx_->time_base.den = fps_;
		encode_ctx_->time_base.num = 1;
		encode_ctx_->gop_size = 10;
		encode_ctx_->max_b_frames = 2;
		encode_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
		if (ofmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
			encode_ctx_->flags |= CODEC_FLAG_GLOBAL_HEADER;
		}
		if (avcodec_open2(encode_ctx_, codec, NULL) < 0) {
			LogError("Unable to open encoder codec context");
			return false;
		}
		av_init_packet(&encode_packet_);
		encode_packet_.data = NULL;
		encode_packet_.size = 0;
	}
	if (!decode_ctx_) {
		AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		if (!codec) {
			LogError("Cannot find decoder");
			return false;
		}
		decode_ctx_ = avcodec_alloc_context3(codec);
		if (!decode_ctx_) {
			LogError("Cannot find decoder context");
			return false;
		}
		if (avcodec_open2(decode_ctx_, codec, NULL) < 0) {
			LogError("Could not open decoder codec context");
			return false;
		}
		decode_frame_ = av_frame_alloc();
	}
	return true;
}

void H264MpegtsStreamer::Close() {
	av_write_trailer(ofmt_ctx_);
	if (encode_ctx_) {
		avcodec_free_context(&encode_ctx_);
	}
	if (decode_ctx_) {
		avcodec_free_context(&decode_ctx_);
		av_frame_free(&decode_frame_);
	}
	if (ofmt_ctx_ && !(ofmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
		avio_close(ofmt_ctx_->pb);
	}
	avformat_free_context(ofmt_ctx_);
}

}

#endif // MULTIMEDIA_TOOLS_INCLUDE_H264_VIDEO_MPEGTS_STREAMER_H_
