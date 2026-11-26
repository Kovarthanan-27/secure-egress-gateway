# No apt-get anywhere: the official gcc image already contains g++ and make,
# and the JSON header is vendored in third_party/. Only Docker Hub access is needed.
# ---- build stage -------------------------------------------------------------
FROM gcc:12 AS build
WORKDIR /src
COPY Makefile .
COPY third_party ./third_party
COPY src ./src
RUN make

# ---- runtime stage: same Debian release (bookworm) as gcc:12, non-root user ----
FROM debian:12-slim
RUN useradd --system --no-create-home proxy \
 && mkdir -p /data /app/static && chown proxy /data
WORKDIR /app
COPY --from=build /src/sgw-proxy .
COPY static ./static
USER proxy
EXPOSE 8080
ENTRYPOINT ["./sgw-proxy"]
