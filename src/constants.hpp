#pragma once

#define Kb *1024
#define Mb Kb Kb

// Approximate uncompressed total size of the chunk
const size_t kChunkSize = 512 Kb;

// Total amount of chunk data in flight
const size_t kMaxQueuedChunkData = 256 Mb;

// Total amount of buffered output in flight
const size_t kMaxBufferedOutput = 32 Mb;

// Flush buffered output from the current chunk after reaching this threshold, if possible
const size_t kBufferedOutputFlushThreshold = 32 Kb;

// File list compression level, 0-9
const int kFileListCompressionLevel = 1;

// File data compression level, 0-9
const int kFileDataCompressionLevel = 5;

// Wait for several seconds before writing changes to amortize writes when many changes are done at once
const int kWatchWriteDeadline = 1;

// Wait for many seconds before launching an update to minimize the chance of concurrent work
const int kWatchUpdateTimeout = 60;

// When we're above a certain threshold of changed files, automatically update
const int kWatchUpdateThresholdFiles = 100;

#undef Mb
#undef Kb