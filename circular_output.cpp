/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (C) 2020, Raspberry Pi (Trading) Ltd.
 *
 * circular_output.cpp - Write output to circular buffer which we save on exit.
 */

#include "circular_output.hpp"

static constexpr int CIRCULAR_BUFFER_SIZE = 1<<22; // 4MB, we could consider this more carefully...

// We're going to align the frames within the buffer to friendly byte boundaries
static constexpr int ALIGN = 16; // power of 2, please

struct Header
{
	unsigned int length;
	bool keyframe;
	int64_t timestamp;
};
static_assert(sizeof(Header) % ALIGN == 0, "Header should have aligned size");

CircularOutput::CircularOutput(VideoOptions const &options)
	: cb_(CIRCULAR_BUFFER_SIZE), Output(options)
{
	// Open this now, so that we can get any complaints out of the way
	fp_ = fopen(options_.output.c_str(), "w");
	if (!fp_)
		throw std::runtime_error("could not open output file");
}

CircularOutput::~CircularOutput()
{
	// We do have to skip to the first I frame before dumping stuff to disk. If there are
	// no I frames you will get nothing. Caveat emptor, methinks.
	unsigned int total = 0, frames = 0;
	bool seen_keyframe = false;
	Header header;
	FILE *fp = fp_; // can't capture a class member in a lambda
	while (!cb_.Empty())
	{
		uint8_t *dst = (uint8_t *)&header;
		cb_.Read([&dst](void *src, int n) { memcpy(dst, src, n); dst += n; }, sizeof(header));
		seen_keyframe |= header.keyframe;
		if (seen_keyframe)
		{
			cb_.Read([fp](void *src, int n) { fwrite(src, 1, n, fp); }, header.length);
			cb_.Skip((ALIGN - header.length) & (ALIGN - 1));
			total += header.length;
			frames++;
		}
		else
			cb_.Skip((header.length + ALIGN - 1) & ~(ALIGN - 1));
	}
	fclose(fp_);
	std::cout << "Wrote " << total << " bytes (" << frames << " frames)" << std::endl;
}

void CircularOutput::outputBuffer(void *mem, size_t size, int64_t timestamp_us, uint32_t flags)
{
	// First make sure there's enough space.
	int pad = (ALIGN - size) & (ALIGN - 1);
	while (size + pad + sizeof(Header) > cb_.Available())
	{
		if (cb_.Empty())
			throw std::runtime_error("circular buffer too small");
		Header header;
		uint8_t *dst = (uint8_t *)&header;
		cb_.Read([&dst](void *src, int n) { memcpy(dst, src, n); dst += n; }, sizeof(header));
		cb_.Skip((header.length + ALIGN - 1) & ~(ALIGN - 1));
	}
	Header header = { size, flags & FLAG_KEYFRAME, timestamp_us };
	cb_.Write(&header, sizeof(header));
	cb_.Write(mem, size);
	cb_.Pad(pad);
}
