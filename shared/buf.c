// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libos/buf.h>
#include <libos/malloc.h>
#include <libos/round.h>
#include <libos/strings.h>

#define LIBOS_BUF_CHUNK_SIZE 1024

void libos_buf_release(libos_buf_t* buf)
{
    if (buf && buf->data)
    {
        libos_memset(buf->data, 0xDD, buf->size);
        libos_free(buf->data);
    }

    libos_memset(buf, 0x00, sizeof(libos_buf_t));
}

int libos_buf_clear(libos_buf_t* buf)
{
    if (!buf)
        return -1;

    buf->size = 0;

    return 0;
}

int libos_buf_reserve(libos_buf_t* buf, size_t cap)
{
    if (!buf)
        return -1;

    /* If capacity is bigger than current capacity */
    if (cap > buf->cap)
    {
        void* new_data;
        size_t new_cap;

        /* Double current capacity (will be zero the first time) */
        new_cap = buf->cap * 2;

        /* If capacity still insufficent, round to multiple of chunk size */
        if (cap > new_cap)
        {
            const size_t N = LIBOS_BUF_CHUNK_SIZE;
            new_cap = (cap + N - 1) / N * N;
        }

        /* Expand allocation */
        if (!(new_data = libos_realloc(buf->data, new_cap)))
            return -1;

        buf->data = new_data;
        buf->cap = new_cap;
    }

    return 0;
}

int libos_buf_resize(libos_buf_t* buf, size_t new_size)
{
    if (!buf)
        return -1;

    if (new_size == 0)
    {
        libos_buf_release(buf);
        libos_memset(buf, 0, sizeof(libos_buf_t));
        return 0;
    }

    if (libos_buf_reserve(buf, new_size) != 0)
        return -1;

    if (new_size > buf->size)
        libos_memset(buf->data + buf->size, 0, new_size - buf->size);

    buf->size = new_size;

    return 0;
}

int libos_buf_append(libos_buf_t* buf, const void* data, size_t size)
{
    size_t new_size;

    /* Check arguments */
    if (!buf || !data)
        return -1;

    /* If zero-sized, then success */
    if (size == 0)
        return 0;

    /* Compute the new size */
    new_size = buf->size + size;

    /* If insufficient capacity to hold new data */
    if (new_size > buf->cap)
    {
        int err;

        if ((err = libos_buf_reserve(buf, new_size)) != 0)
            return err;
    }

    /* Copy the data */
    libos_memcpy(buf->data + buf->size, data, size);
    buf->size = new_size;

    return 0;
}

int libos_buf_insert(
    libos_buf_t* buf,
    size_t pos,
    const void* data,
    size_t size)
{
    int ret = -1;
    size_t rem;

    if (!buf || pos > buf->size)
        goto done;

    if (libos_buf_reserve(buf, buf->size + size) != 0)
        return -1;

    rem = buf->size - pos;

    if (rem)
        libos_memmove(buf->data + pos + size, buf->data + pos, rem);

    if (data)
        libos_memcpy(buf->data + pos, data, size);
    else
        libos_memset(buf->data + pos, 0, size);

    buf->size += size;
    ret = 0;

done:
    return ret;
}

int libos_buf_remove(libos_buf_t* buf, size_t pos, size_t size)
{
    size_t rem;

    if (!buf || pos > buf->size || pos + size > buf->size)
        return -1;

    rem = buf->size - (pos + size);

    if (rem)
        libos_memmove(buf->data + pos, buf->data + pos + size, rem);

    buf->size -= size;

    return 0;
}

int libos_buf_pack_u64(libos_buf_t* buf, uint64_t x)
{
    int ret = -1;
    const size_t n = sizeof(uint64_t);

    if (!buf)
        goto done;

    if (libos_buf_append(buf, &x, n) != 0)
        goto done;

    ret = 0;

done:
    return ret;
}

int libos_buf_unpack_u64(libos_buf_t* buf, uint64_t* x)
{
    int ret = -1;
    size_t r;
    const size_t n = sizeof(uint64_t);

    if (!buf || !x)
        goto done;

    r = buf->size - buf->offset;

    if (r < n)
        goto done;

    libos_memcpy(x, buf->data + buf->offset, n);
    buf->offset += n;

    ret = 0;

done:
    return ret;
}

