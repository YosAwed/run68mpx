#include "x68k_audio_live_apple.h"

#include <AudioUnit/AudioUnit.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
	LIVE_RING_FRAMES = 8192,
	LIVE_PREFILL_FRAMES = 2048,
	LIVE_CHANNELS = 2,
	LIVE_WAIT_LIMIT = 2000
};

struct X68K_LIVE_AUDIO {
	AudioUnit unit;
	int16_t *ring;
	uint32_t source_sample_rate;
	uint32_t output_sample_rate;
	_Atomic uint64_t read_position;
	_Atomic uint64_t write_position;
	_Atomic uint64_t rendered_frames;
	_Atomic uint32_t callbacks;
	uint64_t output_frames;
	uint64_t nonzero_samples;
	uint16_t peak;
	int started;
	int have_previous_sample;
	int16_t previous_sample[LIVE_CHANNELS];
	uint64_t source_frame_index;
	uint64_t next_output_position;
};

static OSStatus render_audio(void *context, AudioUnitRenderActionFlags *flags,
	const AudioTimeStamp *timestamp, UInt32 bus, UInt32 frame_count,
	AudioBufferList *buffers)
{
	X68K_LIVE_AUDIO *audio = (X68K_LIVE_AUDIO *)context;
	uint64_t read_position;
	uint64_t write_position;
	uint64_t available;
	UInt32 copy_frames;
	UInt32 first_frames;
	UInt32 frame;
	int16_t *destination;

	(void)flags;
	(void)timestamp;
	(void)bus;
	if (buffers->mNumberBuffers != 1 || buffers->mBuffers[0].mData == NULL) {
		for (frame = 0; frame < buffers->mNumberBuffers; ++frame) {
			if (buffers->mBuffers[frame].mData != NULL)
				memset(buffers->mBuffers[frame].mData, 0,
				       buffers->mBuffers[frame].mDataByteSize);
		}
		return noErr;
	}
	destination = (int16_t *)buffers->mBuffers[0].mData;
	read_position = atomic_load_explicit(&audio->read_position,
	                                     memory_order_relaxed);
	write_position = atomic_load_explicit(&audio->write_position,
	                                      memory_order_acquire);
	available = write_position - read_position;
	copy_frames = available < frame_count ? (UInt32)available : frame_count;
	first_frames = LIVE_RING_FRAMES -
	               (UInt32)(read_position % LIVE_RING_FRAMES);
	if (first_frames > copy_frames)
		first_frames = copy_frames;
	memcpy(destination,
	       audio->ring + (read_position % LIVE_RING_FRAMES) * LIVE_CHANNELS,
	       first_frames * LIVE_CHANNELS * sizeof(int16_t));
	if (first_frames < copy_frames)
		memcpy(destination + first_frames * LIVE_CHANNELS, audio->ring,
		       (copy_frames - first_frames) * LIVE_CHANNELS * sizeof(int16_t));
	if (copy_frames < frame_count) {
		memset(destination + copy_frames * 2, 0,
		       (frame_count - copy_frames) * LIVE_CHANNELS * sizeof(int16_t));
	}
	atomic_store_explicit(&audio->read_position,
	                      read_position + copy_frames, memory_order_release);
	atomic_fetch_add_explicit(&audio->rendered_frames, copy_frames,
	                          memory_order_relaxed);
	atomic_fetch_add_explicit(&audio->callbacks, 1, memory_order_relaxed);
	return noErr;
}

static void update_sample_stats(X68K_LIVE_AUDIO *audio,
	const int16_t *samples, size_t frames)
{
	size_t index;
	uint16_t magnitude;

	for (index = 0; index < frames * LIVE_CHANNELS; ++index) {
		int16_t sample = samples[index];

		if (sample != 0)
			audio->nonzero_samples++;
		magnitude = sample == INT16_MIN ? 32768u :
		            (uint16_t)(sample < 0 ? -sample : sample);
		if (magnitude > audio->peak)
			audio->peak = magnitude;
	}
}

