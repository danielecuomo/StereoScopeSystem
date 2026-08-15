# SMS texture upload test

Based on the RunFor(16) test build.

Changes in this test:
- Normal SMS 2-D updates only one texture per frame.
- Both 3DS top-screen eyes reuse that texture when stereo is inactive.
- SegaScope/stereo continues to update separate eye textures.
- Existing RunFor(16), SYNCDRAW, CPU80 and other prior test changes are retained.

This is a diagnostic build intended to compare frame speed against the RunFor(16) build.
