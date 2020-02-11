FROM debian

ENV BUILD_DEPS $(perl -ne 'next if /^#/; $p=(s/^Build-Depends:\s*/ / or (/^ / and $p)); s/,|\n|\([^)]+\)//mg; print if $p' < debian/control)

RUN apt-get update && apt-get install -y $BUILD_DEPS devscripts

CMD ['make']

