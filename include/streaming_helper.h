#ifndef MULTIMEDIA_TOOLS_INCLUDE_STREAMING_HELPER_H_
#define MULTIMEDIA_TOOLS_INCLUDE_STREAMING_HELPER_H_

#include <stdint.h>
#include <memory>
#include <vector>

namespace {
constexpr int kMaxPESPacketLen = 65535;
}

enum MediaType {
	AUDIO = 0xC0,
	VIDEO = 0xE0
};

std::vector<uint8_t> CreatePESHeader(MediaType type, int data_size, int *size_included, int64_t pts = -1) {
	int header_size = (pts == -1) ? 9 : 14;
	int header_extension_size = header_size - 6;
	std::vector<uint8_t> header(header_size);
	header[0] = header[1] = 0x00;
	header[2] = 0x01;
	header[3] = type & 0xFF;
	int total_data_size = data_size + header_extension_size;
	if (total_data_size > kMaxPESPacketLen) {
		total_data_size = kMaxPESPacketLen;
	}
	*size_included = total_data_size - header_extension_size;
	header[4] = total_data_size >> 8;
	header[5] = total_data_size & 0xFF;

	header[6] = 0x84;
	if (pts == -1) {
		header[7] = 0x00;
		header[8] = 0x00;
	} else {
		header[7] = 0x80;
		header[8] = 5;
		header[9] = 0x21 | (((pts >> 30) & 7) << 1);
		header[10] = (pts >> 22) & 0xFF;
		header[11] = 1 | (((pts >> 15) & 0x7F) << 1);
		header[12] = (pts >> 7) & 0xFF;
		header[13] = 1 | (((pts >>0)  & 0x7F) << 1);
	}
	return header;
}

bool IsKeyFrame( const void *p, int len ) {
	if ( !p || 6 >= len ) {
		return -1;
	}
	unsigned char *b = (unsigned char*)p;
	// Verify NAL marker
	if ( b[ 0 ] || b[ 1 ] || 0x01 != b[ 2 ] ) {
		b++;
		if ( b[ 0 ] || b[ 1 ] || 0x01 != b[ 2 ] ) {
			return false;
		}
	}
	b += 3;
	// Verify VOP id
	if ( 0xb6 == *b ) {
		b++;
		return ( *b & 0xc0 ) >> 6 == 0;
	}
	if (*b == 0x65) {
		return true;
	}
	return false;
}

#endif // MULTIMEDIA_TOOLS_INCLUDE_STREAMING_HELPER_H_
