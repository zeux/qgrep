#pragma once

#define Kb *1024
#define Mb Kb Kb

// Approximate uncompressed total size of the chunk
const size_t kChunkSize = 1 Mb;

// Total amount of chunk data in flight
const size_t kMaxQueuedChunkData = 256 Mb;

// Max size of the chunk that can be pushed into async queue ($$ remove this)
const size_t kMaxChunkSizeAsync = 32 Mb;

// Total amount of buffered output in flight
const size_t kMaxBufferedOutput = 32 Mb;

// Flush buffered output from the current chunk after reaching this threshold, if possible
const size_t kBufferedOutputFlushThreshold = 32 Kb;

#undef Mb
#undef Kb