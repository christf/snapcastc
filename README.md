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
  latency. Pipes in Linux by default buffer up to 64KB of data. On my machine I can 
  see that once in a while the input pipe will not be read for multiple seconds. 
  To compensate I am running snapcast with a very large buffer.
* That being said, care has been taken to allow immediate playback and pausing 
  when using elastic pipes.

### Low CPU Usage

While it is important for clients to run on small devices, audio quality is 
valued above CPU utilization. The smallest target platform is Raspberry PI B.

### Time Synchronisation

Snapcastc assumes that system clocks are synchronized. Use ntp to achieve that.

## Usage
See roadmap for implementation status.

## Configuring MPD -- use the pipe plugin

MPD has two output plugins that can potentially be used: pipe and fifo.
The fifo plugin will never block mpd at the expense of dropping audio data when 
the fifo is full. This is 64KB on a Linux > 2.6.11
When the server is under significant IO load, this will lead to dropped audio.

The pipe plugin makes the choice the other way around: It will not drop audio 
data but it will block playback in mpd when data cannot be processed. As long 
as blocking does not take longer than the configured buffer of snapcast, it 
will not be audible.

If you are using the fifo output plugin from mpd and playback stops on all 
clients simultaneously after they have been playing for more than 
30 minutes, then consider switching to the following mpd configuration in 
mpd.conf. Also, make sure that the snapfifo is created using mkfifo and 
snapcastc as well as mpd have permission to access it.

```
Audio_output {
        Type            "pipe"
        Name            "snapcast"
        Command         "cat >/tmp/snapfifo"
        Format          "48000:16:2"
        Auto_resample   "no"
}
```

### server
```
Allowed options:
  -h, --help                          Produce help message
  -V, --version                       Show version number
  -v                                  verbose output
  -d                                  debug output
  -p <port>                           Server UDP port (default: 1704)
  -P <port>                           Remote TCP control port (default: 1705)
  -s <file>                           filename of the PCM input stream.
  -c, --codec arg (=opus)             Default transport codec *
                                      (flac*|opus|pcm*)[:options]
  -B <read_ms>                        Default stream read buffer [ms]
  -b, --buffer arg (=1000)            Buffer [ms]
```
Options marked with (*) are not implemented yet.

I am starting snapserver like this:
```
snapcast-server -b 25000 -s /tmp/snapfifo -B 120 -p 1704
```

### Client
```
Allowed options:
  -h --help                       produce help message
  -V, --version                   show version number
  -v                              verbose output
  -d                              debug output
  -l                              list pcm devices
  -s,                             index or name of the soundcard
  -H, --host arg                  server hostname or ip address
  -p (=1704)                      local port
  -P (=1704)                      server port
  -L arg (=0)                     latency of the client in ms
  -i, --instance arg (=1)         instance id
```

I am starting the client like this:
```
snapcast-client -H <hostname-of-server> -p 1705 -P 1704 -s default
```


## Status and Roadmap

* Audio Playback [Working]
* Usage of UDP for transporting media data [Working]
* Support for Opus [Working]
* Synchronous playback by dropping / inserting single frames  [Working]
* Synchronous playback by time stretching audio chunks using librubberband  [patches welcome]
* Support for Snapcast android app [patches welcome]
* implement missing command line options [patches welcome]


## Limitations

* exclusively Linux is supported (patches welcome)
* No avahi support (patches welcome)
* supports a single stream only (patches welcome)

## Building SnapCastC

### Dependencies

    apt install libasound2 libopus libjson-c3 librubberband2
    
### Build

    apt install librubberband-dev libasound2-dev libopus-dev build-essential git libjson-c-dev

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
* every data packet has a sequence number
* when a client receives a sequence number n+1 but has not received m it will 
  REQUEST missing packets from the server


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

Some of us are in #snapcastc on freenode.

# Thanks!

This project is inspired by [snapcast](https://github.com/badaix/snapcast/).
Its concept of playback from a pipe and the android App are re-used for snapcastc.
Thank you, badaix for this inspiration and the proof that this can be achieved!


