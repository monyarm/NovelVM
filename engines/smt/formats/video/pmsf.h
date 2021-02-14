#ifndef PMSF_H
#define PMSF_H

#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/debug.h"
#include "common/hex.h"

#include "common/pack-start.h"	// START STRUCT PACKING
struct PSMFHeader
{
    char magic[4];
    char version_num[4];
    uint32 header_size;
    uint32 stream_size;

    byte padding[64];

    uint32 stream_info_size;
}PACKED_STRUCT;

struct PSMFStream
{
    byte stream_id; // E0, E1, BD, etc.
    byte private_stream_id; // 00, 01, etc.

    byte unk5; // Always 0x20?
    byte unk6; // FB, 55, 9F, 04... not sure.

    // Filled for video.
    uint32 ep_map_offset; // Optional (e.g. 0), from start of file.
    uint32 ep_map_count; // Optional (e.g. 0.)
    byte stream_width_div_16;
    byte stream_height_div_16;

    // Filled for audio.
    byte num_channels;
    byte frequency;
}PACKED_STRUCT;

struct PSMFStreamInfo
{
    byte stream_start_pts[6];
    byte stream_end_pts[6];
    uint32 unk1;
    uint32 unk2;
    byte num_streams;
    byte unk3;

    uint32 remaining_length;

    byte stream_start_pts2[6];
    byte stream_end_pts2[6];

    uint32 remaining_length2;

    uint16 num_streams2;



}PACKED_STRUCT;

struct PSMFEPEntry
{
  byte ep_index; // This is what our code and JPCSP call it, but always C0?
  byte pic_offset; // Don't know what this offsets into or how it can be one byte.
  uint32 pts; // Note only 32 bits.
  uint32 offset;
}PACKED_STRUCT;

struct PSMFData
{
    PSMFHeader header;
    PSMFStreamInfo stream_info;
    Common::Array<PSMFStream> streams; //num_streams2
    Common::Array<PSMFEPEntry> ep_entries;
}PACKED_STRUCT;

#include "common/pack-end.h"	// END STRUCT PACKING

class PMSFFile
{
public:
    PMSFFile();

    void ReadFile(char *path);

private:
    PSMFData dat;
};

#endif