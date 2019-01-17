# SnapCastC

This program consists of a client and a server component allowing synchronous
audio playback over the network. This Software is heavily inspired by snapcast.
The server obtains its input from a fifo which is fed by any audio player (or
even converter) that supports playing to this fifo.
The server can be controlled by the existing android app.

This Implementation focuses on a small maintainable single-threaded design 
while aiming for good audio quality. After all, music is meant to be enjoyed.
The code currently relies on poll/epoll/timefd/alsa and thus is specific to 
Linux. Patches for other platforms are welcome.

## Non-Goals

### Low Latency

* Latency is induced by snapcast to compensate short network outages. 
* The design of snapcast (reading from an input pipe) induces significant 
  latency. Pipes in Linux by default buffer up to 4MB of data. This is well 
  above 23 seconds worth of audio data for 44100:16:2 format. On my machine I can 
  see that once in a while the input pipe will not be read for multiple seconds. 
  To compensate I am running snapcast with a very large buffer.

### Low CPU Usage

While it is important for clients to run on small devices, audio quality is 
valued above CPU utilization. The smallest target platform is Raspberry PI B.

## Usage
See roadmap for implementation status.

### server
```
Allowed options:
  -h, --help                          Produce help message
  -V, --version                       Show version number
  -v                                  verbose output
  -d                                  debug output
  -p, --port arg (=1704)              Server port *
  --controlPort arg (=1705)           Remote control port *
  -s, --stream arg (=pipe:///tmp/snapfifo?name=default) 
                                      URI of the PCM input stream.
                                      Format: TYPE://host/path?name=NAME
                                      [&codec=CODEC]
                                      [&sampleformat=SAMPLEFORMAT]
  --sampleformat arg (=48000:16:2)    Default sample format *
  -c, --codec arg (=flac)             Default transport codec *
                                      (flac|ogg|pcm)[:options]
                                      Type codec:? to get codec specific options
  --streamBuffer arg (=20)            Default stream read buffer [ms] *
  -b, --buffer arg (=1000)            Buffer [ms]
```
Options marked with (*) are not implemented yet.

I am starting snapserver like this:
```
snapcast-server -b 1000 -s /tmp/snapfifo -s 48000:16:2 -c opus
```


### client
```
Allowed options:
  -h --help                       produce help message
  -V, --version                   show version number
  -v                              verbose output
  -d                              debug output
  -l, --list                      list pcm devices *
  -s, --soundcard arg (=default)  index or name of the soundcard *
  -e, --mstderr                   send metadata to stderr *
  -H, --host arg                  server hostname or ip address
  -p (=1704)                      local port
  -P (1704)                       server port *
  --latency arg (=0)              latency of the soundcard *
  -i, --instance arg (=1)         instance id *
```

Options marked with (*) are not implemented yet.

## Status and Roadmap

* Audio Playback [Working]
* Usage of UDP for transporting media data [Working]
* Support for Opus [patches welcome]
* Synchronous playback retaining audio quality, resample using libsoxr  [Working]
* Support for Snapcast android app [patches welcome]
* implement missing command line options [patches welcome]


## Limitations

* exclusively Linux is supported (patches welcome)
* No avahi support (patches welcome)
* supports a single stream only (patches welcome)

## Building SnapCastC

### Dependencies

    apt install libsoxr-dev libasound2-dev libopus-dev build-essential git libjson-c3-dev
    
### Build

in the root of this project, run:
```
mkdir build
cd build
cmake ..
make -j5
make install
```
to build and install the program.

## Communication Protocol

* clients say hello to servers
* servers maintain a list of clients that have recently checked in
* when playing data, it is sent to each client using unicast UDP
* every hello is replied with the current time
* every data packet has a sequence number
* when a client receives a sequence number n+1 but has not received m it will 
  REQUEST missing packets from the server (we could also check every 100ms for 
  missing packets)


# Collaboation
## Improvements welcome!

Help is much appreciated. Be it testing and opening specific issues, or 
contributing code or ideas - every improvement is welcome. Sometimes there are 
different ideals for structuring changes. Please work with us to keep the 
project maintainable.

If you can improve this project (typos, better wording, restructering, ...)
or even new important aspects, feel free to open a pull request. Please
consider the [Seven Rules](https://chris.beams.io/posts/git-commit/) when 
writing commit messages.

This approach makes reviewing and reasoning about changes a lot easier.

## Packages

* A debian package for x86 and raspberry pi would be very helpful.

## Communication

Some of us are in #snapcast on freenode.

# Thanks!

This project is inspired by [snapcast](https://github.com/badaix/snapcast/).
Its concept of playback from a pipe and the android App are re-used for snapcastc.
Thank you, badaix for this inspiration and the proof that this can be achieved!


