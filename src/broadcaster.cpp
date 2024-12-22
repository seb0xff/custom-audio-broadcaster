#include "../include/broadcaster.hpp"

using httplib::StatusCode;
using json = nlohmann::json;

nlohmann::json Urls::to_json() const
{
  return {
      {"rtsp", rtsp},
      {"rtmp", rtmp},
      {"hls", hls},
      {"webrtc", webrtc},
      {"srt", srt}};
}

nlohmann::json Client::to_json() const
{
  return {
      {"id", id},
      {"type", type}};
}

nlohmann::json Room::to_json() const
{
  nlohmann::json json;
  json["path"] = path;
  json["title"] = title;
  json["description"] = description;
  json["max_readers"] = max_readers;
  json["urls"] = urls.to_json();
  json["has_audio_data_provider"] = has_audio_data_provider;
  json["has_text_data_provider"] = has_text_data_provider;
  return json;
}

Broadcaster::Broadcaster(const std::string &media_server_api_url, bool start_http_server) : api_client(media_server_api_url)
{
  if (start_http_server)
  {
    this->start_http_server();
  }
}

void Broadcaster::start_http_server(const std::string &ip, int port)
{
  if (!server.is_running())
  {
    server_ip = ip;
    server_port = port;
    server.Get("/v1/rooms", [this](const httplib::Request &, httplib::Response &res)
               {
                  const auto rooms = get_rooms();
                  json response_json;
                  json rooms_json = json::array();
                  for (const auto &room : this->rooms)
                  {
                    const auto clients = get_connected_clients(room.first);
                    const auto room_json = json{
                        {"path", '/' + room.first},
                        {"title", room.second.title},
                        {"description", room.second.description},
                        {"audioUrls", room.second.urls.to_json()},
                        {"dataUrl", "http://" + server_ip + ':' +
                                     std::to_string(server_port) +
                                     "/v1/rooms/" + room.first + "/data"},
                        {"currentClientsNumber", clients.size()},
                        {"maxClientsNumber", room.second.max_readers}};
                    rooms_json.push_back(room_json);
        }
          response_json["rooms"] = rooms_json;
          res.set_content(response_json.dump(), "application/json"); });

    server.Get(R"(/v1/rooms/(\w+)/text)", [this](const httplib::Request &req, httplib::Response &res)
               {
                   const std::string path = req.matches[1];
                   auto data = json{};

                   if (!does_room_exist(path))
                   {
                     data["errorMessage"] = "Room does not exist";
                     res.status = 404;
                   }
                   else
                   {
                     if (rooms.at(path).text_data_provider.has_value())
                     {
                        data["data"] = rooms.at(path).text_data_provider.value()();
                     }
                     else{
                       data["data"] = "";
                     }
                   }
                   // auto data = json{{"message", "Hello from * room!"}};
                   res.set_content(data.dump(), "application/json"); });

    server_thread = std::thread([&]()
                                { server.listen(ip.c_str(), port); });
    server.wait_until_ready();
  }
}

void Broadcaster::stop_http_server()
{
  if (server.is_running())
  {
    server.stop();
    if (server_thread.joinable())
    {
      server_thread.join();
    }
  }
}

void Broadcaster::publish_audio(const std::string &path, const std::function<int(uint8_t *buffer, int chunk_size, int sample_rate)> &data_provider, GstAudioFormat audio_format, int chunk_size, int sample_rate)
{
  if (!does_room_exist(path))
  {
    create_new_room(path);
  }

  rooms[path].pusher = std::move(RtspPusher("rtsp://localhost:8554/" + path, data_provider, audio_format, chunk_size, sample_rate));
  rooms[path].pusher->start();
}

void Broadcaster::unpublish_audio(const std::string &path)
{
  if (!does_room_exist(path))
  {
    return;
  }

  if (rooms.contains(path))
  {
    rooms.at(path).pusher = std::nullopt;
  }
}

void Broadcaster::publish_text(const std::string &path, const std::function<std::string()> &data_provider)
{
  if (!does_room_exist(path))
  {
    create_new_room(path);
  }

  rooms.at(path).text_data_provider = data_provider;
}

void Broadcaster::unpublish_text(const std::string &path)
{
  if (!does_room_exist(path))
  {
    return;
  }

  if (rooms.contains(path))
  {
    rooms.at(path).text_data_provider = std::nullopt;
  }
}

void Broadcaster::kick_client(const std::string &client_id)
{
  for (const auto &room : rooms)
  {
    const auto clients = get_connected_clients(room.first);
    for (const auto &client : clients)
    {
      if (client.id == client_id)
      {
        httplib::Result res;
        if (client.type == "rtspSession")
        {
          res = api_client.Post("/v3/rtspsessions/kick/" + client_id);
        }
        else if (client.type == "rtmpConn")
        {
          res = api_client.Post("/v3/rtmpconns/kick/" + client_id);
        }
        else if (client.type == "webrtcSession")
        {
          res = api_client.Post("/v3/webrtcsessions/kick/" + client_id);
        }
        else if (client.type == "srtConn")
        {
          res = api_client.Post("/v3/srtconns/kick/" + client_id);
        }
        if (!res)
        {
          throw std::runtime_error("Http error: " + httplib::to_string(res.error()));
        }
        if (!(res->status == StatusCode::OK_200))
        {
          throw std::runtime_error(std::to_string(res->status) + " " + json::parse(res->body).value("error", ""));
        }
        return;
      }
    }
  }
  return;
}

