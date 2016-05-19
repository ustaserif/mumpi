#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <thread>
#include <log4cpp/Category.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <portaudio.h>
#include <mumlib/Transport.hpp>
#include "MumpiCallback.hpp"
#include "RingBuffer.hpp"


#define SAMPLE_RATE (48000)
#define NUM_CHANNELS (1)

static bool mumble_thread_run_flag = true;
static bool input_consumer_thread_run_flag = true;

/**
 * Signal interrupt handler
 *
 * @param signal the signal
 */
static void sigHandler(int signal) {
	printf("received signal %d\n", signal);
	mumble_thread_run_flag = false;	// trigger running threads to stop
}

/**
 * Simple data structure for storing audio sample data
 */
struct PaData {
	std::shared_ptr<RingBuffer<int16_t>> buf;
};

/**
 * Record callback for PortAudio engine. This gets called when audio input is
 * available. This will simply take the data from the input buffer and store
 * it in a ring buffer that we allocated. That data will then be consumed by
 * another thread.
 *
 * @param  inputBuffer     [description]
 * @param  outputBuffer    [description]
 * @param  framesPerBuffer [description]
 * @param  timeInfo        [description]
 * @param  statusFlags     [description]
 * @param  userData        [description]
 * @return                 [description]
 */
static int paRecordCallback(const void *inputBuffer,
                            void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData ) {
//	printf("trace1\n");
	(void) outputBuffer;
	(void) timeInfo;
	(void) statusFlags;
	int result = paContinue;
	// cast the pointers to the appropriate types
	const PaData *pa_data = (const PaData*) userData;
	int16_t *input_buffer = (int16_t*) inputBuffer;

	if(inputBuffer != NULL) {
//		printf("Filling buf with %d bytes\n", framesPerBuffer * NUM_CHANNELS);
		// fill ring buffer with samples
		pa_data->buf->push(input_buffer, 0, framesPerBuffer * NUM_CHANNELS);
		// for(int i = 0; i < framesPerBuffer * NUM_CHANNELS; i += NUM_CHANNELS) {
		// 	pa_data->buf->push(input_buffer[i]);	// channel 1
		// 	pa_data->buf->push(input_buffer[i+1]);	// channel 2
		// }
	} else {
		// fill ring buffer with silence
		printf("Filling buf with silence\n");
		for(int i = 0; i < framesPerBuffer * NUM_CHANNELS; i += NUM_CHANNELS) {
			pa_data->buf->push(0);	// channel 1
			pa_data->buf->push(0);	// channel 2
		}
	}

	return result;
}

/**
 * Output callback for PortAudio engine. This gets called when audio output is
 * ready to be sent. This will simply consume data from a ring buffer that we
 * allocated which is being filled by another thread.
 *
 * @param  inputBuffer     [description]
 * @param  outputBuffer    [description]
 * @param  framesPerBuffer [description]
 * @param  timeInfo        [description]
 * @param  statusFlags     [description]
 * @param  userData        [description]
 * @return                 [description]
 */
static int paOutputCallback(const void *inputBuffer,
                            void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void *userData ) {
	int result = paContinue;
	(void) inputBuffer;
	(void) outputBuffer;
	(void) timeInfo;
	(void) framesPerBuffer;
	(void) statusFlags;
	(void) userData;
	return result;
}

static unsigned nextPowerOf2(unsigned val) {
    val--;
    val = (val >> 1) | val;
    val = (val >> 2) | val;
    val = (val >> 4) | val;
    val = (val >> 8) | val;
    val = (val >> 16) | val;
    return ++val;
}

/**
 * Displays usage
 */
void help() {
	printf("mumpi - Simple mumble client daemon for the RaspberryPi\n\n");
	printf("Usage:\n");
	printf("mumpi (-s|--server) string (-u|--username) string (-c|--cert) "
	       "string [(-h|--help)] [(-v|--verbose)]\n\n");
	printf("Options:\n");
	printf("-h, --help                Displays this information.\n");
	printf("-v, --verbose             Verbose mode on.\n");
	printf("-s, --server <string>     mumble server IP:PORT. Required.\n");
	printf("-u, --username <username> username. Required.\n");
	exit(1);
}

