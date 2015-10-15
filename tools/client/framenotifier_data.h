/**
framenotifier_data.h

  Created on: 15 Oct 2015
      Author: up45
*/

#ifndef TOOLS_CLIENT_FRAMENOTIFIER_DATA_H_
#define TOOLS_CLIENT_FRAMENOTIFIER_DATA_H_

#include <string>
#include <stdint.h>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <log4cxx/logger.h>


//===========================================================================================
//    Bits stolen directly from the frameReceiver Percival Emulator header
//    - should make it into a public header file
//
//
//    Memory layout of a full frame from Python Emulator in shared memory:
//
//    0x000000 [[Data Frame][FrameHeader size=1056 bytes]
//    0x000420 [            [Subframe 0][UDP packet 0 size=8192 bytes]
//    0x002420                          [UDP packet 1]
//    0x004420                          [UDP packet 2]
//                                      ...
//    0x1FC420                          [UDP packet 254]
//    0x1FE420                          [UDP packet 255 size=512 bytes]
//    0x1FE620              [Subframe 1][UDP packet 0]                    subframe size: (8192 x 255) + 512 = 2089472 bytes
//                          ...
//             ]
//    0x3FC820 [[Reset Frame][FrameHeader size=1056 bytes]]
//
//    In terms of pixels, the python emulator creates a P2M image of:
//        width x  = 2 x 22 x 32 = 1404 pixels
//        height y = 2 x 106 x 7 = 1484 pixels
//

static const size_t primary_packet_size = 8192;
static const size_t num_primary_packets = 255;
static const size_t tail_packet_size    = 512;
static const size_t num_tail_packets    = 1;
static const size_t num_subframes       = 2;
static const size_t num_data_types      = 2;

typedef struct
{
    uint32_t frame_number;
    uint32_t frame_state;
    struct timespec frame_start_time;
    uint32_t packets_received;
    uint8_t  packet_state[num_data_types][num_subframes][num_primary_packets + num_tail_packets];
} FrameHeader;

static const size_t subframe_size       = (num_primary_packets * primary_packet_size)
        + (num_tail_packets * tail_packet_size);
static const size_t data_type_size      = subframe_size * num_subframes;
static const size_t total_frame_size    = (data_type_size * num_data_types) + sizeof(FrameHeader);
static const size_t num_frame_packets   = num_subframes * num_data_types *
        (num_primary_packets + num_tail_packets);

// Shared Buffer (IPC) Header
typedef struct
{
    size_t manager_id;
    size_t num_buffers;
    size_t buffer_size;
} Header;


//============== end of stolen bits ============================================================
class Frame
{
public:
    Frame(size_t data_size);
    ~Frame();

    void copy_data(const void* data_src);
    void copy_header(const void* header_src);

    const FrameHeader* get_header(){return this->header;};
    size_t get_data_size(){return this->data_size;};
private:
    Frame();
    Frame(const Frame& src); // Don't try to copy one of these!

    FrameHeader *header;
    uint16_t *data;
    size_t data_size;
};

class SharedMemParser
{
public:
    SharedMemParser(const std::string & shared_mem_name);
    ~SharedMemParser();
    void get_frame(Frame& dest_frame, unsigned int buffer_id);
    size_t get_buffer_size();

private:
    SharedMemParser();
    SharedMemParser(const SharedMemParser& src); // Don't copy one of these!

    const void* get_buffer_address(unsigned int bufferid) const;
    const void* get_frame_header_address(unsigned int bufferid) const;
    const void* get_frame_data_address(unsigned int bufferid) const;

    log4cxx::LoggerPtr logger;
    boost::interprocess::shared_memory_object shared_mem;
    boost::interprocess::mapped_region        shared_mem_region;
    Header*                                   shared_mem_header;
};


#endif /* TOOLS_CLIENT_FRAMENOTIFIER_DATA_H_ */
