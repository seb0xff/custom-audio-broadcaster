#ifndef BROADCASTER_HPP
#define BROADCASTER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <stdexcept>
#include <optional>
#include "../external/httplib.h"
#include "../external/json.hpp"
#include "rtsp_pusher.hpp"

using httplib::StatusCode;
using json = nlohmann::json;

struct Urls
{
  std::string rtsp;
  std::string rtmp;
  std::string hls;
  std::string webrtc;
  std::string srt;

  nlohmann::json to_json() const;
};

struct Client
{
  std::string id;
  std::string type;

  nlohmann::json to_json() const;
};

struct Room
{
  std::string path;
  std::string title;
  std::string description;
  int max_readers;
  Urls urls;
  bool has_audio_data_provider;
  bool has_text_data_provider;

  nlohmann::json to_json() const;
};

class Broadcaster
{
public:
  Broadcaster(const std::string &media_server_api_url = "http://localhost:9997", bool start_http_server = true);

  /**
   * After calling this clients can ask for the list of rooms (GET /v1/rooms).
   * If server is already running it won't do anything.
   * @param ip ip of the http server.
   * @param port port of the http server.
   */
  void start_http_server(const std::string &ip = "localhost", int port = 3000);

  /**
   * Stop the http server and wait for it to finish.
   * If server is not running it won't do anything.
   */
  void stop_http_server();

  /**
   * If somethig is already published at the given path it won't do anything.
   * If the room (path) does not exist it will be created using create_new_room() with default parameters.
   * So it may throw the same as the create_new_room() does.
   * @param path where to publish the stream (Note: do not add leading '/' character).
   * @param data_provider function that will write to the buffer to publish. It should return the number of samples written.
   * @param audio_format format of the audio data written by data_provider.
   * @param chunk_size size of the buffer (in bytes) provided to data_provider.
   * @param sample_rate sample rate of the audio data written by data_provider.
   * @return true if the stream was published, false if it already exists.
   */
  void publish_audio(const std::string &path, const std::function<int(uint8_t *buffer, int chunk_size, int sample_rate)> &data_provider, GstAudioFormat audio_format, int chunk_size = 1024, int sample_rate = 44100);

  /**
   * Do nothing if the stream is not published or the room (path) does not exist.
   * @param path where to unpublish stream from (Note: do not add leading '/' character).
   */
  void unpublish_audio(const std::string &path);

  void publish_text(const std::string &path, const std::function<std::string()> &data_provider);

  void unpublish_text(const std::string &path);

  /**
   * It may throw if the media server responds with an error or the response is invalid.
   * @param client_id id of the client to kick.
   */
  void kick_client(const std::string &client_id);

  /**
   * @return list of rooms.
   */
  std::vector<Room> get_rooms();

  /**
   * Asks the media server for the list of currently connected clients.
   * It may throw if the media server responds with an error or the response is invalid.
   * @return list of clients.
   */
  std::vector<Client> get_connected_clients(const std::string &path);

  /**
   * @param path path to check (Note: do not add leading '/' character).
   * @return true if it does exist.
   */
  bool does_room_exist(const std::string &path);

  /**
   * Nothing will happen if the room already exists.
   * It may throw if the media server responds with an error or the response is invalid.
   * @param path  path where the room will be available (Note: do not add leading '/' character).
   * @param max_readers 0 means unlimited.
   */
  void create_new_room(const std::string &path, const std::string &title = "", const std::string &description = "", int max_readers = 0);

  /**
   * It does nothing if the room does not exist.
   * It may throw if the media server responds with an error or the response is invalid.
   * @param path  path to delete (Note: do not add leading '/' character).
   */
  void delete_room(const std::string &path);

  /**
   * Whether to delete rooms on the media server when destructor is called.
   * @return this
   */
  Broadcaster *set_delete_rooms_in_destructor(bool delete_rooms_in_destructor);

  /**
   * @return true if the rooms on the media server will be deleted in the destructor.
   */
  bool get_delete_rooms_in_destructor() const;

  std::string get_server_ip() const;

  int get_server_port() const;

  ~Broadcaster();

private:
  struct RoomData
  {
    std::string title;
    std::string description;
    int max_readers;
    Urls urls;
    std::optional<RtspPusher> pusher;
    std::optional<std::function<std::string()>> text_data_provider;
  };

  httplib::Client api_client;
  httplib::Server server;
  std::string server_ip;
  int server_port;
  std::thread server_thread;
  bool delete_rooms_in_destructor = false;
  std::map<std::string, RoomData> rooms;
};
#endif // BROADCASTER_HPP