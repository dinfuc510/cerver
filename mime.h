#ifndef MIME_H
#define MIME_H

#include <assert.h>
#include <stdbool.h>
#include "cer_ds/slice.h"

#define MAX_PLAIN_TEXT_LEN 10240
#define FF "\xff"
#define DF "\xdf"
#define ZZ "\x00"
#define EMPTY_PATTERN slice_bytes("")

bool is_match_mime_pattern(Slice input, Slice pattern, Slice mask, Slice ignored) {
	assert(pattern.len == mask.len);
	if (input.len < pattern.len) {
		return false;
	}

	size_t s = 0;
	while (s < input.len) {
		if (strchr(ignored.ptr, input.ptr[s]) == NULL) {
			break;
		}
		s += 1;
	}

	size_t p = 0;
	while (p < pattern.len) {
		if ((input.ptr[s] & mask.ptr[p]) != pattern.ptr[p]) {
			return false;
		}
		s += 1;
		p += 1;
	}

	return true;
}

const char *is_html_xml_pdf(Slice input) {
	if (is_match_mime_pattern(input, slice_bytes("\x3C\x3F\x78\x6D\x6C"), slice_bytes(FF FF FF FF FF), EMPTY_PATTERN)) {
		return "application/pdf";
	}

	size_t leading_ws_cnt = strspn(input.ptr, "\x09\x0a\x0c\x0d\x20");
	input = slice_advanced(input, leading_ws_cnt);
	if (is_match_mime_pattern(input, slice_bytes("\x3C\x3F\x78\x6D\x6C"), slice_bytes(FF FF FF FF FF), EMPTY_PATTERN)) {
		return "text/xml";
	}

	static const Slice signatures[][2] = {
		{ slice_bytes("\x3C\x21\x44\x4F\x43\x54\x59\x50\x45\x20\x48\x54\x4D\x4C"), 	slice_bytes(FF FF DF DF DF DF DF DF DF FF DF DF DF DF)},
		{ slice_bytes("\x3C\x48\x54\x4D\x4C"), 										slice_bytes(FF DF DF DF DF) 						},
		{ slice_bytes("\x3C\x48\x45\x41\x44"), 										slice_bytes(FF DF DF DF DF) 						},
		{ slice_bytes("\x3C\x53\x43\x52\x49\x50\x54"), 								slice_bytes(FF DF DF DF DF DF DF) 					},
		{ slice_bytes("\x3C\x49\x46\x52\x41\x4D\x45"), 								slice_bytes(FF DF DF DF DF DF DF) 					},
		{ slice_bytes("\x3C\x48\x31"), 												slice_bytes(FF DF FF) 								},
		{ slice_bytes("\x3C\x44\x49\x56"), 											slice_bytes(FF DF DF DF) 							},
		{ slice_bytes("\x3C\x46\x4F\x4E\x54"), 										slice_bytes(FF DF DF DF DF) 						},
		{ slice_bytes("\x3C\x54\x41\x42\x4C\x45"), 									slice_bytes(FF DF DF DF DF DF) 						},
		{ slice_bytes("\x3C\x41"), 													slice_bytes(FF DF)									},
		{ slice_bytes("\x3C\x53\x54\x59\x4C\x45"), 									slice_bytes(FF DF DF DF DF DF)						},
		{ slice_bytes("\x3C\x54\x49\x54\x4C\x45"), 									slice_bytes(FF DF DF DF DF DF) 						},
		{ slice_bytes("\x3C\x42"), 													slice_bytes(FF DF) 									},
		{ slice_bytes("\x3C\x42\x4F\x44\x59"), 										slice_bytes(FF DF DF DF DF) 						},
		{ slice_bytes("\x3C\x42\x52"), 												slice_bytes(FF DF DF) 								},
		{ slice_bytes("\x3C\x50"), 													slice_bytes(FF DF) 									},
		{ slice_bytes("\x3C\x21\x2D\x2D"), 											slice_bytes(FF FF FF FF) 							},
	};

	for (size_t i = 0; i < sizeof(signatures)/sizeof(signatures[0]); i++) {
		if (is_match_mime_pattern(input, signatures[i][0], signatures[i][1], EMPTY_PATTERN)) {
			Slice html = slice_advanced(input, signatures[i][0].len);
			if (html.len > 0 && (html.ptr[0] == ' ' || html.ptr[0] == '>')) {
				return "text/html";
			}
		}
	}

	return NULL;
}