static int write_output_frames(X68K_LIVE_AUDIO *audio,
	const int16_t *samples, size_t frames)
{
	static const struct timespec wait_time = {0, 1000000};
	size_t written = 0;
	int waits = 0;

	while (written < frames) {
		uint64_t write_position = atomic_load_explicit(
			&audio->write_position, memory_order_relaxed);
		uint64_t read_position = atomic_load_explicit(
			&audio->read_position, memory_order_acquire);
		uint64_t used = write_position - read_position;
		size_t free_frames = LIVE_RING_FRAMES - (size_t)used;
		size_t contiguous = LIVE_RING_FRAMES -
		                    (size_t)(write_position % LIVE_RING_FRAMES);
		size_t chunk = frames - written;

		if (free_frames == 0) {
			if (++waits >= LIVE_WAIT_LIMIT) {
				fprintf(stderr,
				        "AudioUnit output stopped consuming samples\n");
				return -1;
			}
			(void)nanosleep(&wait_time, NULL);
			continue;
		}
		if (chunk > free_frames)
			chunk = free_frames;
		if (chunk > contiguous)
			chunk = contiguous;
		memcpy(audio->ring +
		           (write_position % LIVE_RING_FRAMES) * LIVE_CHANNELS,
		       samples + written * LIVE_CHANNELS,
		       chunk * LIVE_CHANNELS * sizeof(int16_t));
		atomic_store_explicit(&audio->write_position,
		                      write_position + chunk, memory_order_release);
		written += chunk;
		waits = 0;
	}
	update_sample_stats(audio, samples, frames);
	audio->output_frames += frames;
	return 0;
}

X68K_LIVE_AUDIO *x68k_live_audio_create(uint32_t source_sample_rate)
{
	AudioComponentDescription description;
	AudioComponent component;
	AudioStreamBasicDescription device_format;
	AudioStreamBasicDescription format;
	AURenderCallbackStruct callback;
	X68K_LIVE_AUDIO *audio;
	OSStatus status;
	UInt32 format_size;

	if (source_sample_rate == 0)
		return NULL;
	audio = (X68K_LIVE_AUDIO *)calloc(1, sizeof(*audio));
	if (audio == NULL)
		return NULL;
	audio->ring = (int16_t *)calloc(LIVE_RING_FRAMES * LIVE_CHANNELS,
	                                sizeof(*audio->ring));
	if (audio->ring == NULL) {
		free(audio);
		return NULL;
	}
	audio->source_sample_rate = source_sample_rate;
	memset(&description, 0, sizeof(description));
	description.componentType = kAudioUnitType_Output;
	description.componentSubType = kAudioUnitSubType_DefaultOutput;
	description.componentManufacturer = kAudioUnitManufacturer_Apple;
	component = AudioComponentFindNext(NULL, &description);
	status = component == NULL ? kAudio_ParamError :
	         AudioComponentInstanceNew(component, &audio->unit);
	memset(&device_format, 0, sizeof(device_format));
	format_size = sizeof(device_format);
	if (status == noErr)
		status = AudioUnitGetProperty(audio->unit,
		                              kAudioUnitProperty_StreamFormat,
		                              kAudioUnitScope_Output, 0,
		                              &device_format, &format_size);
	if (status == noErr &&
	    (device_format.mSampleRate < 1.0 ||
	     device_format.mSampleRate > (double)UINT32_MAX))
		status = kAudio_ParamError;
	audio->output_sample_rate = status == noErr
		? (uint32_t)(device_format.mSampleRate + 0.5)
		: 0;
	memset(&format, 0, sizeof(format));
	format.mSampleRate = audio->output_sample_rate;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
	                      kLinearPCMFormatFlagIsPacked |
	                      kAudioFormatFlagsNativeEndian;
	format.mBytesPerPacket = 4;
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = 4;
	format.mChannelsPerFrame = LIVE_CHANNELS;
	format.mBitsPerChannel = 16;
	if (status == noErr)
		status = AudioUnitSetProperty(audio->unit,
		                              kAudioUnitProperty_StreamFormat,
		                              kAudioUnitScope_Input, 0, &format,
		                              sizeof(format));
	callback.inputProc = render_audio;
	callback.inputProcRefCon = audio;
	if (status == noErr)
		status = AudioUnitSetProperty(audio->unit,
		                              kAudioUnitProperty_SetRenderCallback,
		                              kAudioUnitScope_Input, 0, &callback,
		                              sizeof(callback));
	if (status == noErr)
		status = AudioUnitInitialize(audio->unit);
	if (status != noErr) {
		fprintf(stderr, "DefaultOutput AudioUnit failed: %d\n", (int)status);
		if (audio->unit != NULL) {
			(void)AudioUnitUninitialize(audio->unit);
			(void)AudioComponentInstanceDispose(audio->unit);
		}
		free(audio->ring);
		free(audio);
		return NULL;
	}
	return audio;
}

