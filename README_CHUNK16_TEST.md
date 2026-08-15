SMS performance test: RunFor(16)

This test is based on the previous RunFor(8) version. It batches up to 16 Z80 T-states per Processor::RunFor call before ticking video/audio.

Purpose: determine whether larger CPU/video/audio scheduling batches materially improve throughput. This is an experimental performance test; raster/timing-sensitive software may reveal compatibility issues.