const char *is_image_mime_type(Slice input) {
	static const Slice signatures[][3] = {
		{ slice_bytes("\x00\x00\x01\x00"), 			slice_bytes(FF FF FF FF), 		slice_bytes("image/x-icon") },
		{ slice_bytes("\x00\x00\x02\x00"), 			slice_bytes(FF FF FF FF), 		slice_bytes("image/x-icon") },
		{ slice_bytes("\x42\x4d"), 					slice_bytes(FF FF), 			slice_bytes("image/bmp") },
		{ slice_bytes("\x47\x49\x46\x38\x37\x61"), 	slice_bytes(FF FF FF FF FF FF), slice_bytes("image/gif") },
		{ slice_bytes("\x47\x49\x46\x38\x39\x61"), 	slice_bytes(FF FF FF FF FF FF), slice_bytes("image/gif") },

		{ slice_bytes("\x52\x49\x46\x46\x00\x00\x00\x00\x57\x45\x42\x50\x56\x50"),
		  slice_bytes(FF FF FF FF ZZ ZZ ZZ ZZ FF FF FF FF FF FF), 					slice_bytes("image/webp") },

		{ slice_bytes("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"),
		  slice_bytes(FF FF FF FF FF FF FF FF), 									slice_bytes("image/png") },

		{ slice_bytes("\xff\xd8\xff"), 				slice_bytes(FF FF FF), 			slice_bytes("image/jpeg") },
	};

	for (size_t i = 0; i < sizeof(signatures)/sizeof(signatures[0]); i++) {
		if (is_match_mime_pattern(input, signatures[i][0], signatures[i][1], EMPTY_PATTERN)) {
			return signatures[i][2].ptr;
		}
	}

	return NULL;
}

const char *is_mp4(Slice input) {
	if (input.len < 12) {
		return NULL;
	}

	size_t box_size = input.ptr[0] << 6 | input.ptr[1] << 4 | input.ptr[2] << 2 | input.ptr[3];
	if (input.len < box_size || box_size % 4 != 0) {
		return NULL;
	}
	if (strncmp(input.ptr + 4, "ftyp", 4) != 0) {
		return NULL;
	}
	if (strncmp(input.ptr + 8, "mp3", 4) == 0) {
		return "video/mp4";
	}

	size_t bytes_read = 16;
	while (bytes_read < box_size) {
		if (strncmp(input.ptr + bytes_read, "mp4", 3) == 0) {
			return "video/mp4";
		}
		bytes_read += 4;
	}

	return NULL;
}

