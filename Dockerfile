# Build and runtime use the same Debian release so the binary's glibc matches
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY main.cpp .
RUN g++ -O2 -std=c++17 -o mini-redis main.cpp

# Runtime stage: just the binary, no compiler
FROM debian:bookworm-slim
COPY --from=build /src/mini-redis /usr/local/bin/mini-redis
EXPOSE 6379
CMD ["mini-redis"]
