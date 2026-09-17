# ADR 0001: Oversampling Quality

## Status

Accepted

## Decision

Nonlinear processors may offer Off, 2x, and 4x oversampling. Both JUCE
maximum-quality polyphase IIR and equiripple FIR paths are prepared in advance.
The product labels these `Minimum Phase` and `Linear Phase` rather than ranking
one as universally better.

The default is 4x minimum phase. It provides strong alias rejection without the
latency and pre-ringing of the FIR alternative. Linear phase remains available
when phase response is the user's priority. Off adds no oversampling latency.

Factors above 4x are excluded unless measurements show materially lower audible
aliasing and controlled listening demonstrates a repeatable benefit on supported
hardware.

## Consequences

- Quality paths consume memory because they are prepared before processing.
- Quality changes alter latency and are non-automatable project settings.
- Changes requested during confirmed playback are deferred.
- Dry signals must be delayed by the active path's exact integer latency.
- Every quality path requires frequency, aliasing, latency, and stability tests.
