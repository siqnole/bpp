# Build stage
FROM debian:bookworm AS builder

# Install build dependencies
# We need all these to compile the bot and its dependencies
RUN apt-get update && apt-get install -y \
    g++ \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    ccache \
    libcairo2-dev \
    libmariadb-dev \
    libssl-dev \
    zlib1g-dev \
    libreadline-dev \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswresample-dev \
    libtesseract-dev \
    tesseract-ocr-eng \
    libfmt-dev \
    && rm -rf /var/lib/apt/lists/*

# Build and install DPP (Discord++)
# This ensures we have the exact version needed
RUN wget -qO dpp.tar.gz https://github.com/brainboxdotcc/DPP/archive/refs/tags/v10.1.4.tar.gz && \
    tar -xf dpp.tar.gz && cd DPP-10.1.4 && \
    cmake -B build -S . -DDPP_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc) && \
    cmake --install build && \
    cd .. && rm -rf DPP-10.1.4 dpp.tar.gz

WORKDIR /build

# Copy the entire source
# (Make sure .dockerignore excludes build folders)
COPY . .

# Build the bot
# Use optimized flags from CMakeLists.txt
RUN rm -rf build && mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Final stage: Runtime
FROM debian:bookworm-slim

# Install runtime dependencies ONLY
RUN apt-get update && apt-get install -y \
    libcairo2 \
    libmariadb3 \
    libssl3 \
    zlib1g \
    libreadline8 \
    libsqlite3-0 \
    libcurl4 \
    libavcodec59 \
    libavformat59 \
    libavutil57 \
    libswresample4 \
    libtesseract5 \
    tesseract-ocr-eng \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy the binary from builder
COPY --from=builder /build/build/discord-bot /usr/local/bin/discord-bot

# Copy DPP shared library from builder
COPY --from=builder /usr/local/lib/libdpp.so /usr/local/lib/libdpp.so
RUN ldconfig

WORKDIR /app

# The bot expects a 'data' directory in its CWD
# We create it here; user should mount their actual data volume to /app/data
RUN mkdir -p /app/data

# Default environment variables
ENV BOT_TOKEN=""
ENV SHARD_COUNT=""

# Run the bot with --no-tui by default as it's running in a container
# (TUI works if run with -it, but for production background use --no-tui is safer)
ENTRYPOINT ["/usr/local/bin/discord-bot"]
CMD ["--no-tui"]
