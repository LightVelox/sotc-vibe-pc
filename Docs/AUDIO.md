# Audio playback

The game's original IOP sound driver uploads ADPCM into SPU2 RAM and programs its voices. `IopSpu2`
decodes active voices, applies ADSR, stereo gains and mixer gates, and routes core 0 through core 1.
Voice addresses and loop flags keep advancing for silent voices without spending time on inaudible
ADPCM decoding or interpolation.

Pitch conversion uses a four-tap Gaussian kernel with 256 fractional phases. The coefficient
construction follows the mathematical generator credited to Near, nocash and Ryphecha in
[PCSX2's interpolation table](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/interpolate_table.h).
The kernel is constructed once; the per-sample path uses integer coefficients and four sample values.
The optional `SINT` save-state section preserves that history. Older states without it remain readable.

The mixer submits 512 stereo frames per host transfer instead of flushing after every IOP scheduler
slice. The audio callback waits for 2,048 frames (about 43 ms at 48 kHz) before consuming audio.
If the producer falls behind, playback fades out over 128 frames and waits for that buffer to refill.
If the bounded queue overruns, it drops old samples and crossfades into the retained waveform over
128 frames. These transitions avoid sudden cuts at arbitrary waveform amplitudes. Sustained
slowdowns can still cause pauses when the producer cannot supply audio in real time.

Adaptive host resampling was reverted after the user reported significantly worse sound. Playback
again consumes the original mixed PCM at a fixed 48 kHz, without host speed or pitch adjustments.
The remaining intro gaps may be related to game slowdown; that cause has not been confirmed.

Reverb, hardware volume sweeps, noise, pitch modulation, AutoDMA PCM input and SPU2 IRQ-address
handling remain fidelity gaps. `PS2X_AUDIO_DUMP` records the mixer output before host buffering;
it cannot by itself confirm that speaker playback is free of underruns.