std::vector<Room> Broadcaster::get_rooms()
{
  std::vector<Room> rooms = {};
  for (const auto &room : this->rooms)
  {
    rooms.emplace_back(Room{room.first, room.second.title, room.second.description, room.second.max_readers, room.second.urls, room.second.pusher.has_value(), room.second.text_data_provider.has_value()});
  }
  return rooms;
}

std::vector<Client> Broadcaster::get_connected_clients(const std::string &path)
{
  std::vector<Client> clients = {};
  const auto paths_res = api_client.Get("/v3/paths/list");
  if (!paths_res)
  {
    throw std::runtime_error("Http error: " + httplib::to_string(paths_res.error()));
  }
  if (paths_res->status != StatusCode::OK_200)
  {
    throw std::runtime_error(std::to_string(paths_res->status) + " " + json::parse(paths_res->body).value("error", ""));
  }

  try
  {
    const auto parsed_res = json::parse(paths_res->body);
    const auto paths_json = parsed_res.at("items");
    for (const auto &path_json : paths_json)
    {
      if (path_json.at("name") == path)
      {
        for (const auto &reader_json : path_json.at("readers"))
        {
          clients.push_back({reader_json.at("id"), reader_json.at("type")});
        }
        break;
      }
    }
    return clients;
  }
  catch (const std::exception &e)
  {
    throw std::runtime_error("Invalid response json from the media server");
  }
}

bool Broadcaster::does_room_exist(const std::string &path)
{
  return rooms.contains(path);
}

void Broadcaster::create_new_room(const std::string &path, const std::string &title, const std::string &description, int max_readers)
{
  if (does_room_exist(path))
  {
    return;
  }
  if (max_readers < 0)
  {
    throw std::runtime_error("max_readers must be >= 0");
  }

  const auto res = api_client.Post("/v3/config/paths/add/" + path, json{{"sourceOnDemand", false}, {"maxReaders", max_readers}}.dump(), "application/json");

  if (!res)
  {
    throw std::runtime_error("Http error: " + httplib::to_string(res.error()));
  }
  // 400 means that the room already exists
  if (!(res->status == StatusCode::OK_200) && !(res->status == StatusCode::BadRequest_400))
  {
    throw std::runtime_error(std::to_string(res->status) + " " + json::parse(res->body).value("error", ""));
  }

  const auto prefixes_res = api_client.Get("/v3/config/global/get");
  if (!prefixes_res)
  {
    throw std::runtime_error("Http error: " + httplib::to_string(prefixes_res.error()));
  }
  if (prefixes_res->status != StatusCode::OK_200)
  {
    throw std::runtime_error(std::to_string(prefixes_res->status) + " " + json::parse(prefixes_res->body).value("error", ""));
  }

  auto urls = Urls();
  try
  {
    const auto parsed_prefixes = json::parse(prefixes_res->body);
    const auto ip = api_client.host();
    const auto rtsp_prefix = "rtsp://" + ip + static_cast<std::string>(parsed_prefixes.at("rtspAddress")) + "/";
    const auto rtmp_prefix = "rtmp://" + ip + static_cast<std::string>(parsed_prefixes.at("rtmpAddress")) + "/";
    const auto hls_prefix = "http://" + ip + static_cast<std::string>(parsed_prefixes.at("hlsAddress")) + "/";
    const auto hls_postfix = "/index.m3u8";
    const auto webrtc_prefix = "http://" + ip + static_cast<std::string>(parsed_prefixes.at("webrtcAddress")) + "/";
    const auto srt_prefix = "srt://" + ip + static_cast<std::string>(parsed_prefixes.at("srtAddress")) + "?streamid=read:";

    urls.rtsp = rtsp_prefix + path;
    urls.rtmp = rtmp_prefix + path;
    urls.hls = hls_prefix + path + hls_postfix;
    urls.webrtc = webrtc_prefix + path;
    urls.srt = srt_prefix + path;
  }
  catch (const std::exception &e)
  {
    throw std::runtime_error("Invalid json");
  }

  rooms.try_emplace(path, RoomData{title, description, max_readers, urls, std::nullopt, std::nullopt});
}

void Broadcaster::delete_room(const std::string &path)
{
  if (!does_room_exist(path))
  {
    return;
  }

  unpublish_audio(path);
  unpublish_text(path);

  const auto res = api_client.Delete("/v3/config/paths/delete/" + path);
  if (!res)
  {
    throw std::runtime_error("Http error: " + httplib::to_string(res.error()));
  }
  if (!(res->status == StatusCode::OK_200))
  {
    throw std::runtime_error(std::to_string(res->status) + " " + json::parse(res->body).value("error", ""));
  }

  rooms.erase(path);
}

Broadcaster *Broadcaster::set_delete_rooms_in_destructor(bool delete_rooms_in_destructor)
{
  this->delete_rooms_in_destructor = delete_rooms_in_destructor;
  return this;
}

bool Broadcaster::get_delete_rooms_in_destructor() const
{
  return delete_rooms_in_destructor;
}

std::string Broadcaster::get_server_ip() const
{
  return server_ip;
}

int Broadcaster::get_server_port() const
{
  return server_port;
}

Broadcaster::~Broadcaster()
{
  if (delete_rooms_in_destructor)
  {
    try
    {
      const auto rooms = get_rooms();
      for (const auto &room : rooms)
      {
        delete_room(room.path);
      }
    }
    catch (const std::exception &e)
    {
      std::cerr << "Could not delete rooms on the media server\n";
    }
  }

  api_client.stop();
  stop_http_server();
}
