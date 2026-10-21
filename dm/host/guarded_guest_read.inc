// #253 one guarded host read into guest memory. The writer lease spans the
// complete pread loop; failure and short-read paths release it exactly once.
// The callback is explicit so the owned regression can inject a failure.
static int dm_guarded_guest_read(int fd, void *buf, size_t size, off_t offset,
                                 size_t *out, ssize_t (*read_fn)(int, void *, size_t, off_t)) {
    size_t got = 0;
    dm_guest_host_write_begin(buf, size);
    while (got < size) {
        ssize_t r = read_fn(fd, (uint8_t *)buf + got, size - got, offset + (off_t)got);
        if (r < 0) {
            if (errno == EINTR) continue;
            dm_guest_host_write_end();
            return -1;
        }
        if (r == 0) break;
        got += (size_t)r;
    }
    dm_guest_host_write_end();
    *out = got;
    return 0;
}
