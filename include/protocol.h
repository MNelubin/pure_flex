#pragma once

#include <stdio.h>
#include <sys/types.h>

#define MAX_MESSAGE_LEN 256

ssize_t read_message(FILE *stream, void *buf);
ssize_t write_message(FILE* stream, const void *buf, size_t nbyte);
