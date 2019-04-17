FROM debian
# RUN apt-get update && apt-get install -y crossbuild-essential-armhf
RUN apt-get update && apt-get install -y  debhelper  librubberband-dev  alsa-utils  gcc  libasound2-dev  libopus-dev  git  libjson-c-dev  libsoxr-dev  cmake 
# RUN dpkg --add-architecture armhf %% RUN apt-get -a=armhf build-dep snapcastc
CMD ['make']
