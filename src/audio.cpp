#include "util/common.hpp"
#include "audio.hpp"


#define DRIVER_BRIDGE -1


namespace rack {


AudioIO::AudioIO() {
#ifdef USE_SDL2_AUDIO
	setDriver(0);
#else
	setDriver(RtAudio::UNSPECIFIED);
#endif
}

AudioIO::~AudioIO() {
	closeStream();
}

std::vector<int> AudioIO::listDrivers() {
	std::vector<int> drivers;
#ifdef USE_SDL2_AUDIO
	drivers.push_back(0);
#else
	std::vector<RtAudio::Api> apis;
	RtAudio::getCompiledApi(apis);
	for (RtAudio::Api api : apis)
		drivers.push_back((int) api);
#endif
	// Add Bridge fake driver
	// drivers.push_back(DRIVER_BRIDGE);
	return drivers;
}

std::string AudioIO::getDriverName(int driver) {
#ifdef USE_SDL2_AUDIO
	if (driver >= 0) {
		const char *driver = SDL_GetCurrentAudioDriver();
		return driver ? driver : "Unknown";
	}
	else if (driver == DRIVER_BRIDGE) return "VCV Bridge";
	else return "Unknown";
#else
	switch (driver) {
		case RtAudio::UNSPECIFIED: return "Unspecified";
		case RtAudio::LINUX_ALSA: return "ALSA";
		case RtAudio::LINUX_PULSE: return "PulseAudio";
		case RtAudio::LINUX_OSS: return "OSS";
		case RtAudio::UNIX_JACK: return "JACK";
		case RtAudio::MACOSX_CORE: return "Core Audio";
		case RtAudio::WINDOWS_WASAPI: return "WASAPI";
		case RtAudio::WINDOWS_ASIO: return "ASIO";
		case RtAudio::WINDOWS_DS: return "DirectSound";
		case RtAudio::RTAUDIO_DUMMY: return "Dummy";
		case DRIVER_BRIDGE: return "VCV Bridge";
		default: return "Unknown";
	}
#endif
}

void AudioIO::setDriver(int driver) {
	// Close driver
	closeStream();
#ifndef USE_SDL2_AUDIO
	if (rtAudio) {
		delete rtAudio;
		rtAudio = NULL;
	}
#endif
	this->driver = 0;

	// Open driver
	if (driver >= 0) {
#ifndef USE_SDL2_AUDIO
		rtAudio = new RtAudio((RtAudio::Api) driver);
		this->driver = (int) rtAudio->getCurrentApi();
#endif
	}
	else if (driver == DRIVER_BRIDGE) {
		// TODO Connect to Bridge
		this->driver = DRIVER_BRIDGE;
	}
}

int AudioIO::getDeviceCount() {
#ifdef USE_SDL2_AUDIO
	if (driver >= 0) {
		return SDL_GetNumAudioDevices(0);
	}
#else
	if (rtAudio) {
		return rtAudio->getDeviceCount();
	}
#endif
	if (driver == DRIVER_BRIDGE) {
		return 16;
	}
	return 0;
}

std::string AudioIO::getDeviceName(int device) {
#ifdef USE_SDL2_AUDIO
	if (driver >= 0 && device >= 0) {
		return SDL_GetAudioDeviceName(device, 0);
	}
#else
	if (rtAudio) {
		try {
			RtAudio::DeviceInfo deviceInfo = rtAudio->getDeviceInfo(device);
			return deviceInfo.name;
		}
		catch (RtAudioError &e) {
			warn("Failed to query RtAudio device: %s", e.what());
		}
	}
#endif
	if (driver == DRIVER_BRIDGE) {
		return stringf("%d", device + 1);
	}
	return "";
}

std::string AudioIO::getDeviceDetail(int device) {
#ifdef USE_SDL2_AUDIO
	if (driver >= 0)
		return getDeviceName(device);
#else
	if (rtAudio) {
		try {
			RtAudio::DeviceInfo deviceInfo = rtAudio->getDeviceInfo(device);
			return stringf("%s (%d in, %d out)", deviceInfo.name.c_str(), deviceInfo.inputChannels, deviceInfo.outputChannels);
		}
		catch (RtAudioError &e) {
			warn("Failed to query RtAudio device: %s", e.what());
		}
	}
#endif
	if (driver == DRIVER_BRIDGE) {
		return stringf("Channel %d", device + 1);
	}
	return "";
}

#ifdef USE_SDL2_AUDIO
static void sdlCallback(void *userdata, Uint8 *stream, int len) {
	AudioIO *audioIO = (AudioIO*) userdata;
	assert(audioIO);
	audioIO->processStream(NULL, (float *) stream, len / sizeof(float) / audioIO->numOutputs);
}
#else
static int rtCallback(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void *userData) {
	AudioIO *audioIO = (AudioIO*) userData;
	assert(audioIO);
	audioIO->processStream((const float *) inputBuffer, (float *) outputBuffer, nFrames);
	return 0;
}
#endif

void AudioIO::openStream() {
	// Close device but remember the current device number
	int device = this->device;
	closeStream();

	if (device < 0)
		return;

#ifdef USE_SDL2_AUDIO
	SDL_AudioSpec desired, obtained;
	desired.freq = sampleRate;
	desired.format = AUDIO_F32SYS;
	desired.channels = 6;
	desired.samples = blockSize;
	desired.callback = sdlCallback;
	desired.userdata = this;

	sdlDevice = SDL_OpenAudioDevice(getDeviceName(device).c_str(), 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE|SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if(sdlDevice == 0) {
		warn("Could not open SDL audio device: %s", SDL_GetError());
		return;
	}

	sampleRate = obtained.freq;
	blockSize = obtained.samples;
	numInputs = 0;
	numOutputs = obtained.channels;

	SDL_PauseAudioDevice(sdlDevice, 0);

	this->device = device;
	onOpenStream();
#else
	if (rtAudio) {
		// Open new device
		RtAudio::DeviceInfo deviceInfo;
		try {
			deviceInfo = rtAudio->getDeviceInfo(device);
		}
		catch (RtAudioError &e) {
			warn("Failed to query RtAudio device: %s", e.what());
			return;
		}

		numOutputs = min(deviceInfo.outputChannels, maxOutputs);
		numInputs = min(deviceInfo.inputChannels, maxInputs);

		if (numOutputs == 0 && numInputs == 0) {
			warn("RtAudio device %d has 0 inputs and 0 outputs");
			return;
		}

		RtAudio::StreamParameters outParameters;
		outParameters.deviceId = device;
		outParameters.nChannels = numOutputs;

		RtAudio::StreamParameters inParameters;
		inParameters.deviceId = device;
		inParameters.nChannels = numInputs;

		RtAudio::StreamOptions options;
		// options.flags |= RTAUDIO_SCHEDULE_REALTIME;

		int closestSampleRate = deviceInfo.preferredSampleRate;
		for (int sr : deviceInfo.sampleRates) {
			if (abs(sr - sampleRate) < abs(closestSampleRate - sampleRate)) {
				closestSampleRate = sr;
			}
		}

		try {
			debug("Opening audio RtAudio device %d", device);
			rtAudio->openStream(
				numOutputs == 0 ? NULL : &outParameters,
				numInputs == 0 ? NULL : &inParameters,
				RTAUDIO_FLOAT32, closestSampleRate, (unsigned int*) &blockSize, &rtCallback, this, &options, NULL);
		}
		catch (RtAudioError &e) {
			warn("Failed to open RtAudio stream: %s", e.what());
			return;
		}

		try {
			debug("Starting RtAudio stream %d", device);
			rtAudio->startStream();
		}
		catch (RtAudioError &e) {
			warn("Failed to start RtAudio stream: %s", e.what());
			return;
		}

		// Update sample rate because this may have changed
		this->sampleRate = rtAudio->getStreamSampleRate();
		this->device = device;
		onOpenStream();
	}
#endif
}

void AudioIO::closeStream() {
#ifdef USE_SDL2_AUDIO
	if (sdlDevice) {
		SDL_CloseAudioDevice(sdlDevice);
	}
#else
	if (rtAudio) {
		if (rtAudio->isStreamRunning()) {
			debug("Stopping RtAudio stream %d", device);
			try {
				rtAudio->stopStream();
			}
			catch (RtAudioError &e) {
				warn("Failed to stop RtAudio stream %s", e.what());
			}
		}
		if (rtAudio->isStreamOpen()) {
			debug("Closing RtAudio stream %d", device);
			try {
				rtAudio->closeStream();
			}
			catch (RtAudioError &e) {
				warn("Failed to close RtAudio stream %s", e.what());
			}
		}
	}
#endif

	// Reset rtAudio settings
	device = -1;
	numOutputs = 0;
	numInputs = 0;
	onCloseStream();
}

bool AudioIO::isActive() {
#ifdef USE_SDL2_AUDIO
	if (sdlDevice)
		return SDL_GetAudioDeviceStatus(sdlDevice) == SDL_AUDIO_PLAYING;
#else
	if (rtAudio)
		return rtAudio->isStreamRunning();
#endif
	// TODO Bridge
	return false;
}


std::vector<int> AudioIO::listSampleRates() {
#ifdef USE_SDL2_AUDIO
	if (driver >= 0) {
		return {44100, 48000, 88200, 96000, 176400, 192000};
	}
#else
	if (rtAudio) {
		try {
			RtAudio::DeviceInfo deviceInfo = rtAudio->getDeviceInfo(device);
			std::vector<int> sampleRates(deviceInfo.sampleRates.begin(), deviceInfo.sampleRates.end());
			return sampleRates;
		}
		catch (RtAudioError &e) {
			warn("Failed to query RtAudio device: %s", e.what());
		}
	}
#endif
	if (driver == DRIVER_BRIDGE) {
		return {44100, 48000, 88200, 96000, 176400, 192000};
	}

	return {};
}

json_t *AudioIO::toJson() {
	json_t *rootJ = json_object();
	json_object_set_new(rootJ, "driver", json_integer(driver));
	std::string deviceName = getDeviceName(device);
	json_object_set_new(rootJ, "deviceName", json_string(deviceName.c_str()));
	json_object_set_new(rootJ, "sampleRate", json_integer(sampleRate));
	json_object_set_new(rootJ, "blockSize", json_integer(blockSize));
	return rootJ;
}

void AudioIO::fromJson(json_t *rootJ) {
	json_t *driverJ = json_object_get(rootJ, "driver");
	if (driverJ)
		setDriver(json_number_value(driverJ));

	json_t *deviceNameJ = json_object_get(rootJ, "deviceName");
	if (deviceNameJ) {
		std::string deviceName = json_string_value(deviceNameJ);
		// Search for device ID with equal name
		for (int device = 0; device < getDeviceCount(); device++) {
			if (getDeviceName(device) == deviceName) {
				this->device = device;
				break;
			}
		}
	}

	json_t *sampleRateJ = json_object_get(rootJ, "sampleRate");
	if (sampleRateJ)
		sampleRate = json_integer_value(sampleRateJ);

	json_t *blockSizeJ = json_object_get(rootJ, "blockSize");
	if (blockSizeJ)
		blockSize = json_integer_value(blockSizeJ);

	openStream();
}


} // namespace rack
