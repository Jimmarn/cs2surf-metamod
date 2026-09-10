FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk

WORKDIR /app

RUN apt update -o Acquire::Check-Valid-Until=false && apt install -y git python3-pip
RUN git clone https://github.com/alliedmodders/ambuild
RUN pip install ./ambuild
RUN git config --global --add safe.directory /app
COPY ./docker-entrypoint.sh .
CMD [ "/bin/bash", "./docker-entrypoint.sh" ]
