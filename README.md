# custom-audio-broadcaster

A simple C++ broadcasting library for audio coming from custom sources (e.g. generated by a program).

The library interface allows to:

- create and delete rooms
- get information about rooms
- kick clients from rooms
- publish/unpublish audio streams
- publish/unpublish custom text data

**Note:** This library is still under development and doesn't provide any security mechanism like authentication or encryption.
The library is currently only tested on macOs.

## Installation

The library depends on [gstreamer](https://gstreamer.freedesktop.org/) and [mediamtx](https://github.com/bluenviron/mediamtx)

On macOs we can install them using homebrew:

```bash
brew install gstreamer
brew install mediamtx
```

Clone the repo to your project for example into `external` subdirectory

**Note:** You need to enable http api of mediamtx in its configuration file (api: yes).

## Usage

The following example shows how to broadcast audio from a custom wave generator.

#### Server

```bash
mediamtx # start media server
```

```cpp
#include <iostream>
#include "broadcaster.hpp"
#include "json.hpp"

using json = nlohmann::json;

int main()
{

  //  a broadcaster object that will be used to create and delete rooms and publish audio.
  Broadcaster broadcaster;
  // we create a new room with path "/test", title "Test room", description "This is a test room" and max number of clients 10 (by default there's no limit).
  broadcaster.create_new_room("test", "Test room", "This is a test room", 10);

  // we can get all available rooms
  const auto rooms = broadcaster.get_rooms();
  for (const auto &room : rooms)
  {
    std::cout << room.to_json().dump(2) << std::endl;
  }

  // This is our custom audio generator.
  const auto data_provider = [](uint8_t *buffer, int chunk_size, int sample_rate)
  {
    static float a = 0, b = 1, c = 0, d = 1;
    int num_samples = chunk_size / 2; /* Because each sample is 16 bits */
    int16_t *raw = (int16_t *)buffer;
    c += d;
    d -= c / 1000;
    float freq = 1100 + 1000 * d;
    for (int i = 0; i < num_samples; i++)
    {
      a += b;
      b -= a / freq;
      raw[i] = (int16_t)(500 * a);
    }
    return num_samples;
  };

  // We Add custom text data to the room that can be queried by clients using GET /rooms/<room_path>/data
  broadcaster.publish_text_data("test", [](json &data)
                                { data["greeting"] = "Hello from /test room!";
                                data["goodbye"] = "Goodbye from /test"; });
  broadcaster.publish_audio("test", data_provider, GST_AUDIO_FORMAT_S16);
  int i = 0;
  while (i++ < 100)
  {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "running..." << std::endl;
  }

  // This is how we can unpublish
  // These two will be called automatically when broadcaster is destroyed or when we delete the room.
  broadcaster.unpublish_audio("test");
  broadcaster.unpublish_text_data("test");
  return 0;
}
```

As you can see audio publisher function gets buffer that can be filled with raw data. The generator function needs to return the number of samples it generated (it's needed for timestamping). We can cast the buffer to many different types, but we need to specify appropriate format in the `publish_audio` function. Possible formats resides in `gstreamer/1.22.8_2/include/gstreamer-1.0/gst/audio/audio-format.h`.
for example:
`GST_AUDIO_FORMAT_U16` - unsigned 16 bit
`GST_AUDIO_FORMAT_F32BE` - float 32 bit big endian

Similarly we can publish custom text data that can be queried by clients using GET /rooms/<room_path>/data. The publisher function gets json object that can be filled with data.

###### Build

**Note:** it assumes that the library is cloned into `external` subdir of your project.

```cmake
cmake_minimum_required(VERSION 3.15.3)
project(example)

set(CMAKE_CXX_STANDARD 20)

add_executable(example example.cpp)

add_subdirectory(external/custom-audio-broadcaster)
target_link_libraries(example PRIVATE broadcaster)
```

#### Client

Basically there's one endpoint you want to use: /v1/rooms, to list all available rooms and their urls and metadata.

Let's query the server for all available rooms:

```bash
curl -X GET http://localhost:3000/v1/rooms
```

**Note:** Ip and port are the ones that we specified when creating the broadcaster object earlier.

This is our response:

```javascript
{
  "rooms": [
    {
      "audioUrls": {
        "hls": "http://localhost:8888/test/index.m3u8",
        "rtmp": "rtmp://localhost:1935/test",
        "rtsp": "rtsp://localhost:8554/test",
        "srt": "srt://localhost:8890?streamid=read:test",
        "webrtc": "http://localhost:8889/test"
      },
      "currentClientsNumber": 0,
      "description": "This is a test room",
      "maxClientsNumber": 10,
      "path": "/test",
      "dataUrl": "localhost:3000/v1/rooms/test/data",
      "title": "Test room"
    }
  ]
}
```

Now we can use the `audioUrls` to listen to the audio stream.
To test it without developing client, we can just open it
in the web browser by using the `webrtc` url. We can also use tools like `ffmpeg`, `gstreamer` or any other player that supports above protocols.

```bash
ffplay rtsp://localhost:8554/test # ffmpeg (rtsp)
gst-play-1.0 srt://localhost:8890?streamid=read:test # gstreamer (srt)
```

To get the custom text data we can use the `textDataUrl`:

```bash
curl -X GET http://localhost:3000/v1/rooms/test/data
```

This is our response:

```javascript
{
	"goodbye": "Goodbye from /test",
	"greeting": "Hello from /test room!"
}
```

It may be empty string if no text data is published.

If the given room doesn't exist, the response will be:

```javascript
{
  "errorMessage": "Room does not exist"
}
```

with response code 404.

**Note** Comression used for the audio stream is currently fixed to `opus`.

## License

MIT license (© 2023 seb0xff)