int libos_buf_pack_bytes(libos_buf_t* buf, const void* p, size_t size)
{
    int ret = -1;
    size_t n;
    size_t align;
    uint8_t align_buf[sizeof(uint64_t)];

    if (!buf || !p)
        goto done;

    /* total size should be a multiple of 8 to guarantee alignment */
    n = libos_round_up_u64(size, sizeof(uint64_t));

    /* calculate how many extra alignment bytes are needed */
    align = n - size;

    /* zero-out part of the alignment bytes array */
    if (align)
        libos_memset(align_buf, 0, align);

    /* append the size */
    if (libos_buf_pack_u64(buf, size) != 0)
        goto done;

    /* append the bytes */
    if (size && libos_buf_append(buf, p, size) != 0)
        goto done;

    /* append the alignment bytes */
    if (align && libos_buf_append(buf, align_buf, align) != 0)
        goto done;

    ret = 0;

done:
    return ret;
}

int libos_buf_unpack_bytes(libos_buf_t* buf, const void** p, size_t* size_out)
{
    int ret = -1;
    size_t size;

    if (!buf || !p || !size_out)
        goto done;

    /* unpack the size of the array */
    if (libos_buf_unpack_u64(buf, &size) != 0)
        goto done;

    /* unpack the array bytes */
    {
        size_t r;
        const size_t n = libos_round_up_u64(size, sizeof(uint64_t));

        r = buf->size - buf->offset;

        if (r < n)
            goto done;

        *p = buf->data + buf->offset;
        buf->offset += n;
    }

    *size_out = size;
    ret = 0;

done:
    return ret;
}

int libos_buf_pack_str(libos_buf_t* buf, const char* str)
{
    int ret = -1;
    size_t len;

    if (!buf || !str)
        goto done;

    len = libos_strlen(str);

    /* pack the characters and the null terminator */
    if (libos_buf_pack_bytes(buf, str, len + 1) != 0)
        goto done;

    ret = 0;

done:
    return ret;
}

int libos_buf_unpack_str(libos_buf_t* buf, const char** str, size_t* len)
{
    int ret = -1;
    size_t size;
    const void* p;

    if (!buf || !str || !len)
        goto done;

    /* unpack the array of charaters */
    if (libos_buf_unpack_bytes(buf, &p, &size) != 0)
        goto done;

    /* a string must have at least one null byte */
    if (size == 0)
        goto done;

    *str = p;
    *len = size - 1;

    ret = 0;

done:
    return ret;
}

/*
** This function serializes an array of pointers to strings into the following
** format:
**
**     <num-strings>(<string-size><string-bytes>+)*
**
** Note that a string will always have at least one null byte.
*/
int libos_buf_pack_strings(
    libos_buf_t* buf,
    const char* strings[],
    size_t count)
{
    int ret = -1;

    if (!buf || !strings)
        goto done;

    /* pack the number of strings */
    if (libos_buf_pack_u64(buf, count) != 0)
        goto done;

    /* pack each of the strings */
    for (size_t i = 0; i < count; i++)
    {
        if (!strings[i])
            goto done;

        if (libos_buf_pack_str(buf, strings[i]) != 0)
            goto done;
    }

    ret = 0;

done:
    return ret;
}

int libos_buf_unpack_strings(
    libos_buf_t* buf,
    const char*** strings_out,
    size_t* count_out)
{
    int ret = -1;
    size_t count;
    const char** strings = NULL;

    if (!buf || !strings_out || !count_out)
        goto done;

    /* unpack the number of strings */
    if (libos_buf_unpack_u64(buf, &count) != 0)
        goto done;

    /* allocate array of pointers to strings */
    if (!(strings = libos_calloc(count + 1, sizeof(char*))))
        goto done;

    /* unpack each of the strings */
    for (size_t i = 0; i < count; i++)
    {
        const char* str;
        size_t len;

        if (libos_buf_unpack_str(buf, &str, &len) != 0)
            goto done;

        strings[i] = str;
    }

    *strings_out = strings;
    *count_out = count;
    strings = NULL;

    ret = 0;

done:

    if (strings)
        libos_free(strings);

    return ret;
}
