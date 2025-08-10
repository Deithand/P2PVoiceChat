#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "network.h"
#include <opus/opus.h>
#include <portaudio.h>

// --- Settings ---
#define SAMPLE_RATE (48000)
#define FRAMES_PER_BUFFER (480)
#define NUM_CHANNELS (1)
#define LOCAL_PORT (8888)
#define OPUS_BITRATE (64000)
#define MAX_PACKET_SIZE (4000)

typedef float SAMPLE;
typedef std::vector<unsigned char> Packet;

// --- Thread-Safe Queue ---
template <typename T> class ThreadSafeQueue {
public:
  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(value));
    cond_.notify_one();
  }

  bool try_pop(T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      return false;
    }
    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

private:
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
};

// --- Globals ---
std::atomic<bool> keep_running(true);
ThreadSafeQueue<Packet> incoming_queue;
ThreadSafeQueue<Packet> outgoing_queue;
OpusEncoder *encoder;
OpusDecoder *decoder;

// --- Audio Callback ---
static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo *timeInfo,
                      PaStreamCallbackFlags statusFlags, void *userData) {
  // --- Output (Playback) ---
  Packet encoded_packet;
  if (incoming_queue.try_pop(encoded_packet)) {
    int decoded_frames =
        opus_decode_float(decoder, encoded_packet.data(), encoded_packet.size(),
                          (SAMPLE *)outputBuffer, framesPerBuffer, 0);
    if (decoded_frames < 0) {
      std::cerr << "Opus decode failed: " << opus_strerror(decoded_frames)
                << std::endl;
      for (size_t i = 0; i < framesPerBuffer * NUM_CHANNELS; ++i) {
        ((SAMPLE *)outputBuffer)[i] = 0;
      }
    } else if (decoded_frames < (int)framesPerBuffer) {
      std::fill(((SAMPLE *)outputBuffer) + decoded_frames * NUM_CHANNELS,
                ((SAMPLE *)outputBuffer) + framesPerBuffer * NUM_CHANNELS,
                0.0f);
    }
  } else {
    // Jitter: No packet available, play silence
    for (size_t i = 0; i < framesPerBuffer * NUM_CHANNELS; ++i)
      ((SAMPLE *)outputBuffer)[i] = 0;
  }

  // --- Input (Recording) ---
  if (inputBuffer != NULL) {
    Packet new_packet(MAX_PACKET_SIZE);
    opus_int32 encoded_bytes =
        opus_encode_float(encoder, (const SAMPLE *)inputBuffer, framesPerBuffer,
                          new_packet.data(), new_packet.size());
    if (encoded_bytes < 0) {
      std::cerr << "Opus encode failed: " << opus_strerror(encoded_bytes)
                << std::endl;
    } else {
      new_packet.resize(encoded_bytes);
      outgoing_queue.push(std::move(new_packet));
    }
  }

  return paContinue;
}

// --- Network Threads ---
void network_receive_loop(int sockfd) {
  char buffer[MAX_PACKET_SIZE];
  std::string from_ip;
  int from_port;
  while (keep_running) {
    ssize_t n =
        network_receive(sockfd, buffer, MAX_PACKET_SIZE, from_ip, from_port);
    if (n > 0) {
      Packet p(buffer, buffer + n);
      incoming_queue.push(std::move(p));
    } else if (n < 0 && keep_running) {
      std::cerr << "Network receive error." << std::endl;
      // Add a small sleep to prevent busy-looping on error
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

void network_send_loop(int sockfd, const std::string &peer_ip, int peer_port) {
  Packet p;
  while (keep_running) {
    if (outgoing_queue.try_pop(p)) {
      network_send(sockfd, peer_ip, peer_port, (const char *)p.data(),
                   p.size());
    } else {
      // Wait a bit if the queue is empty
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

// --- Main ---
int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <peer_ip> <peer_port>" << std::endl;
    return 1;
  }
  std::string peer_ip = argv[1];
  int peer_port = std::stoi(argv[2]);

  // --- Init Opus ---
  int opus_error;
  encoder = opus_encoder_create(SAMPLE_RATE, NUM_CHANNELS,
                                OPUS_APPLICATION_VOIP, &opus_error);
  if (opus_error != OPUS_OK) {
    std::cerr << "Failed to create Opus encoder" << std::endl;
    return 1;
  }
  opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE));

  decoder = opus_decoder_create(SAMPLE_RATE, NUM_CHANNELS, &opus_error);
  if (opus_error != OPUS_OK) {
    std::cerr << "Failed to create Opus decoder" << std::endl;
    return 1;
  }
  std::cout << "Opus initialized." << std::endl;

  // --- Init Network ---
  int sockfd = network_init(LOCAL_PORT);
  if (sockfd < 0) {
    return 1;
  }
  std::cout << "Network initialized on port " << LOCAL_PORT << std::endl;

  // --- Init PortAudio ---
  PaStream *stream;
  if (Pa_Initialize() != paNoError) {
    std::cerr << "PortAudio init failed." << std::endl;
    return 1;
  }
  if (Pa_OpenDefaultStream(&stream, NUM_CHANNELS, NUM_CHANNELS, paFloat32,
                           SAMPLE_RATE, FRAMES_PER_BUFFER, paCallback,
                           NULL) != paNoError) {
    std::cerr << "PortAudio open stream failed." << std::endl;
    return 1;
  }
  std::cout << "PortAudio initialized." << std::endl;

  // --- Start everything ---
  std::thread receive_thread(network_receive_loop, sockfd);
  std::thread send_thread(network_send_loop, sockfd, peer_ip, peer_port);
  if (Pa_StartStream(stream) != paNoError) {
    std::cerr << "PortAudio start stream failed." << std::endl;
    return 1;
  }

  std::cout << "Voice chat running. Press Enter to stop..." << std::endl;
  std::cin.get();
  keep_running = false;

  // --- Cleanup ---
  send_thread.join();
  // To unblock the blocking recvfrom call in the receive thread
  network_send(sockfd, "127.0.0.1", LOCAL_PORT, "q", 1);
  receive_thread.join();

  Pa_StopStream(stream);
  Pa_CloseStream(stream);
  Pa_Terminate();
  network_close(sockfd);
  opus_encoder_destroy(encoder);
  opus_decoder_destroy(decoder);

  std::cout << "Program finished." << std::endl;
  return 0;
}
