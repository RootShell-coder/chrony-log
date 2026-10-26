import json
import datetime
import os
import pathlib
import socket
import subprocess
import tempfile
import time
import uuid
import struct
import threading


def docker(*args):
    return subprocess.check_output(["docker", *args], text=True).strip()


name = "chrony-log-test-" + uuid.uuid4().hex[:8]
offset = os.environ.get("TEST_LOG_TIME_OFFSET", "+0000")
upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
upstream.bind(("0.0.0.0", 0))
upstream.settimeout(0.2)
enabled = threading.Event()
finished = threading.Event()
gateway = docker("network", "inspect", "bridge", "-f", "{{(index .IPAM.Config 0).Gateway}}")


def ntp_timestamp(now):
    seconds = now + 2208988800
    return struct.pack("!II", int(seconds), int((seconds % 1) * 2**32))


def serve_upstream():
    while not finished.is_set():
        try:
            request, peer = upstream.recvfrom(1024)
        except socket.timeout:
            continue
        if not enabled.is_set() or len(request) < 48:
            continue
        now = time.time()
        reply = bytearray(48)
        reply[:4] = bytes([0x24, 1, 0, 236])
        reply[8:12] = struct.pack("!I", 1)
        reply[12:16] = b"GPS\x00"
        reply[16:24] = ntp_timestamp(now - 1)
        reply[24:32] = request[40:48]
        reply[32:40] = ntp_timestamp(now)
        reply[40:48] = ntp_timestamp(time.time())
        upstream.sendto(reply, peer)


worker = threading.Thread(target=serve_upstream, daemon=True)
worker.start()
with tempfile.TemporaryDirectory() as directory:
    config = pathlib.Path(directory) / "chrony.conf"
    config.write_text(f"server {gateway} port {upstream.getsockname()[1]} iburst minpoll 0 maxpoll 0 minsamples 1 maxsamples 4\n"
                      "port 123\nallow 0.0.0.0/0\n"
                      "pidfile /tmp/chrony/chronyd.pid\n"
                      "bindcmdaddress /tmp/chrony/chronyd.sock\ncmdport 0\n")
    try:
        docker("run", "-d", "--name", name, "--read-only", "--cap-drop=ALL",
               "--security-opt=no-new-privileges:true",
               "--sysctl=net.ipv4.ip_unprivileged_port_start=123",
               "--tmpfs=/tmp:rw,nosuid,nodev,noexec,mode=1777",
               "-e", "LOG_TIME_OFFSET=" + offset,
               "-v", str(config) + ":/etc/chrony/chrony.conf:ro", "chrony-log:latest")
        address = docker("inspect", "-f", "{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}", name)
        packet = b"\x23" + bytes(47)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.connect((address, 123))
            client.settimeout(1)
            time.sleep(0.3)
            client.sendto(packet, (address, 123))
            try:
                client.recvfrom(1024)
                raise AssertionError("Unsynchronized server replied")
            except TimeoutError:
                pass
            enabled.set()
            for attempt in range(30):
                client.sendto(packet, (address, 123))
                try:
                    response, _ = client.recvfrom(1024)
                    assert len(response) >= 48 and response[0] & 7 == 4
                    response_time = int.from_bytes(response[40:44], "big") - 2208988800
                    break
                except TimeoutError:
                    if attempt == 29:
                        raise
            client.sendto(b"\x3b" + bytes(47), (address, 123))
            time.sleep(0.2)
            docker("exec", name, "chronyc", "-h", "/tmp/chrony/chronyd.sock",
                   "deny", client.getsockname()[0])
            client.sendto(packet, (address, 123))
        time.sleep(0.3)
        lines = docker("logs", name).splitlines()
        records = [json.loads(line) for line in lines if line.startswith("{")]
        outcomes = {record["result"] for record in records}
        assert {"not_synchronized", "response_sent", "invalid_packet", "access_denied"} <= outcomes, outcomes
        assert all(record["synchronized"] for record in records if record["result"] == "response_sent")
        assert all(record["log_dropped_total"] == 0 for record in records)
        expected_offset = (int(offset[1:3]) * 60 + int(offset[3:5])) * 60
        if offset[0] == "-":
            expected_offset *= -1
        for record in records:
            stamp = datetime.datetime.fromisoformat(record["timestamp"].replace("Z", "+00:00"))
            assert stamp.utcoffset().total_seconds() == expected_offset
            assert abs(stamp.timestamp() - response_time) < 40
        docker("stop", "-t", "5", name)
        assert docker("inspect", "-f", "{{.State.ExitCode}}", name) == "0"
        print(json.dumps(records, indent=2))
        print("PASS: unsynchronized silence, synchronized reply, invalid packet, ACL rejection, nonroot, SIGTERM")
    finally:
        subprocess.run(["docker", "rm", "-f", name], check=False, capture_output=True)
        finished.set()
        worker.join(timeout=1)
        upstream.close()
