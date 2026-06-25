# Reproducible build of both modules. Same result on Windows/macOS/Linux.
#
#   docker build -t vitaplaytime .
#   docker run --rm -v "$PWD/dist:/out" vitaplaytime
#
# Produces dist/playtime_k.skprx and dist/playtime_u.suprx.

FROM vitasdk/vitasdk:latest

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

CMD ["sh", "-c", "cp build/kernel/playtime_k.skprx build/user/playtime_u.suprx /out/ && ls -l /out"]
