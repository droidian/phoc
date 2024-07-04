FROM debian:trixie-slim

# Build-deps for phoc and wlroots
RUN export DEBIAN_FRONTEND=noninteractive \
   && apt-get -y update \
   && apt-get -y install --no-install-recommends wget ca-certificates gnupg eatmydata \
   && eatmydata apt-get -y update \
   && eatmydata apt-get -y dist-upgrade \
   && cd /home/user/app \
   && DEB_BUILD_PROFILES=pkg.phoc.embedwlroots eatmydata apt-get --no-install-recommends -y build-dep . \
   && eatmydata apt-get -y remove --purge libwlroots-dev \
   && eatmydata apt-get clean

# Tools needed for CI jobs
RUN export DEBIAN_FRONTEND=noninteractive \
   && cd /home/user/app \
   && eatmydata apt-get install --no-install-recommends -y git gcovr uncrustify \
   && eatmydata apt-get clean
