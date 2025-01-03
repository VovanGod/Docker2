# Stage 1
FROM gcc:latest as builder
WORKDIR /app
COPY app/main.c .
RUN gcc -o main main.c -static
RUN chmod +x /app/main 
# изменяет права файла, делает его исполняемым


# Stage 2
FROM alpine
WORKDIR /app
COPY --from=builder /app/main /app/main
RUN apk add --no-cache bash
CMD ["/app/main"]

# Запускать через docker run -it --rm --privileged image_id 
