/*
 * PercivalEmulatorFrameDecoder.cpp
 *
 *  Created on: Feb 24, 2015
 *      Author: tcn45
 */

#include "PercivalEmulatorFrameDecoder.h"
#include <iostream>
#include <arpa/inet.h>

using namespace FrameReceiver;

PercivalEmulatorFrameDecoder::PercivalEmulatorFrameDecoder(LoggerPtr& logger) :
        FrameDecoder(logger),
		current_frame_seen_(-1),
		current_frame_buffer_id_(-1),
		current_frame_buffer_(0),
		current_frame_header_(0),
		dropping_frame_data_(false)
{
    current_packet_header_.reset(new uint8_t[sizeof(PercivalEmulatorFrameDecoder::PacketHeader)]);
    dropped_frame_buffer_.reset(new uint8_t[PercivalEmulatorFrameDecoder::total_frame_size]);
}

PercivalEmulatorFrameDecoder::~PercivalEmulatorFrameDecoder()
{
}

const size_t PercivalEmulatorFrameDecoder::get_frame_buffer_size(void) const
{
    return PercivalEmulatorFrameDecoder::total_frame_size;
}

const size_t PercivalEmulatorFrameDecoder::get_frame_header_size(void) const
{
    return sizeof(PercivalEmulatorFrameDecoder::FrameHeader);
}

const size_t PercivalEmulatorFrameDecoder::get_packet_header_size(void) const
{
    return sizeof(PercivalEmulatorFrameDecoder::PacketHeader);
}

void* PercivalEmulatorFrameDecoder::get_packet_header_buffer(void)
{
    return current_packet_header_.get();
}

void PercivalEmulatorFrameDecoder::process_packet_header(size_t bytes_received)
{
    //TODO validate header size and content, handle incoming new packet buffer allocation etc

	uint32_t frame = get_frame_number();
	uint16_t packet_number = get_packet_number();
	uint8_t  subframe = get_subframe_number();
	uint8_t  type = get_packet_type();

    LOG4CXX_DEBUG_LEVEL(3, logger_, "Got packet header:"
            << " type: "     << (int)type << " subframe: " << (int)subframe
            << " packet: "   << packet_number    << " frame: "    << frame
    );

    if (frame != current_frame_seen_)
    {
        current_frame_seen_ = frame;

    	if (frame_buffer_map_.count(current_frame_seen_) == 0)
    	{
    	    if (empty_buffer_queue_.empty())
            {
                current_frame_buffer_ = dropped_frame_buffer_.get();

    	        if (!dropping_frame_data_)
                {
                    LOG4CXX_ERROR(logger_, "First packet from frame " << current_frame_seen_ << " detected but no free buffers available. Dropping packet data for this frame");
                    dropping_frame_data_ = true;
                }
            }
    	    else
    	    {

                current_frame_buffer_id_ = empty_buffer_queue_.front();
                empty_buffer_queue_.pop();
                frame_buffer_map_[current_frame_seen_] = current_frame_buffer_id_;
                current_frame_buffer_ = buffer_manager_->get_buffer_address(current_frame_buffer_id_);

                if (!dropping_frame_data_)
                {
                    LOG4CXX_DEBUG_LEVEL(2, logger_, "First packet from frame " << current_frame_seen_ << " detected, allocating frame buffer ID " << current_frame_buffer_id_);
                }
                else
                {
                    dropping_frame_data_ = false;
                    LOG4CXX_DEBUG_LEVEL(2, logger_, "Free buffer now available for frame " << current_frame_seen_ << ", allocating frame buffer ID " << current_frame_buffer_id_);
                }
    	    }

    	    // Initialise frame header
            current_frame_header_ = reinterpret_cast<FrameHeader*>(current_frame_buffer_);
            current_frame_header_->frame_number = current_frame_seen_;
            current_frame_header_->frame_state = FrameDecoder::FrameReceiveStateIncomplete;
            current_frame_header_->packets_received = 0;

            clock_gettime(CLOCK_REALTIME, reinterpret_cast<struct timespec*>(&(current_frame_header_->frame_start_time)));

    	}
    	else
    	{
    		current_frame_buffer_id_ = frame_buffer_map_[current_frame_seen_];
        	current_frame_buffer_ = buffer_manager_->get_buffer_address(current_frame_buffer_id_);
        	current_frame_header_ = reinterpret_cast<FrameHeader*>(current_frame_buffer_);
    	}

    }

    // Update packet_number state map in frame header
    current_frame_header_->packet_state[type][subframe][packet_number] = 1;

}

void* PercivalEmulatorFrameDecoder::get_next_payload_buffer(void) const
{

    uint8_t* next_receive_location =
            reinterpret_cast<uint8_t*>(current_frame_buffer_) +
            get_frame_header_size() +
            (data_type_size * get_packet_type()) +
            (subframe_size * get_subframe_number()) +
            (primary_packet_size * get_packet_number());

    return reinterpret_cast<void*>(next_receive_location);
}

size_t PercivalEmulatorFrameDecoder::get_next_payload_size(void) const
{
   size_t next_receive_size = 0;

	if (get_packet_number() < num_primary_packets)
	{
		next_receive_size = primary_packet_size;
	}
	else
	{
		next_receive_size = tail_packet_size;
	}

    return next_receive_size;
}

FrameDecoder::FrameReceiveState PercivalEmulatorFrameDecoder::process_packet(size_t bytes_received)
{

    FrameDecoder::FrameReceiveState frame_state = FrameDecoder::FrameReceiveStateIncomplete;

	current_frame_header_->packets_received++;

	if (current_frame_header_->packets_received == num_frame_packets)
	{

	    // Set frame state accordingly
		frame_state = FrameDecoder::FrameReceiveStateComplete;

		// Complete frame header
		current_frame_header_->frame_state = frame_state;

		if (!dropping_frame_data_)
		{
			// Erase frame from buffer map
			frame_buffer_map_.erase(current_frame_seen_);

			// Notify main thread that frame is ready
			ready_callback_(current_frame_buffer_id_, current_frame_seen_);

			// Reset current frame seen ID so that if next frame has same number (e.g. repeated
			// sends of single frame 0), it is detected properly
			current_frame_seen_ = -1;
		}
	}

	return frame_state;
}

uint8_t PercivalEmulatorFrameDecoder::get_packet_type(void) const
{
    return *(reinterpret_cast<uint8_t*>(raw_packet_header()+0));
}

uint8_t PercivalEmulatorFrameDecoder::get_subframe_number(void) const
{
    return *(reinterpret_cast<uint8_t*>(raw_packet_header()+1));
}

uint16_t PercivalEmulatorFrameDecoder::get_packet_number(void) const
{
	uint16_t packet_number_raw = *(reinterpret_cast<uint16_t*>(raw_packet_header()+6));
    return ntohs(packet_number_raw);
}

uint32_t PercivalEmulatorFrameDecoder::get_frame_number(void) const
{
	uint32_t frame_number_raw = *(reinterpret_cast<uint32_t*>(raw_packet_header()+2));
    return ntohl(frame_number_raw);
}

uint8_t* PercivalEmulatorFrameDecoder::raw_packet_header(void) const
{
    return reinterpret_cast<uint8_t*>(current_packet_header_.get());
}
