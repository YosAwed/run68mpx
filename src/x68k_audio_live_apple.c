#include "x68k_audio_live_apple.h"

#include <AudioUnit/AudioUnit.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
	LIVE_RING_FRAMES = 8192,
	LIVE_CHANNELS = 2,
	LIVE_WAIT_LIMIT = 2000
};

struct X68K_LIVE_AUDIO {
	AudioUnit unit;
	int16_t *ring;
	uint32_t source_sample_rate;
	_Atomic uint64_t read_position;
	_Atomic uint64_t write_position;
	_Atomic uint64_t rendered_frames;
	_Atomic uint32_t callbacks;
	uint64_t output_frames;
	uint64_t nonzero_samples;
	uint16_t peak;
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
	for (frame = 0; frame < copy_frames; ++frame) {
		uint64_t source = (read_position + frame) % LIVE_RING_FRAMES;

		destination[frame * 2] = audio->ring[source * 2];
		destination[frame * 2 + 1] = audio->ring[source * 2 + 1];
	}
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

static int write_output_frame(X68K_LIVE_AUDIO *audio, int16_t left,
	int16_t right)
{
	static const struct timespec wait_time = {0, 1000000};
	uint64_t write_position;
	uint64_t read_position;
	uint16_t magnitude;
	int waits = 0;

	for (;;) {
		write_position = atomic_load_explicit(&audio->write_position,
		                                      memory_order_relaxed);
		read_position = atomic_load_explicit(&audio->read_position,
		                                     memory_order_acquire);
		if (write_position - read_position < LIVE_RING_FRAMES)
			break;
		if (++waits >= LIVE_WAIT_LIMIT) {
			fprintf(stderr, "AudioUnit output stopped consuming samples\n");
			return -1;
		}
		(void)nanosleep(&wait_time, NULL);
	}
	audio->ring[(write_position % LIVE_RING_FRAMES) * 2] = left;
	audio->ring[(write_position % LIVE_RING_FRAMES) * 2 + 1] = right;
	atomic_store_explicit(&audio->write_position, write_position + 1,
	                      memory_order_release);
	audio->output_frames++;
	if (left != 0)
		audio->nonzero_samples++;
	if (right != 0)
		audio->nonzero_samples++;
	magnitude = left == INT16_MIN ? 32768u :
	            (uint16_t)(left < 0 ? -left : left);
	if (magnitude > audio->peak)
		audio->peak = magnitude;
	magnitude = right == INT16_MIN ? 32768u :
	            (uint16_t)(right < 0 ? -right : right);
	if (magnitude > audio->peak)
		audio->peak = magnitude;
	return 0;
}

X68K_LIVE_AUDIO *x68k_live_audio_create(uint32_t source_sample_rate)
{
	AudioComponentDescription description;
	AudioComponent component;
	AudioStreamBasicDescription format;
	AURenderCallbackStruct callback;
	X68K_LIVE_AUDIO *audio;
	OSStatus status;

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
	memset(&format, 0, sizeof(format));
	format.mSampleRate = source_sample_rate;
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
	if (status == noErr)
		status = AudioOutputUnitStart(audio->unit);
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
	size_t index;

	if (audio == NULL || samples == NULL)
		return -1;
	for (index = 0; index < frames; ++index) {
		if (write_output_frame(audio, samples[index * 2],
		                       samples[index * 2 + 1]) != 0)
			return -1;
	}
	return 0;
}

int x68k_live_audio_destroy(X68K_LIVE_AUDIO *audio,
	X68K_LIVE_AUDIO_STATS *stats)
{
	int result = 0;

	if (audio == NULL)
		return 0;
	if (AudioOutputUnitStop(audio->unit) != noErr)
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
