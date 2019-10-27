FROM debian

RUN apt-get update && apt-get install -y  librubberband-dev alsa-utils gcc libasound2-dev libopus-dev build-essential git libjson-c-dev libsoxr-dev cmake devscripts

CMD ['make']

