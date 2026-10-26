# chrony-log

A small Chrony NTP server that writes client requests as JSON logs.

If you run an NTP server, you may want to know which devices use it, when they last asked for time, and whether their requests were answered or rejected. Chrony's client counters are useful, but they don't give you a history of individual requests.

This image adds request logging to Chrony. Each request reaching the patched handler produces a record with the receive time, client IP and port, processing result, and server synchronization state. You can send these records through Vector to OpenSearch, search for a device, or plot its requests over time.

Logs go to stdout, so your existing container log collector can pick them up. There is no separate database or logging service inside the image. If the log output fills up, Chrony drops log records and counts them rather than delaying NTP responses.

The image uses scratch, runs without root, and serves time without changing the host clock. It is intended for networks where you need both an NTP server and a way to see how clients use it.

When Chrony reports that its clock is not synchronized, client requests receive no reply and are logged as `not_synchronized`. Replies resume when synchronization is restored.


See the [Docker Compose and Kubernetes examples](examples/README.md) to get started.
