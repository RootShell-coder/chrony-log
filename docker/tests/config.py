import subprocess


def run(env=(), expected=0):
    args = ["docker", "run", "--rm", "--read-only", "--cap-drop=ALL",
            "--tmpfs=/tmp:rw,nosuid,nodev,noexec,mode=1777"]
    for value in env:
        args.extend(["-e", value])
    result = subprocess.run([*args, "chrony-log:latest", "-p"],
                            capture_output=True, text=True)
    if expected == 0:
        assert result.returncode == 0, result.stderr
    else:
        assert result.returncode != 0, result.stdout
    return result.stdout


default = run()
assert "pool pool.ntp.org iburst maxsources 4" in default
assert not any(line.startswith("allow ") for line in default.splitlines())
custom = run(["NTP_POOLS=", "NTP_SERVERS=time.example.org 2001:db8::1",
              "ALLOW_CIDR=192.0.2.0/24 2001:db8::/32"])
assert "pool " not in custom
for line in ("server time.example.org iburst", "server 2001:db8::1 iburst",
             "allow 192.0.2.0/24", "allow 2001:db8::/32"):
    assert line in custom, custom
run(["ALLOW_CIDR=192.0.2.0/99"], expected=1)
run(["ALLOW_CIDR=999.0.2.0/24"], expected=1)
run(["ALLOW_CIDR=2001:db8::/129"], expected=1)
run(["ALLOW_CIDR=all"], expected=1)
run(["NTP_SERVERS=time.example.org;touch"], expected=1)
run(["CHRONY_CONFIG=/missing.conf"], expected=1)
for offset in ("+0300", "-0530", "+1400", "+0000"):
    run(["LOG_TIME_OFFSET=" + offset])
for offset in ("+1460", "+1401", "+030", "MSK", "+03:00"):
    run(["LOG_TIME_OFFSET=" + offset], expected=1)
print("PASS: environment configuration, IPv4/IPv6, defaults, invalid values")
