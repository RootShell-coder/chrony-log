# Running chrony-log

These examples use `ghcr.io/rootshell-coder/chrony-log:latest` and allow NTP requests from `192.168.0.0/24`. Change the subnet to match your network. Port **123/UDP** must be available on the server.

## Docker Compose

Open `examples/docker` and copy the settings file:

```sh
cp .env.example .env
```

Set `ALLOW_CIDR` in `.env` to your client subnet. You can list several subnets separated by spaces. Use `NTP_POOLS` and `NTP_SERVERS` to choose the upstream time sources.

Start the container:

```sh
docker compose pull
docker compose up -d
```

Check the logs and clock status:

```sh
docker compose logs --tail=20 chrony
docker compose exec chrony chronyc -h /tmp/chrony/chronyd.sock tracking
```

The container become healthy once Chrony synchronizes with an upstream server. It serves time to clients without changing the host clock.

## Kubernetes

kubernetes/chrony.yaml creates the chrony namespace, Deployment and Service. Before apply it, change these values:

- `ALLOW_CIDR`: your client subnet.
- `kubernetes.io/hostname`: replace `ntp-node` with your node's hostname.
- `externalIPs`: replace `192.168.0.10` with that node's LAN address.

Run these comands from `examples`. The namespace is created when you apply the manifest.

```sh
kubectl apply -f kubernetes/chrony.yaml
kubectl -n chrony rollout status deployment/chrony
kubectl -n chrony logs deployment/chrony --tail=20
kubectl -n chrony exec deployment/chrony -- /usr/bin/chronyc -h /tmp/chrony/chronyd.sock tracking
```

Clients connect directly to the node's LAN address on UDP/123. Traefik and MetalLB are not needed. The adress must already belong to the node: `externalIPs` does not assign it. If another Service uses the same address and UDP port, remove that port from it first.

Chrony is pinned to that node so `externalTrafficPolicy: Local` can preserve client IPs. If the node goes down, this instance stops serving time.

## Vector

### Log examples

Set `LOG_TIME_OFFSET` to change the time shown in JSON logs. For Moscow use `+0300`, for example:

```yaml
environment:
  LOG_TIME_OFFSET: "+0300"
```

With this setting, `2026-10-26T12:01:09.234567890Z` becomes `2026-10-26T15:01:09.234567890+03:00`. Both refer to the same instant. NTP replies and Chrony's plain-text messages are unchanged. This is a fixed offset, with no daylight-saving adjustments. The default is UTC (`+0000`, written as `Z`). Restart the container after changing it. In Kubernetes set the same variable in `env`.

A successful request:

```json
{
  "timestamp": "2026-10-26T12:01:08.123456789Z",
  "service": "chrony",
  "event": "ntp_request",
  "client_ip": "192.168.0.25",
  "client_port": 51324,
  "request_bytes": 48,
  "result": "response_sent",
  "server_stratum": 2,
  "synchronized": true,
  "sequence": 125,
  "log_dropped_total": 0
}
```

`response_sent` means the response was handed to the network stack. It does not confirm that the client received it.

A request rejected because the client address is not allowed:

```json
{
  "timestamp": "2026-10-26T12:01:09.234567890Z",
  "service": "chrony",
  "event": "ntp_request",
  "client_ip": "192.168.1.25",
  "client_port": 51325,
  "request_bytes": 48,
  "result": "access_denied",
  "server_stratum": 2,
  "synchronized": true,
  "sequence": 126,
  "log_dropped_total": 0
}
```

Here Chrony is synchronized, but it does not answer the client because of the access rules. Other results include `invalid_packet`, `rate_limited` and `response_failed` (the response could not be generated or sent).

If Chrony has not synchronized yet, it sends no NTP reply and logs `result: "not_synchronized"` with `synchronized: false`. Clients time out and can try another server. The check runs on each request, so replies also stop if Chrony later declares its clock unsynchronized. Losing an upstream connection does not necessarily trigger this immediately: Chrony can keep its estimated time valid for a while. This uses Chrony's synchronization state, not a guarantee of a particular clock accuracy.

These are sample records with the default UTC setting, formatted for reading. The container writes each JSON record on a single line. `timestamp` is the receive time according to Chrony's clock, formatted with `LOG_TIME_OFFSET`. If `synchronized` is false, the clock may not yet be accurate. `sequence` increases for each logged request attempt. `log_dropped_total` counts records skipped when the log output cannot accept them. Both counters reset when Chrony restarts.

Startup messages and general Chrony errors remain plain text on stderr. The JSON records describe request processing, not every possible container error.

### Collecting logs

Chrony writes request records as JSON to stdout and service mesages to stderr. Vector can collect both from the container logs.

For Kubernetes, select Pods with `app.kubernetes.io/name=chrony` in your `kubernetes_logs` source. For Docker Compose, use `docker_logs`.

Parse JSON records into a `chrony` field and keep plain-text messages as they are. Request records have `chrony.event` set to `ntp_request`. Check `chrony.client_ip` to confirm that your network setup preserves the original client address.
