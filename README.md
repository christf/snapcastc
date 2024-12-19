# SnapCastC

[![Packagecloud.io Debian](https://img.shields.io/badge/deb-packagecloud.io-844fec.svg)](https://packagecloud.io)
[![Build Status](https://api.travis-ci.org/christf/snapcastc.svg?branch=master)](https://travis-ci.org/christf/snapcastc)

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

On this device which is clocked to 800Mhz, when using Opus, snapcastc-client 
utilizes 14% CPU. snapcastc-server utilizes 36% CPU during regular playback for 
a single stream. When starting a stream the buffers are filled and 
snapcastc-server will max out a single core.

This means there a single Pi can run snapcastc-server, snapcastc-client and 
there is barely enough room for mpd - but nothing more. Once the buffers are 
filled, the pi runs with 85-90%% CPU usage



## Installation

Once available, stable packages will be pushed to the OSS packagecloud repository:
```
curl -s https://packagecloud.io/install/repositories/christf/OSS/script.deb.sh | sudo bash
```

Until that time, you are welcome to use the development packages for debian and 
raspbian from packagecloud.

```
curl -s https://packagecloud.io/install/repositories/christf/dev/script.deb.sh | sudo bash
```

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
audio_output {
        type            "pipe"
        name            "snapcast"
        command         "cat >/tmp/snapfifo"
        format          "48000:16:2"
        auto_resample   "no"
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
  -b, --buffer arg (=1000)            Read ahead buffer before playing [ms]
```
Options marked with (*) are not implemented yet.

I am starting snapserver like this:
```
snapcast-server -b 25000 -s "pipe:///tmp/snapfifo?buffer_ms=120&codec=opus&name=default&sampleformat=48000:16:2" -p 1704
```

### Client
```
Allowed options:
  -h --help                       produce help message
  -V, --version                   show version number
  -v                              verbose output
  -d                              debug output
  -l                              list pcm devices
  -m <control>                    name of the mixer device
  -c <card>                       sound device on which the mixer lives eg "hw:1"
  -s <playback_device>            index or name of the soundcard
  -H, --host arg                  server hostname or ip address
  -p (=1704)                      server port
  -L arg (=0)                     latency of the client in ms
  -i, --instance arg (=1)         instance id
```

I am starting the client like this:
```
snapcast-client -H <hostname-of-server> -p <port of server - 1704> -s default
```

### Separate volume control

Alsa allows to create custom volume controls. This can be achieved by editing /etc/asound.conf
```
Pcm.snapclient {
    Type            softvol
    Slave.pcm       "asymed"
    Control.name    "snapclient"
    Control.card    1
}
```
to use this control, use the parameters:
```
-m snapclient
-c hw:1
-s snapclient
```

### Scripts

The scripts directory contains a set of example api calls to mute/unmute 
clients, set their volume and move clients to different streams to illustrate 
API usage and provide scripts to be integrated into your home automation.


## Testing

For testing, the following commands can be executed to generate some audio

```
mkfifo /tmp/snapfifo
sox input.ogg -r 48000 -t raw -b 16 -e signed -c 2 /tmp/snapfifo
```
In a different terminal, start
```
./src/snapcast-server -s "pipe:///tmp/snapfifo?name=default&codec=opus&sampleformat=48000:16:2&buffer_ms=120"  -b 30000 -p 1704 -v
```

And yet in another terminal, start
```
./build/src/snapcast-client -H localhost
```


## Status and Roadmap


* Audio Playback [Working]
* Usage of UDP for transporting media data [Working]
* Support for Opus [Working]
* Synchronous playback by dropping / inserting single frames  [Working]
* Synchronous playback by time stretching audio chunks using libsoxr [Working]
* Synchronous playback by time stretching audio chunks using librubberband while maintaining pitch [patches welcome]
* Support for Snapcast andoid app [patches welcome]
* implement missing command line options [patches welcome]


## Limitations

* The Audio player will have to resample to a fixed sample rate.
* exclusively Linux is supported (patches welcome)

## Building SnapCastC

The included Dockerfile provides a build environment, that is used for the 
travis-ci integration. See the script section of .travis.yml on how to run it.
Of course, this can be used to build SnapCastC on your own infrastructure.

### Dependencies

    apt install libasound2 libopus0 libsoxr0 libjson-c3 librubberband2
    
### Build

    apt install cmake libsoxr-dev librubberband-dev libasound2-dev libopus-dev build-essential git libjson-c-dev

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
* servers maintain a list of clients that have recently said hello
* when playing data, it is sent to each client using unicast UDP
* every data packet has a sequence number
* when a client receives a sequence number n+1 but has not received m it will 
  REQUEST missing packets from the server


# FHEM Integration

To integrate SnapCastC with fhem mute button and volume sliders can be defined:
The two snapcast clients are started using -i 2 and -i 3 respectively to hard-code their ID.
```
define Musik_Wohnzimmer dummy
Attr Musik_Wohnzimmer devStateIcon on:audio_volume_high off:audio_volume_mute .on:audio_volume_high
Attr Musik_Wohnzimmer icon audio_volume
Attr Musik_Wohnzimmer room Musik
Attr Musik_Wohnzimmer setList on off
Define off_Musik_Wohnzimmer notify Musik_Wohnzimmer:off {system("snapclient_mute 3 true")}
Define on_Musik_Wohnzimmer notify Musik_Wohnzimmer:on {system("snapclient_mute 3 false")}

Define Musik_Kueche dummy
Attr Musik_Kueche devStateIcon on:audio_volume_high off:audio_volume_mute
Attr Musik_Kueche room Musik
Attr Musik_Kueche setList on off
Define off_Musik_Kueche notify Musik_Kueche:off {system("snapclient_mute 2 true")}
Define on_Musik_Kueche notify Musik_Kueche:on {system("snapclient_mute 2 false")}

Define Lautstaerke_Kueche dummy
Attr Lautstaerke_Kueche icon audio_audio
Attr Lautstaerke_Kueche readingList state
Attr Lautstaerke_Kueche room Musik
Attr Lautstaerke_Kueche setList state:slider,0,2,100
Attr Lautstaerke_Kueche webCmd state
Define n_Lautstaerke_Kueche notify Lautstaerke_Kueche {system("/usr/local/bin/snapclient_setvolume 2 $EVENT")}

Define Lautstaerke_Wohnzimmer dummy
Attr Lautstaerke_Wohnzimmer icon audio_audio
Attr Lautstaerke_Wohnzimmer readingList state
Attr Lautstaerke_Wohnzimmer room Musik
Attr Lautstaerke_Wohnzimmer setList state:slider,0,2,100
Attr Lautstaerke_Wohnzimmer webCmd state
Define n_Lautstaerke_Wohnzimmer notify Lautstaerke_Wohnzimmer {system("/usr/local/bin/snapclient_setvolume 3 $EVENT")}

```



# Collaboration
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

## Communication

Some of us are in #snapcastc on freenode.

# Thanks!

This project is inspired by [snapcast](https://github.com/badaix/snapcast/).
Its concept of playback from a pipe and the android App are re-used for snapcastc.
Thank you, badaix for this inspiration and the proof that this can be achieved!