int main(int argc, char *argv[]) {
	// Program flow:
	// parse cmd line args
	// init audio device using PortAudio or OpenAL
	// init gpio driver
	// init mumlib
	// connect to mumble server
	//


	//TODO: Use Strings instead of cstrings
	// parse command line args using getopt

	log4cpp::Appender *appender = new log4cpp::OstreamAppender("console", &std::cout);
	appender->setLayout(new log4cpp::BasicLayout());

	log4cpp::Category& logger = log4cpp::Category::getRoot();
	logger.setPriority(log4cpp::Priority::WARN);
	logger.addAppender(appender);

	std::vector<std::thread> thread_pool;
	bool verbose = false;
	std::string server;
	std::string username;
	int next_option;
	const char* const short_options = "hvs:u:";
	const struct option long_options[] =
	{
		{ "help", 0, NULL, 'h' },
		{ "verbose", 0, NULL, 'v' },
		{ "server", 1, NULL, 's' },
		{ "username", 1, NULL, 'u' },
		{ NULL, 0, NULL, 0 }
	};

	// parse options
	while(1) {
		// obtain a option
		next_option = getopt_long(argc, argv, short_options, long_options, NULL);

		if(next_option == -1)
			break;  // no more options

		switch(next_option) {

		case 'h':      // -h or --help
			help();
			break;

		case 'v':      // -v or --verbose
			verbose = true;
			break;

		case 's':      // -s or --server
			server = std::string(optarg);
			break;

		case 'u':      // -u or --username
			username = std::string(optarg);
			break;

		case '?':      // Invalid option
			help();

		case -1:      // No more options
			break;

		default:      // shouldn't happen :-)
			return(1);
		}
	}

	if(verbose)
		logger.setPriority(log4cpp::Priority::INFO);

	// check for mandatory arguments
	if(server.empty() || username.empty()) {
		logger.error("Mandatory arguments not specified");
		help();
	}

	logger.info("Server:        %s", server.c_str());
	logger.info("Username:      %s", username.c_str());

	///////////////////////
	// init audio library
	///////////////////////
	PaError err;
	err = Pa_Initialize();
	if(err != paNoError) {
		logger.error("PortAudio error: %s", Pa_GetErrorText(err));
		exit(-1);
	}

	logger.info("%s", Pa_GetVersionText());

	// init audio I/O stream
	PaStream *stream;
	PaData data;

	// ring buffer size to about 500ms
	const size_t MAX_SAMPLES = nextPowerOf2(0.5 * SAMPLE_RATE * NUM_CHANNELS);
	data.buf = std::make_shared<RingBuffer<int16_t>>(MAX_SAMPLES);

	PaStreamParameters  inputParameters;
	inputParameters.device = Pa_GetDefaultInputDevice();
	if (inputParameters.device == paNoDevice) {
		logger.error("Error: No default input device.");
		exit(-1);
	}
	inputParameters.channelCount = NUM_CHANNELS;
	inputParameters.sampleFormat = paInt16;
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
	inputParameters.hostApiSpecificStreamInfo = NULL;

	logger.info("Opening stream...");
	err = Pa_OpenStream(&stream,
						&inputParameters,
						NULL,                  /* &outputParameters, */
						SAMPLE_RATE,
						512,
						paClipOff,      /* we won't output out of range samples so don't bother clipping them */
						paRecordCallback,
						&data );
//	err = Pa_OpenDefaultStream(&stream,         //
//	                           2,               // 2 input channels
//	                           0,               // 0 output channels
//	                           paInt16,         // 16 bit int output
//	                           SAMPLE_RATE,     // sample rate
//	                           256,             // frames per buffer
//	                           paRecordCallback,// PortAudio callback
//	                           &data);          // data pointer

	if(err != paNoError) {
	    logger.error("PortAudio error: %s", Pa_GetErrorText(err));
		exit(-1);
	}

	logger.info("Opened default stream");
	logger.info("Starting stream...");
	err = Pa_StartStream( stream );
	logger.info("Started stream!");

	///////////////////////
	// init gpio library
	///////////////////////
	// when button is pressed, start input audio stream and pipe it to output audio stream
	// when button is released, stop input audio stream

	///////////////////////
	// init mumble library
	///////////////////////
	// open output audio stream, pipe incoming audio PCM data to output audio stream

	// This stuff should be on a separate thread
	MumpiCallback mumble_callback;
	mumlib::MumlibConfiguration conf;
	conf.opusEncoderBitrate = SAMPLE_RATE;
	mumlib::Mumlib mum(mumble_callback, conf);
	mumble_callback.mum = &mum;
	std::thread mumble_thread([&]() {
		while (mumble_thread_run_flag) {
			try {
				logger.info("Connecting to %s", server.c_str());
				mum.connect(server, 64738, username, "");
				mum.run();
				logger.info("yo");
			} catch (mumlib::TransportException &exp) {
				logger.error("TransportException: %s.", exp.what());
				logger.error("Attempting to reconnect in 5 s.");
				std::this_thread::sleep_for(std::chrono::seconds(5));
			}
		}
	});

	std::thread input_consumer_thread([&]() {
		// consumes the data that the input audio thread receives and sends it
		// through mumble client
		// this will continuously read from the input data circular buffer

		const int OPUS_FRAME_SIZE = 960;
		int16_t *outBuf = new int16_t[MAX_SAMPLES];
		while(input_consumer_thread_run_flag) {
			if(!data.buf->isEmpty() && data.buf->getRemaining() >= OPUS_FRAME_SIZE) {
				// do a bulk get and send it through mumble client

				if(mum.getConnectionState() == mumlib::ConnectionState::CONNECTED) {
					// Opus can encode frames of 2.5, 5, 10, 20, 40, or 60 ms
					// the Opus RFC 6716 reccomends  using 20ms frame sizes
					// so at 48k sample rate, 20ms is 960 samples
					const size_t samplesRetrieved = data.buf->top(outBuf, 0, OPUS_FRAME_SIZE);
//					logger.info("Sending %d samples through mumble", samplesRetrieved);
					mum.sendAudioData(outBuf, OPUS_FRAME_SIZE);
				}
			} else {
//				logger.info("No data, sleeping...");
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		delete[] outBuf;
	});

    // init signal handler
	// struct sigaction action;
 //    action.sa_handler = [](int signal) {
 //    	printf("Received signal: %d", signal);
 //    };
 //    action.sa_flags = 0;
 //    sigemptyset (&action.sa_mask);
 //    sigaction (SIGINT, &action, NULL);
 //    sigaction (SIGTERM, &action, NULL);

	///////////////////////
	// CLEAN UP
	///////////////////////

	///////////////////////////
	// clean up mumble library
	///////////////////////////

	mumble_thread.join();
	logger.info("Disconnecting...");
	mum.disconnect();

	///////////////////////////
	// clean up gpio library
	///////////////////////////

	///////////////////////////
	// clean up audio library
	///////////////////////////
	// close input audio stream (if open)
	// close output audio stream
	// terminate audio lib
	// err = Pa_CloseStream(stream);
	// if(err != paNoError) {
	//  logger.info("PortAudio error: %s", Pa_GetErrorText(err));
	//  exit(-1);
	// }
	logger.info("Cleaning up PortAudio...");
	err = Pa_Terminate();
	if(err != paNoError) {
		logger.error("PortAudio error: %s", Pa_GetErrorText(err));
		exit(-1);
	}

	logger.info("Done!");
	return 0;
}