int x68k_live_audio_write(X68K_LIVE_AUDIO *audio, const int16_t *samples,
	size_t frames)
{
	int16_t converted[512 * LIVE_CHANNELS];
	size_t converted_frames = 0;
	size_t input_frame;
	OSStatus status;

	if (audio == NULL || samples == NULL)
		return -1;
	for (input_frame = 0; input_frame < frames; ++input_frame) {
		const int16_t *current = samples + input_frame * LIVE_CHANNELS;

		if (!audio->have_previous_sample) {
			converted[0] = current[0];
			converted[1] = current[1];
			converted_frames = 1;
			audio->previous_sample[0] = current[0];
			audio->previous_sample[1] = current[1];
			audio->have_previous_sample = 1;
			audio->next_output_position = audio->source_sample_rate;
			continue;
		}
		audio->source_frame_index++;
		while (audio->next_output_position <=
		       audio->source_frame_index * audio->output_sample_rate) {
			uint64_t segment_start = (audio->source_frame_index - 1u) *
			                         audio->output_sample_rate;
			uint64_t fraction = audio->next_output_position -
			                    segment_start;
			unsigned int channel;

			for (channel = 0; channel < LIVE_CHANNELS; ++channel) {
				int64_t value =
					(int64_t)audio->previous_sample[channel] *
					    (audio->output_sample_rate - fraction) +
					(int64_t)current[channel] * fraction;
				converted[converted_frames * LIVE_CHANNELS + channel] =
					(int16_t)(value / audio->output_sample_rate);
			}
			converted_frames++;
			audio->next_output_position += audio->source_sample_rate;
			if (converted_frames == 512) {
				if (write_output_frames(audio, converted,
				                        converted_frames) != 0)
					return -1;
				converted_frames = 0;
			}
		}
		audio->previous_sample[0] = current[0];
		audio->previous_sample[1] = current[1];
	}
	if (converted_frames != 0 &&
	    write_output_frames(audio, converted, converted_frames) != 0)
		return -1;
	if (!audio->started &&
	    atomic_load_explicit(&audio->write_position, memory_order_acquire) >=
	        LIVE_PREFILL_FRAMES) {
		status = AudioOutputUnitStart(audio->unit);
		if (status != noErr) {
			fprintf(stderr, "DefaultOutput AudioUnit start failed: %d\n",
			        (int)status);
			return -1;
		}
		audio->started = 1;
	}
	return 0;
}

int x68k_live_audio_destroy(X68K_LIVE_AUDIO *audio,
	X68K_LIVE_AUDIO_STATS *stats)
{
	int result = 0;

	if (audio == NULL)
		return 0;
	if (audio->started && AudioOutputUnitStop(audio->unit) != noErr)
		result = -1;
	if (AudioUnitUninitialize(audio->unit) != noErr)
		result = -1;
	if (AudioComponentInstanceDispose(audio->unit) != noErr)
		result = -1;
	if (stats != NULL) {
		stats->output_frames = audio->output_frames;
		stats->rendered_frames = atomic_load(&audio->rendered_frames);
		stats->nonzero_samples = audio->nonzero_samples;
		stats->callbacks = atomic_load(&audio->callbacks);
		stats->peak = audio->peak;
	}
	free(audio->ring);
	free(audio);
	return result;
}