const char *is_audio_video_mime_type(Slice input) {
	static const Slice signatures[][4] = {
		{ slice_bytes("\x46\x4F\x52\x4D\x00\x00\x00\x00\x41\x49\x46\x46"),
		  slice_bytes(FF FF FF FF ZZ ZZ ZZ ZZ FF FF FF FF), 						slice_bytes("audio/aiff") },

		{ slice_bytes("\x49\x44\x33"), 				slice_bytes(FF FF FF), 			slice_bytes("audio/mpeg") },
		{ slice_bytes("\x4F\x67\x67\x53\x00"), 		slice_bytes(FF FF FF FF FF), 	slice_bytes("application/ogg") },

		{ slice_bytes("\x4D\x54\x68\x64\x00\x00\x00\x06"),
		  slice_bytes(FF FF FF FF FF FF FF FF), 									slice_bytes("audio/midi") },

		{ slice_bytes("\x52\x49\x46\x46\x00\x00\x00\x00\x41\x56\x49\x20"),
		  slice_bytes(FF FF FF FF ZZ ZZ ZZ ZZ FF FF FF FF), 						slice_bytes("video/avi") },

		{ slice_bytes("\x52\x49\x46\x46\x00\x00\x00\x00\x57\x41\x56\x45"),
		  slice_bytes(FF FF FF FF ZZ ZZ ZZ ZZ FF FF FF FF ), 						slice_bytes("audio/wave") },
	};

	for (size_t i = 0; i < sizeof(signatures)/sizeof(signatures[0]); i++) {
		if (is_match_mime_pattern(input, signatures[i][0], signatures[i][1], EMPTY_PATTERN)) {
			return signatures[i][2].ptr;
		}
	}

	const char *type = NULL;
	if ((type = is_mp4(input)) != NULL) {
		return type;
	}
	// TODO: video/webm, audio/mpeg

	return NULL;
};

const char *is_font_mime_type(Slice input) {
	static const Slice signatures[][4] = {
		{ slice_bytes("\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x4C\x50"),
		  slice_bytes(ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ ZZ FF FF),	slice_bytes("application/vnd.ms-fontobject") },

		{ slice_bytes("\x00\x01\x00\x00"), 			slice_bytes(FF FF FF FF), 		slice_bytes("font/ttf") },
		{ slice_bytes("\x4F\x54\x54\x4F"), 			slice_bytes(FF FF FF FF), 		slice_bytes("font/otf") },
		{ slice_bytes("\x74\x74\x63\x66"), 			slice_bytes(FF FF FF FF), 		slice_bytes("font/collection") },
		{ slice_bytes("\x77\x4F\x46\x46"), 			slice_bytes(FF FF FF FF), 		slice_bytes("font/woff") },
		{ slice_bytes("\x77\x4F\x46\x32"), 			slice_bytes(FF FF FF FF), 		slice_bytes("font/woff2") },
	};

	for (size_t i = 0; i < sizeof(signatures)/sizeof(signatures[0]); i++) {
		if (is_match_mime_pattern(input, signatures[i][0], signatures[i][1], EMPTY_PATTERN)) {
			return signatures[i][2].ptr;
		}
	}

	return NULL;
}

const char *is_archive_mime_type(Slice input) {
	static const Slice signatures[][4] = {
		{ slice_bytes("\x1F\x8B\x08"), 					slice_bytes(FF FF FF), 				slice_bytes("application/x-gzip") },
		{ slice_bytes("\x50\x4B\x03\x04"), 				slice_bytes(FF FF FF FF), 			slice_bytes("application/zip") },
		{ slice_bytes("\x52\x61\x72\x21\x1A\x07\x00"), 	slice_bytes(FF FF FF FF FF FF FF), 	slice_bytes("application/x-rar-compressed") },
	};

	for (size_t i = 0; i < sizeof(signatures)/sizeof(signatures[0]); i++) {
		if (is_match_mime_pattern(input, signatures[i][0], signatures[i][1], EMPTY_PATTERN)) {
			return signatures[i][2].ptr;
		}
	}

	return NULL;
}

const char *find_mime(Slice input) {
	const char *type = NULL;

	if ((type = is_html_xml_pdf(input)) != NULL) {
		return type;
	}
	if ((type = is_image_mime_type(input)) != NULL) {
		return type;
	}
	if ((type = is_audio_video_mime_type(input)) != NULL) {
		return type;
	}

	if (input.len < MAX_PLAIN_TEXT_LEN) {
		for (size_t i = 0; i < input.len; i++) {
			int c = input.ptr[i];
			if ((c >= 0 && c <= 0x08) || c == 0x0b || (c >= 0x0e && c <= 0x1a) || (c >= 0x1c && c <= 0x1f)) {
				break;
			}
		}

		return "plain/text";
	}

	return "application/octet-stream";
}

#endif // MIME_H
